idf_component_register(
    SRCS
    "main.c"
    "mant1s_ethernet.c"
    "init_mqtt.c"
    "configuration.c"
    INCLUDE_DIRS
    "."

    # With set(COMPONENTS main) in the top-level CMakeLists, main no longer
    # gets an implicit dependency on every component, so list them explicitly.
    # esp_eth is public because mant1s_ethernet.h includes esp_eth_driver.h.
    REQUIRES
    esp_eth
    PRIV_REQUIRES
    espressif__cjson
    esp_app_format
    esp_event
    esp_netif
    app_update
    esp_https_ota
    nvs_flash
    soc
)
