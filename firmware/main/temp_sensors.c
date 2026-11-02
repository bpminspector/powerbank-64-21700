#include "temp_sensors.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

static const char *TAG = "temp_adc";
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channels[2];

#define NTC_SAMPLE_COUNT 16

esp_err_t pb_temp_sensors_init(void)
{
    const int gpio_pins[2] = {CONFIG_PB_NTC1_GPIO, CONFIG_PB_NTC2_GPIO};
    adc_unit_t units[2];

    for (int i = 0; i < 2; i++) {
        if (gpio_pins[i] == CONFIG_PB_I2C_SDA_GPIO ||
            gpio_pins[i] == CONFIG_PB_I2C_SCL_GPIO ||
            gpio_pins[i] == CONFIG_PB_TOUCH_LEFT_GPIO ||
            gpio_pins[i] == CONFIG_PB_TOUCH_OK_GPIO ||
            gpio_pins[i] == CONFIG_PB_TOUCH_RIGHT_GPIO ||
            (i > 0 && gpio_pins[i] == gpio_pins[0])) {
            ESP_LOGE(TAG, "NTC GPIO %d conflicts with another configured pin", gpio_pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio_pins[i], &units[i],
                                                       &s_channels[i]),
                            TAG, "NTC GPIO is not ADC-capable");
        if (units[i] != ADC_UNIT_1) {
            ESP_LOGE(TAG, "NTC GPIO %d must use ADC1", gpio_pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_adc),
                        TAG, "ADC1 initialization failed");

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (int i = 0; i < 2; i++) {
        esp_err_t err = adc_oneshot_config_channel(s_adc, s_channels[i], &channel_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NTC channel %d configuration failed: %s",
                     i + 1, esp_err_to_name(err));
            adc_oneshot_del_unit(s_adc);
            s_adc = NULL;
            return err;
        }
    }

    ESP_LOGI(TAG, "NTC raw ADC inputs configured on GPIO %d and %d",
             gpio_pins[0], gpio_pins[1]);
    return ESP_OK;
}

esp_err_t pb_temp_sensors_read(pb_temp_raw_sample_t *sample)
{
    if (!sample || !s_adc) return ESP_ERR_INVALID_STATE;

    sample->valid[0] = false;
    sample->valid[1] = false;
    esp_err_t first_error = ESP_OK;
    for (int i = 0; i < 2; i++) {
        int total = 0;
        bool channel_ok = true;
        for (int read = 0; read < NTC_SAMPLE_COUNT; read++) {
            int raw = 0;
            esp_err_t err = adc_oneshot_read(s_adc, s_channels[i], &raw);
            if (err != ESP_OK) {
                if (first_error == ESP_OK) first_error = err;
                channel_ok = false;
                break;
            }
            total += raw;
        }
        if (channel_ok) {
            sample->raw[i] = total / NTC_SAMPLE_COUNT;
            sample->valid[i] = true;
        }
    }
    return first_error;
}
