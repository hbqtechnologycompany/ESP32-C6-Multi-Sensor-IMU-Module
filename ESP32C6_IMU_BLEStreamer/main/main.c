#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "ble_stream.h"
#include "imu_ble.h"
#include "led_status.h"

static const char *TAG = "BLE_MAIN";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting BLE IMU streamer");

    ESP_ERROR_CHECK(led_status_init());

    // Init BLE stack
    ESP_ERROR_CHECK(ble_stream_init());

    // Init IMU(s) with default priorities and safe BLE-friendly ODR
    imu_ble_config_t cfg = {
        .enable_iis2mdc = true,
        .enable_iis3dwb = true,
        .enable_icm45686 = true,
        .enable_scl3300 = true,
        .iis3dwb_odr_hz = 800,
        .icm45686_odr_hz = 400,
        .packet_interval_ms = 20    // ~50 Hz notify bursts (min: 10ms for BLE stability)
    };
    ESP_ERROR_CHECK(imu_ble_init(&cfg));

    // Start advertising + GATT service
    ESP_ERROR_CHECK(ble_stream_start());

    // Nothing else to do; streaming handled in callbacks/tasks
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
