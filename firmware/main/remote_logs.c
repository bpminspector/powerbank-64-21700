#include "remote_logs.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pb_wifi_config.h"

static const char *TAG = "remote_logs";
#define WIFI_CONNECTED_BIT BIT0
#define LOG_LINE_SIZE 384
#define LOG_BATCH_SIZE 8
#define LOG_BODY_SIZE 4096
#define LOG_PARTIAL_SLOTS 16

typedef struct {
    TaskHandle_t owner;
    size_t length;
    char text[LOG_LINE_SIZE];
} partial_log_t;

static QueueHandle_t s_log_queue;
static EventGroupHandle_t s_wifi_events;
static vprintf_like_t s_original_vprintf;
static char s_log_batch[LOG_BATCH_SIZE][LOG_LINE_SIZE];
static char s_log_body[LOG_BODY_SIZE];
static partial_log_t s_partial_logs[LOG_PARTIAL_SLOTS];
static portMUX_TYPE s_partial_logs_lock = portMUX_INITIALIZER_UNLOCKED;

static int forwarding_vprintf(const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[LOG_LINE_SIZE];
    int formatted = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);

    if (formatted > 0 && s_log_queue) {
        TaskHandle_t owner = xTaskGetCurrentTaskHandle();
        partial_log_t *partial = NULL;
        partial_log_t *available = NULL;
        portENTER_CRITICAL(&s_partial_logs_lock);
        for (size_t i = 0; i < LOG_PARTIAL_SLOTS; ++i) {
            if (s_partial_logs[i].owner == owner) {
                partial = &s_partial_logs[i];
                break;
            }
            if (!available && !s_partial_logs[i].owner) {
                available = &s_partial_logs[i];
            }
        }
        if (!partial) {
            partial = available ? available : &s_partial_logs[(uintptr_t)owner % LOG_PARTIAL_SLOTS];
            partial->owner = owner;
            partial->length = 0;
        }

        char completed[LOG_LINE_SIZE];
        bool has_complete_line = false;
        for (const char *p = line; *p; ++p) {
            if (*p == '\r') continue;
            if (*p == '\n' || partial->length >= sizeof(partial->text) - 1) {
                partial->text[partial->length] = '\0';
                memcpy(completed, partial->text, partial->length + 1);
                partial->length = 0;
                has_complete_line = true;
                if (*p == '\n') continue;
            }
            partial->text[partial->length++] = *p;
        }
        portEXIT_CRITICAL(&s_partial_logs_lock);
        if (has_complete_line && completed[0] != '\0') {
            (void)xQueueSend(s_log_queue, completed, 0);
        }
    }

    return s_original_vprintf ? s_original_vprintf(format, args) : formatted;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        (void)esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected; remote JSON logging enabled");
    }
}

static bool append_json_string(char *output, size_t capacity, size_t *used,
                               const char *text)
{
    if (*used >= capacity || capacity - *used < 2) return false;
    output[(*used)++] = '"';
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *escape = NULL;
        switch (*p) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape) {
            size_t n = strlen(escape);
            if (capacity - *used <= n) return false;
            memcpy(output + *used, escape, n);
            *used += n;
        } else if (*p < 0x20) {
            if (capacity - *used <= 6) return false;
            int n = snprintf(output + *used, capacity - *used, "\\u%04x", *p);
            if (n != 6) return false;
            *used += 6;
        } else {
            if (capacity - *used <= 1) return false;
            output[(*used)++] = (char)*p;
        }
    }
    if (*used >= capacity) return false;
    output[(*used)++] = '"';
    output[*used] = '\0';
    return true;
}

static bool build_log_batch(char lines[LOG_BATCH_SIZE][LOG_LINE_SIZE],
                            size_t count, char *body, size_t capacity)
{
    int prefix = snprintf(body, capacity,
                          "{\"device\":\"PowerBank64-ESP32-S3\","
                          "\"logs\":[");
    if (prefix < 0 || (size_t)prefix >= capacity) return false;
    size_t used = (size_t)prefix;
    for (size_t i = 0; i < count; ++i) {
        int n = snprintf(body + used, capacity - used,
                         "%s{\"uptime_ms\":%lld,\"message\":",
                         i ? "," : "",
                         (long long)(esp_timer_get_time() / 1000));
        if (n < 0 || (size_t)n >= capacity - used) return false;
        used += (size_t)n;
        if (!append_json_string(body, capacity, &used, lines[i])) return false;
        if (capacity - used <= 1) return false;
        body[used++] = '}';
        body[used] = '\0';
    }
    if (capacity - used < 3) return false;
    memcpy(body + used, "]}", 3);
    return true;
}

static esp_err_t send_log_batch(char *body)
{
    char url[96];
    int n = snprintf(url, sizeof(url), "http://%s:%d/logs",
                     PB_LOG_SERVER_IP, PB_LOG_SERVER_PORT);
    if (n < 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 3000,
        .buffer_size = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && esp_http_client_get_status_code(client) != 200) {
        err = ESP_FAIL;
    }
    esp_http_client_cleanup(client);
    return err;
}

static void log_sender_task(void *arg)
{
    (void)arg;
    for (;;) {
        (void)xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                  pdFALSE, pdTRUE, portMAX_DELAY);

        memset(s_log_batch, 0, sizeof(s_log_batch));
        if (xQueueReceive(s_log_queue, s_log_batch[0],
                          pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        size_t count = 1;
        while (count < LOG_BATCH_SIZE &&
               xQueueReceive(s_log_queue, s_log_batch[count], 0) == pdTRUE) {
            ++count;
        }

        memset(s_log_body, 0, sizeof(s_log_body));
        if (!build_log_batch(s_log_batch, count, s_log_body, sizeof(s_log_body))) {
            continue;
        }
        esp_err_t err = send_log_batch(s_log_body);
        if (err != ESP_OK) {
            static int64_t last_error_us;
            int64_t now_us = esp_timer_get_time();
            if (now_us - last_error_us >= 10000000) {
                last_error_us = now_us;
                esp_rom_printf("remote_logs: POST failed: %s\n", esp_err_to_name(err));
            }
        }
    }
}

esp_err_t pb_remote_logs_init(void)
{
    s_log_queue = xQueueCreate(32, LOG_LINE_SIZE);
    if (!s_log_queue) return ESP_ERR_NO_MEM;
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;

    s_original_vprintf = esp_log_set_vprintf(forwarding_vprintf);
    if (xTaskCreate(log_sender_task, "remote_log_sender", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "network interface init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(TAG, "could not create Wi-Fi station interface");
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL),
                        TAG, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_event_handler, NULL),
                        TAG, "IP event registration failed");

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, PB_WIFI_SSID, strlen(PB_WIFI_SSID));
    memcpy(wifi_config.sta.password, PB_WIFI_PASSWORD, strlen(PB_WIFI_PASSWORD));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode setup failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                        TAG, "Wi-Fi credentials setup failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Wi-Fi connection request failed");
    return ESP_OK;
}
