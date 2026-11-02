#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int16_t temperature_c10;
    int16_t humidity_pct10;
    bool valid;
} pb_dht_sample_t;

esp_err_t pb_dht_sensor_init(void);
esp_err_t pb_dht_sensor_read(pb_dht_sample_t *sample);
