#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dht_sensor.h"
#include "ina226.h"
#include "oled_display.h"
#include "temp_sensors.h"
#include "touch.h"
#include "remote_logs.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "powerbank64";
static const char *NVS_NAMESPACE = "pb_meter";
static pb_ina226_t s_ina;
static pb_pack_sample_t s_sample;
static pb_temp_raw_sample_t s_temperature;
static pb_dht_sample_t s_dht;
static nvs_handle_t s_nvs;
static bool s_ina_ready;
static bool s_touch_ready;
static bool s_display_ready;
static bool s_temperature_ready;
static bool s_dht_ready;
static uint64_t s_energy_mwh;
static uint64_t s_discharge_mah;
static uint64_t s_energy_remainder;
static uint64_t s_discharge_remainder;
static int s_screen;

#define SCREEN_COUNT 4
#define SAMPLE_PERIOD_US 1000000LL
#define DISPLAY_PERIOD_US 250000LL
#define SAVE_PERIOD_US 300000000LL
#define DHT_SAMPLE_PERIOD_US 2200000LL

static esp_err_t load_counter(const char *key, uint64_t *value)
{
    esp_err_t err = nvs_get_u64(s_nvs, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return ESP_OK;
    }
    return err;
}

static esp_err_t save_counters(void)
{
    ESP_RETURN_ON_ERROR(nvs_set_u64(s_nvs, "energy_mwh", s_energy_mwh),
                        TAG, "energy counter write failed");
    ESP_RETURN_ON_ERROR(nvs_set_u64(s_nvs, "discharge_mah", s_discharge_mah),
                        TAG, "discharge counter write failed");
    return nvs_commit(s_nvs);
}

static void reset_counters(void)
{
    s_energy_mwh = 0;
    s_discharge_mah = 0;
    s_energy_remainder = 0;
    s_discharge_remainder = 0;
    esp_err_t err = save_counters();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not persist reset counters: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "energy and discharge counters reset");
    }
}

static void show_previous_page(void)
{
    s_screen = (s_screen + SCREEN_COUNT - 1) % SCREEN_COUNT;
}

static void show_next_page(void)
{
    s_screen = (s_screen + 1) % SCREEN_COUNT;
}

static void accumulate_energy(uint32_t dt_ms)
{
    if (!s_sample.power_valid || s_sample.power_mw <= 0) return;
    uint64_t energy_numerator =
        (uint64_t)s_sample.power_mw * dt_ms + s_energy_remainder;
    s_energy_mwh += energy_numerator / 3600000ULL;
    s_energy_remainder = energy_numerator % 3600000ULL;

    uint64_t discharge_numerator =
        (uint64_t)s_sample.current_ma * dt_ms + s_discharge_remainder;
    s_discharge_mah += discharge_numerator / 3600000ULL;
    s_discharge_remainder = discharge_numerator % 3600000ULL;
}

static void handle_touch(void)
{
    if (!s_touch_ready) return;
    pb_touch_event_t event = pb_touch_poll();
    if (event != PB_TOUCH_EVENT_NONE) {
        ESP_LOGI(TAG, "touch event=%d", (int)event);
    }
    switch (event) {
    case PB_TOUCH_EVENT_UP:
    case PB_TOUCH_EVENT_DOUBLE_UP:
    case PB_TOUCH_EVENT_LONG_UP:
        show_previous_page();
        break;
    case PB_TOUCH_EVENT_DOWN:
    case PB_TOUCH_EVENT_OK:
        show_next_page();
        break;
    case PB_TOUCH_EVENT_DOUBLE_OK:
        reset_counters();
        break;
    case PB_TOUCH_EVENT_DOUBLE_DOWN:
    case PB_TOUCH_EVENT_LONG_DOWN:
    case PB_TOUCH_EVENT_LONG_OK:
    case PB_TOUCH_EVENT_NONE:
    default:
        break;
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    const TickType_t poll_period = pdMS_TO_TICKS(20);
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_sample_us = esp_timer_get_time();
    int64_t last_dht_sample_us = 0;
    int64_t last_display_us = 0;
    int64_t last_save_us = last_sample_us;

    for (;;) {
        vTaskDelayUntil(&last_wake, poll_period);
        int64_t now_us = esp_timer_get_time();
        handle_touch();

        if (now_us - last_sample_us >= SAMPLE_PERIOD_US) {
            uint32_t dt_ms = (uint32_t)((now_us - last_sample_us) / 1000);
            last_sample_us = now_us;
            memset(&s_sample, 0, sizeof(s_sample));

            if (s_ina_ready) {
                esp_err_t err = pb_ina226_read(&s_ina, &s_sample);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "INA226 read failed: %s", esp_err_to_name(err));
                }
            }
            if (s_temperature_ready) {
                esp_err_t err = pb_temp_sensors_read(&s_temperature);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "temperature ADC read failed: %s", esp_err_to_name(err));
                }
            }
            accumulate_energy(dt_ms);
            ESP_LOGI(TAG, "INA=%s bus=%" PRIu32 "mV current=%" PRId32
                     "mA power=%" PRId32 "mW energy=%" PRIu64 "mWh used=%"
                     PRIu64 "mAh screen=%d",
                     s_sample.bus_valid ? "OK" : "FAULT",
                     s_sample.bus_mv, s_sample.current_ma, s_sample.power_mw,
                     s_energy_mwh, s_discharge_mah, s_screen);
        }

        if (s_dht_ready && now_us - last_dht_sample_us >= DHT_SAMPLE_PERIOD_US) {
            last_dht_sample_us = now_us;
            esp_err_t err = pb_dht_sensor_read(&s_dht);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "DHT22 read failed: %s", esp_err_to_name(err));
            }
        }

        if (now_us - last_display_us >= DISPLAY_PERIOD_US) {
            last_display_us = now_us;
            if (s_display_ready) {
                pb_display_update(&s_sample, &s_temperature, &s_dht,
                                  s_energy_mwh, s_discharge_mah,
                                  s_sample.bus_valid, s_screen);
            }
        }

        if (now_us - last_save_us >= SAVE_PERIOD_US) {
            last_save_us = now_us;
            esp_err_t err = save_counters();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "counter persistence failed: %s", esp_err_to_name(err));
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
    esp_err_t err = pb_remote_logs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "remote JSON logging unavailable: %s", esp_err_to_name(err));
    }

    err = load_counter("energy_mwh", &s_energy_mwh);
    if (err != ESP_OK) ESP_LOGE(TAG, "energy counter load failed: %s", esp_err_to_name(err));
    err = load_counter("discharge_mah", &s_discharge_mah);
    if (err != ESP_OK) ESP_LOGE(TAG, "discharge counter load failed: %s", esp_err_to_name(err));

    err = pb_ina226_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
    } else {
        int current_sign = 1;
#ifdef CONFIG_PB_INA226_INVERT_CURRENT
        current_sign = -1;
#endif
        err = pb_ina226_open(&s_ina, CONFIG_PB_INA226_ADDRESS,
                             CONFIG_PB_INA226_SHUNT_UOHM, current_sign);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "INA226 unavailable: %s", esp_err_to_name(err));
        } else {
            s_ina_ready = true;
        }
        err = pb_display_init(pb_ina226_bus_handle());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OLED unavailable: %s", esp_err_to_name(err));
        } else {
            s_display_ready = true;
            pb_display_show_splash(4000);
        }
    }

    err = pb_touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch buttons unavailable: %s", esp_err_to_name(err));
    } else {
        s_touch_ready = true;
    }

    err = pb_temp_sensors_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "temperature ADC unavailable: %s", esp_err_to_name(err));
    } else {
        s_temperature_ready = true;
    }

    err = pb_dht_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DHT22 unavailable: %s", esp_err_to_name(err));
    } else {
        s_dht_ready = true;
    }

    ESP_LOGW(TAG, "monitor-only firmware: power outputs are not controlled by ESP32");
    if (xTaskCreate(monitor_task, "power_monitor", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start monitor task");
    }
}
