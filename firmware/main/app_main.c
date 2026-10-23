#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ina226.h"
#include "display.h"
#include "radio.h"
#include "telemetry.h"
#include "nvs_flash.h"

static const char *TAG = "powerbank64";
static pb_ina226_t s_pack_a;
static pb_ina226_t s_pack_b;
static pb_pack_sample_t s_sample_a;
static pb_pack_sample_t s_sample_b;
static uint32_t s_energy_a_mwh;
static uint32_t s_energy_b_mwh;
static uint64_t s_energy_a_remainder;
static uint64_t s_energy_b_remainder;

static void keep_outputs_off(void)
{
    const uint64_t mask = (1ULL << CONFIG_PB_ENABLE_12V_GPIO) |
                          (1ULL << CONFIG_PB_ENABLE_24V_GPIO) |
                          (1ULL << CONFIG_PB_ENABLE_USB_GPIO);
    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_PB_ENABLE_12V_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_PB_ENABLE_24V_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_PB_ENABLE_USB_GPIO, 0));
    ESP_LOGW(TAG, "12 V / 24 V / USB requests are held OFF by firmware v0.1");
}

static void sensor_init(void)
{
    esp_err_t err = pb_ina226_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return;
    }
    err = pb_ina226_open(&s_pack_a, 0x40, CONFIG_PB_PACK_A_SHUNT_UOHM);
    if (err != ESP_OK) ESP_LOGW(TAG, "Pack A INA226 unavailable: %s", esp_err_to_name(err));
    err = pb_ina226_open(&s_pack_b, 0x41, CONFIG_PB_PACK_B_SHUNT_UOHM);
    if (err != ESP_OK) ESP_LOGW(TAG, "Pack B INA226 unavailable: %s", esp_err_to_name(err));
}

static void energy_accumulate(uint32_t *energy_mwh, uint64_t *remainder,
                              const pb_pack_sample_t *sample, uint32_t dt_ms)
{
    if (!sample->bus_valid || !sample->current_valid || sample->current_ma <= 0) return;
    uint64_t numerator = (uint64_t)sample->bus_mv * (uint32_t)sample->current_ma * dt_ms + *remainder;
    uint64_t add = numerator / 3600000000ULL;
    *remainder = numerator % 3600000000ULL;
    *energy_mwh += (uint32_t)add;
}

static void monitor_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(1000);
    TickType_t last = xTaskGetTickCount();
    int64_t last_us = esp_timer_get_time();
    for (;;) {
        vTaskDelayUntil(&last, period);
        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;

        memset(&s_sample_a, 0, sizeof(s_sample_a));
        memset(&s_sample_b, 0, sizeof(s_sample_b));
        if (s_pack_a.ready && pb_ina226_read(&s_pack_a, &s_sample_a) != ESP_OK) {
            ESP_LOGW(TAG, "Pack A sensor read failed");
        }
        if (s_pack_b.ready && pb_ina226_read(&s_pack_b, &s_sample_b) != ESP_OK) {
            ESP_LOGW(TAG, "Pack B sensor read failed");
        }
        energy_accumulate(&s_energy_a_mwh, &s_energy_a_remainder, &s_sample_a, dt_ms);
        energy_accumulate(&s_energy_b_mwh, &s_energy_b_remainder, &s_sample_b, dt_ms);

        ESP_LOGI(TAG,
                 "node=%d A=%" PRIu32 "mV/%" PRId32 "mA B=%" PRIu32 "mV/%" PRId32
                 "mA energy=%" PRIu32 "/%" PRIu32 "mWh wifi=%d",
                 CONFIG_PB_NODE_ID,
                 s_sample_a.bus_mv, s_sample_a.current_ma,
                 s_sample_b.bus_mv, s_sample_b.current_ma,
                 s_energy_a_mwh, s_energy_b_mwh,
                 pb_wifi_connected());

        pb_telemetry_send(&s_sample_a, &s_sample_b, s_energy_a_mwh, s_energy_b_mwh,
                          pb_wifi_connected());
        pb_display_update(&s_sample_a, &s_sample_b, s_energy_a_mwh, s_energy_b_mwh,
                          pb_wifi_connected(), pb_telemetry_ready());
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    keep_outputs_off();
    sensor_init();
    ESP_ERROR_CHECK(pb_radio_init());
    esp_err_t display_err = pb_display_init();
    if (display_err != ESP_OK) {
        ESP_LOGW(TAG, "display unavailable: %s", esp_err_to_name(display_err));
    }
    if (xTaskCreate(monitor_task, "power_monitor", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start monitor task");
    }
}
