/**
 * @file    iis3dwb_hal.h
 * @brief   This file contains the HAL layer for the IIS3DWB sensor
 */

#ifndef IIS3DWB_HAL_H
#define IIS3DWB_HAL_H

#include "iis3dwb_reg.h"
#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
// read data using FIFO if enabled (1: enable, 0: disable)
#define FIFO_MODE              0           

// ===== SPI CONFIGURATION =====
#define IIS3DWB_SPI_FREQ_HZ     10000000    // 10 MHz, theo datasheet IIS3DWB
#define IIS3DWB_SPI_MODE        0           // CPOL=0, CPHA=0
#define FIFO_WATERMARK          256         // Mức ngưỡng FIFO

// ===== PRIVATE MACROS =====
#define BOOT_TIME               10          //ms
#define WAIT_TIME               100         //ms

#define MIN_ST_LIMIT_mg         800.0f
#define MAX_ST_LIMIT_mg         3200.0f
#define ST_PASS                 1U
#define ST_FAIL                 0U

// ===== IIS3DWB HAL DATA STRUCTURE =====
typedef struct {
    float x_mg;                 // Vibration in X axis [mg]
    float y_mg;                 // Vibration in Y axis [mg]
    float z_mg;                 // Vibration in Z axis [mg]
    float temperature_degC;     // Temperature [°C]
#if FIFO_MODE
    int32_t timestamp_ms;        // Timestamp [ms]
#endif
} iis3dwb_hal_data_t;

// ===== IIS3DWB HAL CONFIGURATION STRUCTURE =====
typedef struct {
    uint8_t bdu;                                // Block data update
    iis3dwb_odr_xl_t odr;                    // Output data rate
    iis3dwb_fs_xl_t fs;                      // Full scale
    iis3dwb_filt_xl_en_t filter;            // Low pass filter 1 enable
#if FIFO_MODE
    iis3dwb_fifo_mode_t fifo_mode;              // FIFO mode
    uint16_t fifo_watermark;                    // FIFO watermark level
    iis3dwb_bdr_xl_t fifo_xl_batch;             // Accelerometer data batching
    iis3dwb_odr_t_batch_t fifo_temp_batch;      // Temperature data
    iis3dwb_fifo_timestamp_batch_t fifo_timestamp_batch; // Timestamp batching
    uint8_t fifo_timestamp_en;                   // Timestamp enable
#endif
} iis3dwb_hal_cfg_t;


// ===== PUBLIC FUNCTION PROTOTYPES =====
esp_err_t iis3dwb_hal_init(stmdev_ctx_t *dev_ctx, spi_host_device_t host, gpio_num_t cs_pin);
esp_err_t iis3dwb_hal_deinit(stmdev_ctx_t *dev_ctx);
esp_err_t iis3dwb_hal_configure(stmdev_ctx_t *dev_ctx, iis3dwb_hal_cfg_t *cfg);
esp_err_t iis3dwb_hal_read_data(stmdev_ctx_t *dev_ctx, iis3dwb_hal_data_t *data);
esp_err_t iis3dwb_hal_self_test(stmdev_ctx_t *dev_ctx, uint8_t *result);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* IIS3DWB_HAL_H */