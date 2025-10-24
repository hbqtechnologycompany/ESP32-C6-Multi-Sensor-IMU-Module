#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "mdns.h"

#include "web_server.h"
#include "imu_manager.h"
#include "data_buffer.h"
#include "led_status.h"
#include "ble_stream.h"
#include "imu_ble.h"
#include "udp.h"

static const char *TAG = "MAIN";

// WiFi credentials - change these for your network
#define WIFI_SSID                   "Titan"
#define WIFI_PASS                   "stm32f103rd"
#define WIFI_MAXIMUM_RETRY          5

// mDNS configuration
#define MDNS_HOSTNAME               "hbq-imu"
#define MDNS_INSTANCE               "HBQ IMU Web Monitor"

// Task priorities
#define IMU_TASK_PRIORITY           5
#define WEB_SERVER_TASK_PRIORITY    4
#define DATA_PROCESSOR_PRIORITY     3

// Task stack sizes
#define IMU_TASK_STACK_SIZE             8192
#define WEB_SERVER_TASK_STACK_SIZE      4096
#define UDP_BROADCAST_TASK_STACK_SIZE   2048

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        led_status_set_state(LED_STATUS_NO_WIFI);  // LED ON - đang kết nối
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        led_status_set_state(LED_STATUS_NO_WIFI);  // LED ON - mất kết nối
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void mdns_init_service(void)
{
    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_LOGI(TAG, "mDNS hostname set to: %s.local", MDNS_HOSTNAME);
    
    ESP_ERROR_CHECK(mdns_instance_name_set(MDNS_INSTANCE));
    
    // Add HTTP service
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS service added: _http._tcp on port 80");
    
    // Set LED to blink state after mDNS is configured
    led_status_set_state(LED_STATUS_WIFI_CONNECTED);
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_SSID, WIFI_PASS);
        
        // Initialize mDNS after WiFi connected
        mdns_init_service();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 WIFI_SSID, WIFI_PASS);
        led_status_set_state(LED_STATUS_NO_WIFI);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

// IMU data collection task
static void imu_task(void *pvParameters)
{
    ESP_LOGI(TAG, "IMU task started");
    
    // Initialize all sensors
    if (imu_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IMU manager");
        vTaskDelete(NULL);
        return;
    }
    
    imu_data_t sensor_data;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20); // 100Hz sampling
    uint32_t read_count = 0;
    
    while (1) {
        // Collect data from all sensors
        if (imu_manager_read_all(&sensor_data) == ESP_OK) {
            // Add to circular buffer
            data_buffer_add(&sensor_data);
            read_count++;
            
            // printf("time(us): %lld, ii3s: x:%f - y:%f - z:%f - valid:%d \r\n", 
            //     sensor_data.timestamp_us ,sensor_data.accelerometer.x_g, sensor_data.accelerometer.y_g, sensor_data.accelerometer.z_g, sensor_data.accelerometer.valid);
            // Log every 100 reads
            if (read_count % 100 == 0) {
                ESP_LOGI(TAG, "IMU read count: %lu, buffer adds: %lu, mag_valid=%d", 
                         read_count, read_count,
                         sensor_data.magnetometer.valid);
            }
        } else {
            ESP_LOGW(TAG, "Failed to read IMU data");
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        // Maintain precise timing
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}
// Web server task
static void web_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Web server task started");
    
    // Initialize SPIFFS for web files
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        vTaskDelete(NULL);
        return;
    }
    
    // Start web server
    if (web_server_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Web server started successfully");
    
    // Keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C6 IMU Web Monitor Starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize LED status indicator
    ESP_ERROR_CHECK(led_status_init(18));  // GPIO 18
    led_status_set_state(LED_STATUS_NO_WIFI);  // Start with NO_WIFI
    
    // Initialize data buffer
    data_buffer_init();

    // Initialize BLE streaming before bringing up Wi-Fi
    imu_ble_config_t ble_cfg = {
        .enable_iis2mdc = true,
        .enable_iis3dwb = true,
        .enable_icm45686 = true,
        .enable_scl3300 = true,
        .iis3dwb_odr_hz = 800,
        .icm45686_odr_hz = 400,
        .packet_interval_ms = 20
    };
    ESP_ERROR_CHECK(ble_stream_init());
    ESP_ERROR_CHECK(imu_ble_init(&ble_cfg));
    ESP_ERROR_CHECK(ble_stream_start());

    // Connect to WiFi
    wifi_init_sta();
    
    // Create tasks
    // ESP32-C6 is single-core, use core 0 or tskNO_AFFINITY
    xTaskCreate(imu_task, "imu_task", IMU_TASK_STACK_SIZE, 
                           NULL, IMU_TASK_PRIORITY, NULL);
    
    xTaskCreate(web_server_task, "web_server", 
                           WEB_SERVER_TASK_STACK_SIZE, NULL, 
                           WEB_SERVER_TASK_PRIORITY, NULL);

    xTaskCreate(udp_broadcast_task, "udp_broadcast_task", UDP_BROADCAST_TASK_STACK_SIZE, 
        NULL, 5, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
    
    // Main loop - just monitor system health
    while (1) {
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000)); // Log every 30 seconds
    }
}
