idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_server nvs_flash)

target_add_binary_data(${COMPONENT_LIB} "../data/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/style.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/script.js" TEXT)