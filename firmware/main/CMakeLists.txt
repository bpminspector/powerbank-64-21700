idf_component_register(
    SRCS "app_main.c" "ina226.c" "radio.c" "telemetry.c" "display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_event esp_lcd esp_lcd_gc9107 esp_netif esp_timer esp_wifi lwip nvs_flash
)
