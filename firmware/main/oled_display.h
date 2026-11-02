#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "dht_sensor.h"
#include "ina226.h"
#include "temp_sensors.h"

esp_err_t pb_display_init(i2c_master_bus_handle_t bus);
void pb_display_show_splash(uint32_t duration_ms);
void pb_display_update(const pb_pack_sample_t *sample,
                       const pb_temp_raw_sample_t *temperature,
                       const pb_dht_sample_t *dht,
                       uint64_t energy_mwh, uint64_t discharge_mah,
                       bool sensor_ready, int screen_index);
