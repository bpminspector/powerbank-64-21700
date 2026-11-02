#include "touch.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

static const char *TAG = "buttons";

static const gpio_num_t s_button_gpios[3] = {
    PB_TOUCH_UP_GPIO,
    PB_TOUCH_OK_GPIO,
    PB_TOUCH_DOWN_GPIO,
};
static bool s_initialized;
static bool s_down[3];
static bool s_candidate[3];
static uint8_t s_candidate_count[3];
static int64_t s_press_start[3];
static int s_pending_click = -1;
static int64_t s_pending_click_us;

#define DOUBLE_CLICK_WINDOW_US 350000
#define BUTTON_DEBOUNCE_SAMPLES 3

static bool button_read_debounced(int idx)
{
    int level = gpio_get_level(s_button_gpios[idx]);
#ifdef CONFIG_PB_TOUCH_ACTIVE_LOW
    bool active = level == 0;
#else
    bool active = level != 0;
#endif

    if (active != s_candidate[idx]) {
        s_candidate[idx] = active;
        s_candidate_count[idx] = 1;
    } else if (s_candidate_count[idx] < BUTTON_DEBOUNCE_SAMPLES) {
        s_candidate_count[idx]++;
    }

    return s_candidate_count[idx] >= BUTTON_DEBOUNCE_SAMPLES
               ? s_candidate[idx]
               : s_down[idx];
}

esp_err_t pb_touch_init(void)
{
    uint64_t pin_mask = 0;
    for (int i = 0; i < 3; i++) {
        int pin = s_button_gpios[i];
        if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT ||
            pin == CONFIG_PB_I2C_SDA_GPIO || pin == CONFIG_PB_I2C_SCL_GPIO ||
            pin == CONFIG_PB_NTC1_GPIO || pin == CONFIG_PB_NTC2_GPIO ||
            (pin_mask & (1ULL << pin))) {
            ESP_LOGE(TAG, "button GPIO %d is invalid or conflicts with another pin", pin);
            return ESP_ERR_INVALID_ARG;
        }
        pin_mask |= 1ULL << pin;
    }

    const gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "button GPIO configuration failed");

    for (int i = 0; i < 3; i++) {
        s_down[i] = false;
        s_candidate[i] = false;
        s_candidate_count[i] = 0;
        s_press_start[i] = 0;
    }
    s_pending_click = -1;
    s_initialized = true;
    ESP_LOGI(TAG, "digital buttons UP=%d OK=%d DOWN=%d active=%s",
             s_button_gpios[0], s_button_gpios[1], s_button_gpios[2],
#ifdef CONFIG_PB_TOUCH_ACTIVE_LOW
             "low"
#else
             "high"
#endif
    );
    return ESP_OK;
}

static pb_touch_event_t index_to_event(int idx, bool long_press)
{
    switch (idx) {
    case 0: return long_press ? PB_TOUCH_EVENT_LONG_UP : PB_TOUCH_EVENT_UP;
    case 1: return long_press ? PB_TOUCH_EVENT_LONG_OK : PB_TOUCH_EVENT_OK;
    case 2: return long_press ? PB_TOUCH_EVENT_LONG_DOWN : PB_TOUCH_EVENT_DOWN;
    default: return PB_TOUCH_EVENT_NONE;
    }
}

static pb_touch_event_t index_to_double_event(int idx)
{
    switch (idx) {
    case 0: return PB_TOUCH_EVENT_DOUBLE_UP;
    case 1: return PB_TOUCH_EVENT_DOUBLE_OK;
    case 2: return PB_TOUCH_EVENT_DOUBLE_DOWN;
    default: return PB_TOUCH_EVENT_NONE;
    }
}

pb_touch_event_t pb_touch_poll(void)
{
    if (!s_initialized) return PB_TOUCH_EVENT_NONE;

    int64_t now = esp_timer_get_time();
    pb_touch_event_t expired_click = PB_TOUCH_EVENT_NONE;

    for (int i = 0; i < 3; i++) {
        bool active = button_read_debounced(i);
        if (active && !s_down[i]) {
            s_down[i] = true;
            s_press_start[i] = now;
        } else if (!active && s_down[i]) {
            s_down[i] = false;
            int64_t held_ms = (now - s_press_start[i]) / 1000;
            if (held_ms >= CONFIG_PB_TOUCH_LONG_MS) {
                if (s_pending_click == i) s_pending_click = -1;
                return index_to_event(i, true);
            }
            if (s_pending_click == i && now - s_pending_click_us <= DOUBLE_CLICK_WINDOW_US) {
                s_pending_click = -1;
                return index_to_double_event(i);
            }
            if (s_pending_click >= 0 &&
                (s_pending_click != i ||
                 now - s_pending_click_us > DOUBLE_CLICK_WINDOW_US)) {
                expired_click = index_to_event(s_pending_click, false);
                s_pending_click = -1;
            }
            s_pending_click = i;
            s_pending_click_us = now;
            if (expired_click != PB_TOUCH_EVENT_NONE) return expired_click;
        }
    }
    if (s_pending_click >= 0 &&
        now - s_pending_click_us > DOUBLE_CLICK_WINDOW_US) {
        int idx = s_pending_click;
        s_pending_click = -1;
        return index_to_event(idx, false);
    }
    return PB_TOUCH_EVENT_NONE;
}

bool pb_touch_held(pb_touch_event_t ev)
{
    switch (ev) {
    case PB_TOUCH_EVENT_UP:
    case PB_TOUCH_EVENT_LONG_UP: return s_down[0];
    case PB_TOUCH_EVENT_OK:
    case PB_TOUCH_EVENT_LONG_OK: return s_down[1];
    case PB_TOUCH_EVENT_DOWN:
    case PB_TOUCH_EVENT_LONG_DOWN: return s_down[2];
    default: return false;
    }
}
