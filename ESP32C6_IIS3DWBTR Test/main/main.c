#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iis3dwb.h"

#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  7
#define PIN_NUM_CLK   6
#define PIN_NUM_CS    19

static const char *TAG = "APP_MAIN";

// Tần số lấy mẫu và khoảng thời gian
#define ODR_HZ 26700.0f
#define DELTA_T_S (1.0f / ODR_HZ)
#define FIFO_WATERMARK 32

void app_main(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (FIFO_WATERMARK * 7) + 1, // Tăng kích thước truyền tối đa
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    iis3dwb_handle_t dev;
    ESP_ERROR_CHECK(iis3dwb_init_spi(&dev, SPI2_HOST, PIN_NUM_CS));
    ESP_ERROR_CHECK(iis3dwb_device_init(&dev));
    ESP_ERROR_CHECK(iis3dwb_configure(&dev, IIS3DWB_FS_2G, IIS3DWB_ODR_26K7HZ));
    
    // Cấu hình FIFO với watermark và chế độ
    uint8_t mode = 0x06; // Chế độ continuous
    uint16_t watermark = FIFO_WATERMARK;
    ESP_ERROR_CHECK(iis3dwb_fifo_config(&dev, watermark, mode));

    float vx = 0, vy = 0, vz = 0;
    
    uint8_t fifo_buf[FIFO_WATERMARK * 7];
    float ax[FIFO_WATERMARK], ay[FIFO_WATERMARK], az[FIFO_WATERMARK];

    int64_t last_log_time_ms = esp_timer_get_time() / 1000;

    while (1) {
        // Kiểm tra mức FIFO trước khi đọc
        uint8_t fifo_status[2];
        if (iis3dwb_read_reg(&dev, IIS3DWB_FIFO_STATUS1, fifo_status, 2) == ESP_OK) {
            uint16_t fifo_level = (fifo_status[1] & 0x0F) << 8 | fifo_status[0];
            
            if (fifo_level >= FIFO_WATERMARK) {
                if (iis3dwb_fifo_read_burst(&dev, fifo_buf, FIFO_WATERMARK) == ESP_OK) {
                    iis3dwb_convert_raw_to_g(fifo_buf, FIFO_WATERMARK, ax, ay, az);
                    iis3dwb_velocity_integrate(&vx, &vy, &vz, ax, ay, az, FIFO_WATERMARK, DELTA_T_S);
                } else {
                    ESP_LOGE(TAG, "Failed to read FIFO data");
                }
            }
        }
        
        // Cứ sau 2s, log giá trị ra
        int64_t current_time_ms = esp_timer_get_time() / 1000;
        if (current_time_ms - last_log_time_ms >= 2000) {
            ESP_LOGI(TAG, "Current Velocity [m/s]: X=%.3f, Y=%.3f, Z=%.3f", vx, vy, vz);
            ESP_LOGI(TAG, "-------------------------------");
            last_log_time_ms = current_time_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}