#include "dht_sensor.h"

#include <stddef.h>

#include "dht.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "dht22";
static bool s_initialized;

esp_err_t pb_dht_sensor_init(void)
{
    const int pin = CONFIG_PB_DHT_GPIO;
    const int reserved_pins[] = {
        CONFIG_PB_I2C_SDA_GPIO, CONFIG_PB_I2C_SCL_GPIO,
        CONFIG_PB_TOUCH_LEFT_GPIO, CONFIG_PB_TOUCH_OK_GPIO,
        CONFIG_PB_TOUCH_RIGHT_GPIO, CONFIG_PB_NTC1_GPIO,
        CONFIG_PB_NTC2_GPIO, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
    };

    if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT || pin == 0 || pin == 3 ||
        pin == 45 || pin == 46) {
        ESP_LOGE(TAG, "GPIO %d is invalid or reserved", pin);
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(reserved_pins) / sizeof(reserved_pins[0]); i++) {
        if (pin == reserved_pins[i]) {
            ESP_LOGE(TAG, "DHT22 GPIO %d conflicts with another assigned pin", pin);
            return ESP_ERR_INVALID_ARG;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "DHT22 configured on GPIO %d", pin);
    return ESP_OK;
}

esp_err_t pb_dht_sensor_read(pb_dht_sample_t *sample)
{
    if (!sample) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    sample->valid = false;

    int16_t humidity = 0;
    int16_t temperature = 0;
    esp_err_t err = dht_read_data(DHT_TYPE_AM2301, (gpio_num_t)CONFIG_PB_DHT_GPIO,
                                  &humidity, &temperature);
    if (err != ESP_OK) return err;
    if (humidity < 0 || humidity > 1000 ||
        temperature < -400 || temperature > 800) {
        ESP_LOGW(TAG, "DHT22 returned out-of-range data");
        return ESP_ERR_INVALID_RESPONSE;
    }

    sample->humidity_pct10 = humidity;
    sample->temperature_c10 = temperature;
    sample->valid = true;
    return ESP_OK;
}
