cmake_minimum_required(VERSION 3.16)
idf_component_register(
    SRCS
        "src/SynaptaNode.cpp"
        "src/SynaptaDevice.cpp"
        "src/NodeConfig.cpp"
    INCLUDE_DIRS "src"
    REQUIRES
        mqtt
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        driver
        esp_timer
        freertos
        log
)
