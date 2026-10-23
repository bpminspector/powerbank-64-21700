#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t pb_radio_init(void);
bool pb_wifi_connected(void);
