# components/wifi_provisioner/CMakeLists.txt

idf_component_register(SRCS "wifi_provisioner.cpp" "dns_server.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server lwip freertos log
                       PRIV_REQUIRES espressif__cjson)

# Here we instruct ESP-IDF directly to embed the web files.
target_add_binary_data(${COMPONENT_TARGET} "web/index_en.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "web/style.css"  TEXT)