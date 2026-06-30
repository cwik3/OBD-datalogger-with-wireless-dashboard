idf_component_register(SRCS "main.cpp" "vehicle_data.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_server nvs_flash esp_driver_twai)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)

target_add_binary_data(${COMPONENT_LIB} "../data/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/style.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/script.js" TEXT)