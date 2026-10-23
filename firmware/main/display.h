#pragma once

#include <stdbool.h>
#include "ina226.h"
#include "esp_err.h"

esp_err_t pb_display_init(void);
void pb_display_update(const pb_pack_sample_t *a, const pb_pack_sample_t *b,
                       unsigned energy_a_mwh, unsigned energy_b_mwh,
                       bool wifi_connected, bool espnow_ready);
