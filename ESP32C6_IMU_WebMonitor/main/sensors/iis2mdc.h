#ifndef IIS2MDC_H
#define IIS2MDC_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#define IIS2MDC_I2C_ADDR         0x1E  // 0011110b
#define IIS2MDC_WHO_AM_I_VAL     0x40

// Register map
#define IIS2MDC_REG_WHO_AM_I     0x4F
#define IIS2MDC_REG_CFG_REG_A    0x60
#define IIS2MDC_REG_CFG_REG_B    0x61
#define IIS2MDC_REG_CFG_REG_C    0x62
#define IIS2MDC_REG_STATUS       0x67
#define IIS2MDC_REG_OUTX_L       0x68
#define IIS2MDC_REG_OUTX_H       0x69
#define IIS2MDC_REG_OUTY_L       0x6A
#define IIS2MDC_REG_OUTY_H       0x6B
#define IIS2MDC_REG_OUTZ_L       0x6C
#define IIS2MDC_REG_OUTZ_H       0x6D
#define IIS2MDC_REG_TEMP_OUT_L   0x6E
#define IIS2MDC_REG_TEMP_OUT_H   0x6F

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} iis2mdc_raw_magnetometer_t;

typedef struct {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
} iis2mdc_handle_t;

// API
esp_err_t iis2mdc_init(iis2mdc_handle_t *sensor, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed_hz);
esp_err_t iis2mdc_read_who_am_i(iis2mdc_handle_t *sensor, uint8_t *id);
esp_err_t iis2mdc_config(iis2mdc_handle_t *sensor, uint8_t cfg_a, uint8_t cfg_b, uint8_t cfg_c);
esp_err_t iis2mdc_read_magnetic_raw(iis2mdc_handle_t *sensor, iis2mdc_raw_magnetometer_t *mag);
esp_err_t iis2mdc_convert_magnetic_raw_to_mg(iis2mdc_raw_magnetometer_t *raw, float *x_mg, float *y_mg, float *z_mg);
esp_err_t iis2mdc_read_temperature_raw(iis2mdc_handle_t *sensor, int16_t *temp);
esp_err_t iis2mdc_convert_temperature_raw_to_celsius(int16_t raw_temp, float *temp_celsius);

#endif // IIS2MDC_H
