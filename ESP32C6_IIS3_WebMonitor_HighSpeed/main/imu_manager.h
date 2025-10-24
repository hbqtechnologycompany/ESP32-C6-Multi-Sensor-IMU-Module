#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IMU_MANAGER_FS_2G = 2,
    IMU_MANAGER_FS_4G = 4,
    IMU_MANAGER_FS_8G = 8,
    IMU_MANAGER_FS_16G = 16,
} imu_manager_full_scale_t;

typedef struct {
    uint64_t timestamp_us;
    struct {
        float x_g;
        float y_g;
        float z_g;
        float magnitude_g;
        bool valid;
    } accelerometer;
    struct {
        uint16_t fifo_level;
        uint16_t samples_read;
        float odr_hz;
        float batch_interval_us;
        float samples_per_second;
    } stats;
} imu_data_t;

#define IMU_MANAGER_MAX_SAMPLES 64

// IMU Manager API
esp_err_t imu_manager_init(void);
esp_err_t imu_manager_read_all(imu_data_t *data);
esp_err_t imu_manager_read_accelerometer(imu_data_t *data);
esp_err_t imu_manager_deinit(void);
float imu_manager_get_configured_odr(void);
uint16_t imu_manager_get_fifo_watermark(void);
imu_manager_full_scale_t imu_manager_get_full_scale(void);
uint8_t imu_manager_get_full_scale_g(void);
esp_err_t imu_manager_set_full_scale(imu_manager_full_scale_t scale);
uint16_t imu_manager_copy_recent_samples(float *x_g, float *y_g, float *z_g,
                                         uint16_t max_samples, uint64_t *timestamp_us,
                                         uint16_t *fifo_level, uint32_t *sequence_id);

#endif // IMU_MANAGER_H
