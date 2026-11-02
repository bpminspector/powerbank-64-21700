#include "leds.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "leds";
static const int s_led_gpios[PB_LED_COUNT] = {
    CONFIG_PB_LED_12V_GPIO,
    CONFIG_PB_LED_24V_GPIO,
    CONFIG_PB_LED_USB_GPIO,
    CONFIG_PB_LED_CHARGE_GPIO,
    CONFIG_PB_LED_FAULT_GPIO,
};
static bool s_on[PB_LED_COUNT];

static void led_set(pb_led_t led, bool on)
{
    if (led >= PB_LED_COUNT) return;
    s_on[led] = on;
    ESP_ERROR_CHECK(gpio_set_level(s_led_gpios[led], on ? 1 : 0));
}

bool pb_led_is_on(pb_led_t led)
{
    return led < PB_LED_COUNT && s_on[led];
}

esp_err_t pb_leds_init(void)
{
    const uint64_t mask =
        (1ULL << s_led_gpios[PB_LED_12V]) |
        (1ULL << s_led_gpios[PB_LED_24V]) |
        (1ULL << s_led_gpios[PB_LED_USB]) |
        (1ULL << s_led_gpios[PB_LED_CHARGE]) |
        (1ULL << s_led_gpios[PB_LED_FAULT]);
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "LED GPIO config failed");
    for (int i = 0; i < PB_LED_COUNT; i++) led_set((pb_led_t)i, false);
    ESP_LOGI(TAG, "LEDs %d/%d/%d/%d/%d ready (all OFF)",
             s_led_gpios[PB_LED_12V], s_led_gpios[PB_LED_24V],
             s_led_gpios[PB_LED_USB], s_led_gpios[PB_LED_CHARGE],
             s_led_gpios[PB_LED_FAULT]);
    return ESP_OK;
}

void pb_leds_update(const pb_output_state_t *outs)
{
    if (!outs) return;
    led_set(PB_LED_12V, outs->out_on[PB_OUT_12V]);
    led_set(PB_LED_24V, outs->out_on[PB_OUT_24V]);
    led_set(PB_LED_USB, outs->out_on[PB_OUT_USB]);
    led_set(PB_LED_CHARGE, outs->charger_present || outs->charging);
    led_set(PB_LED_FAULT, outs->state == PB_STATE_FAULT);
}

void pb_leds_fault_indicate(void)
{
    for (int i = 0; i < 3; i++) {
        led_set(PB_LED_FAULT, true);
        vTaskDelay(pdMS_TO_TICKS(120));
        led_set(PB_LED_FAULT, false);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    (void)esp_timer_get_time();
}