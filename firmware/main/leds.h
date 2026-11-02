#pragma once
#include <stdbool.h>
#include "outputs.h"

/* Physical LED indicators on the control panel.
 * Each LED is driven through a transistor key, never directly from GPIO. */
typedef enum {
    PB_LED_12V = 0,   /* 12 V group active */
    PB_LED_24V,       /* 24 V group active */
    PB_LED_USB,       /* USB group active */
    PB_LED_CHARGE,    /* external charger present / pack charging */
    PB_LED_FAULT,     /* system fault latched */
    PB_LED_COUNT,
} pb_led_t;

esp_err_t pb_leds_init(void);

/* Refresh all LEDs from the current output state.  Called once per tick. */
void pb_leds_update(const pb_output_state_t *outs);

/* Flash a fault indicator three times; blocks briefly.  Call from FAULT path. */
void pb_leds_fault_indicate(void);

bool pb_led_is_on(pb_led_t led);

#endif