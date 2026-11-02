#include "outputs.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ina226.h"

static const char *TAG = "outputs";
static pb_output_state_t s;
static const int s_enable_gpios[PB_OUT_COUNT] = {
    CONFIG_PB_ENABLE_12V_GPIO, CONFIG_PB_ENABLE_24V_GPIO, CONFIG_PB_ENABLE_USB_GPIO,
};
static bool s_fault_indicate_pending = false;

static pb_pack_sample_t s_a;
static pb_pack_sample_t s_b;

static void drive_output(pb_output_t out, bool on)
{
    bool was = s.out_on[out];
    s.out_on[out] = on;
    if (was == on) return;
    ESP_ERROR_CHECK(gpio_set_level(s_enable_gpios[out], on ? 1 : 0));
    ESP_LOGI(TAG, "output %d -> %s", (int)out, on ? "ON" : "OFF");
}

static void set_state(pb_system_state_t st)
{
    if (s.state == st) return;
    ESP_LOGI(TAG, "state %d -> %d", (int)s.state, (int)st);
    s.state = st;
}

static bool block_is_ok(const pb_pack_sample_t *smp)
{
    return smp && smp->bus_valid && smp->bus_mv >= (uint32_t)CONFIG_PB_CUTOFF_MV;
}

static bool block_is_charged(const pb_pack_sample_t *smp)
{
    return smp && smp->bus_valid && smp->bus_mv >= (uint32_t)CONFIG_PB_CHARGE_CV_MV - 200;
}

static unsigned estimate_soc(const pb_pack_sample_t *smp)
{
    if (!smp || !smp->bus_valid) return 0;
    uint32_t mv = smp->bus_mv;
    if (mv < 3200) return 0;
    if (mv > 4200) return 100;
    return (unsigned)((mv - 3200) * 100 / 1000);
}

esp_err_t pb_outputs_init(void)
{
    const uint64_t mask =
        (1ULL << s_enable_gpios[PB_OUT_12V]) |
        (1ULL << s_enable_gpios[PB_OUT_24V]) |
        (1ULL << s_enable_gpios[PB_OUT_USB]);
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "enable GPIO config failed");
    drive_output(PB_OUT_12V, false);
    drive_output(PB_OUT_24V, false);
    drive_output(PB_OUT_USB, false);

    gpio_config_t chg = {
        .pin_bit_mask = 1ULL << CONFIG_PB_CHARGER_DETECT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&chg), TAG, "charger detect GPIO config failed");

    memset(&s, 0, sizeof(s));
    s.state = PB_STATE_OFF;
    ESP_LOGI(TAG, "outputs init: all groups OFF, safe state");
    return ESP_OK;
}

void pb_outputs_set_enabled(pb_output_t out, bool enabled)
{
    if (out >= PB_OUT_COUNT) return;
    s.out_enabled[out] = enabled;
}

bool pb_outputs_is_on(pb_output_t out)
{
    return out < PB_OUT_COUNT && s.out_on[out];
}

pb_system_state_t pb_outputs_state(void) { return s.state; }
const pb_output_state_t *pb_outputs_snapshot(void) { return &s; }

unsigned pb_outputs_active_soc_percent(void)
{
    return estimate_soc(&s_a) + estimate_soc(&s_b);
}

bool pb_outputs_fault_indicate_pending(void)
{
    return s_fault_indicate_pending;
}

void pb_outputs_ack_fault_indicate(void)
{
    s_fault_indicate_pending = false;
}

/* Acknowledge a latched fault: return to READY only while the underlying
 * condition has cleared.  Called from the UI (long press). */
void pb_outputs_ack_fault(void)
{
    if (s.state != PB_STATE_FAULT) return;
    if (both_ok(&s_a, &s_b)) {
        set_state(PB_STATE_READY);
        s_fault_indicate_pending = false;
    }
}

static void apply_user_toggles(void)
{
    for (int i = 0; i < PB_OUT_COUNT; i++) {
        bool want = s.out_enabled[i] && s.state == PB_STATE_RUN;
        drive_output((pb_output_t)i, want);
    }
}

static bool both_ok(const pb_pack_sample_t *a, const pb_pack_sample_t *b)
{
    return block_is_ok(a) || block_is_ok(b);
}

void pb_outputs_update(const pb_pack_sample_t *a, const pb_pack_sample_t *b)
{
    if (a) s_a = *a;
    if (b) s_b = *b;
    s.charger_present = gpio_get_level(CONFIG_PB_CHARGER_DETECT_GPIO) == 1;

    if (s.charger_present) {
        if (s.state != PB_STATE_CHARGING) {
            set_state(PB_STATE_CHARGING);
            s_fault_indicate_pending = false;
        }
        s.charging = true;
        drive_output(PB_OUT_12V, false);
        drive_output(PB_OUT_24V, false);
        drive_output(PB_OUT_USB, false);
        return;
    }
    s.charging = false;

    if (!both_ok(a, b)) {
        if (s.state != PB_STATE_FAULT) {
            set_state(PB_STATE_FAULT);
            s_fault_indicate_pending = true;
        }
        drive_output(PB_OUT_12V, false);
        drive_output(PB_OUT_24V, false);
        drive_output(PB_OUT_USB, false);
        return;
    }

    if (s.state == PB_STATE_OFF) {
        set_state(PB_STATE_READY);
    }
    if (s.state == PB_STATE_READY || s.state == PB_STATE_RUN) {
        set_state(PB_STATE_RUN);
        apply_user_toggles();
    }
}