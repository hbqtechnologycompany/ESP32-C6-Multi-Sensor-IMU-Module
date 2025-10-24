#include "iis3dwb.h"
#include "esp_log.h"
#include <string.h>

#define TAG "IIS3DWB"

static esp_err_t spi_transfer(iis3dwb_handle_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    return spi_device_transmit(dev->spi, &t);
}

esp_err_t iis3dwb_init_spi(iis3dwb_handle_t *dev, spi_host_device_t host, gpio_num_t cs_pin) {
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 3,
        .spics_io_num = cs_pin,
        .queue_size = 1
    };
    return spi_bus_add_device(host, &devcfg, &dev->spi);
}

esp_err_t iis3dwb_read_reg(iis3dwb_handle_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    uint8_t tx[len + 1];
    uint8_t rx[len + 1];
    tx[0] = reg | 0x80;
    memset(&tx[1], 0x00, len);
    esp_err_t ret = spi_transfer(dev, tx, rx, len + 1);
    if (ret == ESP_OK) memcpy(data, &rx[1], len);
    return ret;
}

esp_err_t iis3dwb_write_reg(iis3dwb_handle_t *dev, uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = reg & 0x7F;
    memcpy(&buf[1], data, len);
    return spi_transfer(dev, buf, NULL, len + 1);
}

esp_err_t iis3dwb_device_init(iis3dwb_handle_t *dev) {
    uint8_t whoami;
    ESP_ERROR_CHECK(iis3dwb_read_reg(dev, IIS3DWB_WHO_AM_I_REG, &whoami, 1));
    if (whoami != IIS3DWB_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: 0x%02X", whoami);
        return ESP_FAIL;
    }
    uint8_t ctrl3 = 0x44; // IF_INC=1, BDU=1
    ESP_ERROR_CHECK(iis3dwb_write_reg(dev, IIS3DWB_CTRL3_C, &ctrl3, 1));
    return ESP_OK;
}

esp_err_t iis3dwb_configure(iis3dwb_handle_t *dev, iis3dwb_fs_t fs, iis3dwb_odr_t odr) {
    uint8_t ctrl1 = (uint8_t)fs | (uint8_t)odr | 0x05; // XL_EN=101
    return iis3dwb_write_reg(dev, IIS3DWB_CTRL1_XL, &ctrl1, 1);
}

esp_err_t iis3dwb_configure_filter(iis3dwb_handle_t *dev, uint8_t lpf2_en, uint8_t fds, uint8_t hpcf) {
    uint8_t ctrl8 = ((hpcf & 0x07) << 5) | ((fds & 0x01) << 3) | ((lpf2_en & 0x01) << 7);
    return iis3dwb_write_reg(dev, IIS3DWB_CTRL8_XL, &ctrl8, 1);
}

esp_err_t iis3dwb_read_accel(iis3dwb_handle_t *dev, float *ax, float *ay, float *az) {
    uint8_t buf[6];
    ESP_ERROR_CHECK(iis3dwb_read_reg(dev, IIS3DWB_OUTX_L_A, buf, 6));
    int16_t raw_x = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t raw_y = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t raw_z = (int16_t)(buf[5] << 8 | buf[4]);
    const float sensitivity = 0.061f; // mg/LSB @ ±2g
    *ax = raw_x * sensitivity / 1000.0f;
    *ay = raw_y * sensitivity / 1000.0f;
    *az = raw_z * sensitivity / 1000.0f;
    return ESP_OK;
}

esp_err_t iis3dwb_fifo_config(iis3dwb_handle_t *dev, uint16_t watermark, uint8_t mode) {
    uint8_t fifo_ctrl1 = watermark & 0xFF;
    uint8_t fifo_ctrl2 = (watermark >> 8) & 0x01;
    ESP_ERROR_CHECK(iis3dwb_write_reg(dev, IIS3DWB_FIFO_CTRL1, &fifo_ctrl1, 1));
    ESP_ERROR_CHECK(iis3dwb_write_reg(dev, IIS3DWB_FIFO_CTRL2, &fifo_ctrl2, 1));
    uint8_t fifo_ctrl4 = mode & 0x07;
    return iis3dwb_write_reg(dev, IIS3DWB_FIFO_CTRL4, &fifo_ctrl4, 1);
}

esp_err_t iis3dwb_fifo_read_burst(iis3dwb_handle_t *dev, uint8_t *data, size_t samples) {
    // Địa chỉ đọc FIFO dữ liệu burst là 0x78 (FIFO_DATA_OUT_TAG)
    size_t bytes_to_read = samples * 7;
    return iis3dwb_read_reg(dev, IIS3DWB_FIFO_DATA_OUT_TAG, data, bytes_to_read);
}

float iis3dwb_g_to_ms2(float g_val) {
    return g_val * 9.80665f;
}

void iis3dwb_convert_raw_to_g(const uint8_t *fifo_buf, size_t samples, float *ax, float *ay, float *az) {
    const float sensitivity = 0.061f / 1000.0f;
    for (size_t i = 0; i < samples; i++) {
        int16_t raw_x = (int16_t)(fifo_buf[i*7 + 2] << 8 | fifo_buf[i*7 + 1]);
        int16_t raw_y = (int16_t)(fifo_buf[i*7 + 4] << 8 | fifo_buf[i*7 + 3]);
        int16_t raw_z = (int16_t)(fifo_buf[i*7 + 6] << 8 | fifo_buf[i*7 + 5]);
        ax[i] = raw_x * sensitivity;
        ay[i] = raw_y * sensitivity;
        az[i] = raw_z * sensitivity;
    }
}

void iis3dwb_velocity_integrate(float *vx, float *vy, float *vz,
                                const float *ax, const float *ay, const float *az,
                                size_t samples, float dt) {
    for (size_t i = 0; i < samples; i++) {
        *vx += iis3dwb_g_to_ms2(ax[i]) * dt;
        *vy += iis3dwb_g_to_ms2(ay[i]) * dt;
        *vz += iis3dwb_g_to_ms2(az[i]) * dt;
    }
}