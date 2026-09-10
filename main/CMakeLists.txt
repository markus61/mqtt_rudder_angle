idf_component_register(
    SRCS
    "main.c"
    "mant1s_ethernet.c"
    "init_mqtt.c"
    "device_config.c"
    "device_utils.c"
    "features_config.c"
    "adc/elobau_angle_sensor_config.c"
    "adc/elobau_angle_sensor.c"
    INCLUDE_DIRS
    "."
    "adc"

    # With set(COMPONENTS main) in the top-level CMakeLists, main no longer
    # gets an implicit dependency on every component, so list them explicitly.
    # esp_eth is public because mant1s_ethernet.h includes esp_eth_driver.h,
    # and cjson because device_config.h includes cJSON.h.
    REQUIRES
    esp_eth
    espressif__cjson
    PRIV_REQUIRES
    esp_adc
    esp_app_format
    esp_event
    esp_netif
    app_update
    esp_https_ota
    nvs_flash
    soc
)
