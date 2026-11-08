idf_component_register(
    SRCS "main.cpp" "web_server.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        zerotier_node
        esp_http_server
        app_update
        esp_driver_tsens
        esp_timer
        lwip
)
