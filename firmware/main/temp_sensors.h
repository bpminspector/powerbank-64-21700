#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int raw[2];
    bool valid[2];
} pb_temp_raw_sample_t;

esp_err_t pb_temp_sensors_init(void);
esp_err_t pb_temp_sensors_read(pb_temp_raw_sample_t *sample);
