#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Three touch pads: UP / OK / DOWN. GPIO numbers come from Kconfig. */
#define PB_TOUCH_UP_GPIO     CONFIG_PB_TOUCH_LEFT_GPIO
#define PB_TOUCH_OK_GPIO     CONFIG_PB_TOUCH_OK_GPIO
#define PB_TOUCH_DOWN_GPIO   CONFIG_PB_TOUCH_RIGHT_GPIO

typedef enum {
    PB_TOUCH_EVENT_NONE = 0,
    PB_TOUCH_EVENT_UP,
    PB_TOUCH_EVENT_OK,
    PB_TOUCH_EVENT_DOWN,
    PB_TOUCH_EVENT_LONG_UP,
    PB_TOUCH_EVENT_LONG_OK,
    PB_TOUCH_EVENT_LONG_DOWN,
    PB_TOUCH_EVENT_DOUBLE_UP,
    PB_TOUCH_EVENT_DOUBLE_OK,
    PB_TOUCH_EVENT_DOUBLE_DOWN,
} pb_touch_event_t;

esp_err_t pb_touch_init(void);

/* Poll for a decoded touch event.  Returns PB_TOUCH_EVENT_NONE when idle. */
pb_touch_event_t pb_touch_poll(void);

/* Returns true while the pad is currently held down. */
bool pb_touch_held(pb_touch_event_t ev);