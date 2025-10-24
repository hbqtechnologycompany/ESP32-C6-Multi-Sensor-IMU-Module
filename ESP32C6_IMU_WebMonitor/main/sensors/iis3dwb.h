#ifndef _IIS3DWB_H_
#define _IIS3DWB_H_

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

// WHO_AM_I register
#define IIS3DWB_WHO_AM_I_REG    0x0F
#define IIS3DWB_WHO_AM_I_VAL    0x7B

// Control registers
#define IIS3DWB_CTRL1_XL        0x10
#define IIS3DWB_CTRL3_C         0x12
#define IIS3DWB_CTRL4_C         0x13
#define IIS3DWB_CTRL8_XL        0x17

// FIFO registers
#define IIS3DWB_FIFO_CTRL1      0x07
#define IIS3DWB_FIFO_CTRL2      0x08
#define IIS3DWB_FIFO_CTRL3      0x09
#define IIS3DWB_FIFO_CTRL4      0x0A
#define IIS3DWB_FIFO_STATUS1    0x3A
#define IIS3DWB_FIFO_STATUS2    0x3B
#define IIS3DWB_FIFO_DATA_OUT_X_L 0x79
#define IIS3DWB_FIFO_DATA_OUT_TAG 0x78

// Data registers
#define IIS3DWB_OUTX_L_A        0x28
#define IIS3DWB_OUTY_L_A        0x2A
#define IIS3DWB_OUTZ_L_A        0x2C

typedef struct {
    spi_device_handle_t spi;
} iis3dwb_handle_t;

typedef enum {
    IIS3DWB_FS_2G  = 0x00,
    IIS3DWB_FS_16G = 0x04,
    IIS3DWB_FS_4G  = 0x08,
    IIS3DWB_FS_8G  = 0x0C
} iis3dwb_fs_t;

typedef enum {
    IIS3DWB_ODR_OFF    = 0x00,
    IIS3DWB_ODR_26K7HZ = 0xA0
} iis3dwb_odr_t;

esp_err_t iis3dwb_init_spi(iis3dwb_handle_t *dev, spi_host_device_t host, gpio_num_t cs_pin);
esp_err_t iis3dwb_read_reg(iis3dwb_handle_t *dev, uint8_t reg, uint8_t *data, size_t len);
esp_err_t iis3dwb_write_reg(iis3dwb_handle_t *dev, uint8_t reg, const uint8_t *data, size_t len);

esp_err_t iis3dwb_device_init(iis3dwb_handle_t *dev);
esp_err_t iis3dwb_configure(iis3dwb_handle_t *dev, iis3dwb_fs_t fs, iis3dwb_odr_t odr);
esp_err_t iis3dwb_configure_filter(iis3dwb_handle_t *dev, uint8_t lpf2_en, uint8_t fds, uint8_t hpcf);

esp_err_t iis3dwb_read_accel(iis3dwb_handle_t *dev, float *ax, float *ay, float *az);
esp_err_t iis3dwb_fifo_config(iis3dwb_handle_t *dev, uint16_t watermark, uint8_t mode);
// Sửa đổi prototype
esp_err_t iis3dwb_fifo_read_burst(iis3dwb_handle_t *dev, uint8_t *data, size_t samples);

float iis3dwb_g_to_ms2(float g_val);
void iis3dwb_convert_raw_to_g(const uint8_t *fifo_buf, size_t samples, float *ax, float *ay, float *az);
void iis3dwb_velocity_integrate(float *vx, float *vy, float *vz,
                                const float *ax, const float *ay, const float *az,
                                size_t samples, float dt);

#endif