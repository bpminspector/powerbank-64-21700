find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PB_WIFI_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/../../.env")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PB_WIFI_ENV_FILE}")
set(PB_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/../scripts/generate_wifi_config.py"
            --env "${PB_WIFI_ENV_FILE}"
            --output "${PB_GENERATED_INCLUDE_DIR}/pb_wifi_config.h"
    RESULT_VARIABLE PB_WIFI_CONFIG_RESULT
    OUTPUT_QUIET
    ERROR_VARIABLE PB_WIFI_CONFIG_ERROR
)
if(NOT PB_WIFI_CONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not generate local Wi-Fi configuration: ${PB_WIFI_CONFIG_ERROR}")
endif()

idf_component_register(
    SRCS "app_main.c" "ina226.c" "oled_display.c" "temp_sensors.c"
         "dht_sensor.c" "touch.c" "remote_logs.c"
    INCLUDE_DIRS "." "${PB_GENERATED_INCLUDE_DIR}"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_timer nvs_flash esp_wifi esp_event
             esp_netif esp_http_client esp_rom
    PRIV_REQUIRES esp_adc dht
)