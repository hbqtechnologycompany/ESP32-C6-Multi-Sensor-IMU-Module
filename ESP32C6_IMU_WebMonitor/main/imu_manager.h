#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Sensor data structure
typedef struct {
    uint64_t timestamp_us;
    
    // IIS2MDC - Magnetometer
    struct {
        float x_mg;
        float y_mg;
        float z_mg;
        float temperature_c;
        bool valid;
    } magnetometer;
    
    // IIS3DWB - Accelerometer
    struct {
        float x_g;
        float y_g;
        float z_g;
        bool valid;
    } accelerometer;
    
    // ICM45686 - IMU 6-axis
    struct {
        float accel_x_g;
        float accel_y_g;
        float accel_z_g;
        float gyro_x_dps;
        float gyro_y_dps;
        float gyro_z_dps;
        float temperature_c;
        bool valid;
    } imu_6axis;
    
    // SCL3300 - Inclinometer
    struct {
        float angle_x_deg;
        float angle_y_deg;
        float angle_z_deg;
        float accel_x_g;
        float accel_y_g;
        float accel_z_g;
        float temperature_c;
        bool valid;
    } inclinometer;
    
} imu_data_t;

// IMU Manager API
esp_err_t imu_manager_init(void);
esp_err_t imu_manager_read_all(imu_data_t *data);
esp_err_t imu_manager_read_magnetometer(imu_data_t *data);
esp_err_t imu_manager_read_accelerometer(imu_data_t *data);
esp_err_t imu_manager_read_imu_6axis(imu_data_t *data);
esp_err_t imu_manager_read_inclinometer(imu_data_t *data);
esp_err_t imu_manager_deinit(void);

// Configuration functions
esp_err_t imu_manager_set_sampling_rate(uint32_t rate_hz);
esp_err_t imu_manager_set_fifo_watermark(uint16_t watermark);
esp_err_t imu_manager_enable_sensor(uint8_t sensor_id, bool enable);
uint32_t imu_manager_get_sampling_rate(void);
uint16_t imu_manager_get_fifo_watermark(void);
uint8_t imu_manager_get_enabled_sensors(void);

// Sensor IDs
#define SENSOR_MAGNETOMETER  0x01
#define SENSOR_ACCELEROMETER 0x02
#define SENSOR_IMU_6AXIS     0x04
#define SENSOR_INCLINOMETER  0x08

#endif // IMU_MANAGER_H
