#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    uint32_t bus_mv;
    int32_t shunt_uv;
    int32_t current_ma;
    int32_t power_mw;
    bool bus_valid;
    bool current_valid;
    bool power_valid;
} pb_pack_sample_t;

typedef struct {
    i2c_master_dev_handle_t dev;
    uint32_t shunt_uohm;
    int current_sign;
    bool ready;
} pb_ina226_t;

esp_err_t pb_ina226_bus_init(void);
esp_err_t pb_ina226_open(pb_ina226_t *sensor, uint8_t address,
                         uint32_t shunt_uohm, int current_sign);
esp_err_t pb_ina226_read(pb_ina226_t *sensor, pb_pack_sample_t *sample);
i2c_master_bus_handle_t pb_ina226_bus_handle(void);
