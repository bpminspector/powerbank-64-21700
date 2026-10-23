#include "radio.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "telemetry.h"

static const char *TAG = "radio";
static EventGroupHandle_t s_events;
static const EventBits_t WIFI_UP_BIT = BIT0;

bool pb_wifi_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & WIFI_UP_BIT);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (CONFIG_PB_WIFI_SSID[0] != '\0') esp_wifi_connect();
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_UP_BIT);
        if (CONFIG_PB_WIFI_SSID[0] != '\0') {
            ESP_LOGW(TAG, "STA disconnected; reconnecting");
            esp_wifi_connect();
        }
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, WIFI_UP_BIT);
        ESP_LOGI(TAG, "STA has IP; ESP-NOW follows the AP channel");
    }
}

esp_err_t pb_radio_init(void)
{
    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (CONFIG_PB_WIFI_SSID[0] != '\0') {
        wifi_config_t sta = {0};
        memcpy(sta.sta.ssid, CONFIG_PB_WIFI_SSID,
               strnlen(CONFIG_PB_WIFI_SSID, sizeof(sta.sta.ssid) - 1));
        memcpy(sta.sta.password, CONFIG_PB_WIFI_PASSWORD,
               strnlen(CONFIG_PB_WIFI_PASSWORD, sizeof(sta.sta.password) - 1));
        sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    if (CONFIG_PB_WIFI_SSID[0] == '\0') {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_PB_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
        ESP_LOGI(TAG, "No AP configured; ESP-NOW uses fixed channel %d", CONFIG_PB_WIFI_CHANNEL);
    }

    /* ESP-NOW peer setup is optional and fails closed unless MAC+LMK are configured. */
    pb_telemetry_init();

    return ESP_OK;
}
