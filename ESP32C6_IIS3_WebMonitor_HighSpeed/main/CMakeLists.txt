idf_component_register(SRCS "led_status.c" "main.c" 
                              "web_server.c" 
                              "imu_manager.c"
                              "data_buffer.c"
                              "sensors/iis3dwb_hal.c"
                              "sensors/iis3dwb_reg.c"
                              "udp.c"
                    INCLUDE_DIRS "." "sensors"
                    REQUIRES esp_http_server esp_wifi nvs_flash spiffs json driver esp_timer espressif__mdns
                    EMBED_FILES "${PROJECT_DIR}/data/index.html"
                                "${PROJECT_DIR}/data/style.css"
                                "${PROJECT_DIR}/data/app.js")
