#include "ina226.h"

#include "esp_log.h"

static const char *TAG = "ina226";
static i2c_master_bus_handle_t s_bus;

#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_VOLT   0x01
#define INA226_REG_BUS_VOLT     0x02
#define INA226_CONFIG_CONTINUOUS 0x4527

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value)
{
    uint8_t data[] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t *value)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, rx, sizeof(rx), 100);
    if (err == ESP_OK) {
        *value = ((uint16_t)rx[0] << 8) | rx[1];
    }
    return err;
}

esp_err_t pb_ina226_bus_init(void)
{
    if (s_bus) return ESP_OK;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_PB_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_PB_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &s_bus);
}

esp_err_t pb_ina226_open(pb_ina226_t *sensor, uint8_t address, uint32_t shunt_uohm)
{
    if (!sensor || !s_bus) return ESP_ERR_INVALID_STATE;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &config, &sensor->dev);
    if (err != ESP_OK) return err;
    sensor->shunt_uohm = shunt_uohm;
    err = write_reg(sensor->dev, INA226_REG_CONFIG, INA226_CONFIG_CONTINUOUS);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(sensor->dev);
        sensor->dev = NULL;
        return err;
    }
    sensor->ready = true;
    ESP_LOGI(TAG, "INA226 at 0x%02x ready; shunt=%lu uOhm%s", address,
             (unsigned long)shunt_uohm, shunt_uohm ? "" : " (current disabled)");
    return ESP_OK;
}

esp_err_t pb_ina226_read(pb_ina226_t *sensor, pb_pack_sample_t *sample)
{
    if (!sensor || !sample || !sensor->ready) return ESP_ERR_INVALID_STATE;
    uint16_t raw_bus, raw_shunt;
    esp_err_t err = read_reg(sensor->dev, INA226_REG_BUS_VOLT, &raw_bus);
    if (err != ESP_OK) return err;
    err = read_reg(sensor->dev, INA226_REG_SHUNT_VOLT, &raw_shunt);
    if (err != ESP_OK) return err;

    sample->bus_mv = ((uint32_t)raw_bus * 5U) / 4U; /* 1.25 mV/LSB */
    sample->bus_valid = true;
    sample->current_valid = sensor->shunt_uohm != 0;
    sample->current_ma = 0;
    if (sample->current_valid) {
        int32_t shunt_uv = (int16_t)raw_shunt * 5 / 2; /* 2.5 uV/LSB */
        sample->current_ma = (int32_t)(((int64_t)shunt_uv * 1000) / sensor->shunt_uohm);
    }
    return ESP_OK;
}
