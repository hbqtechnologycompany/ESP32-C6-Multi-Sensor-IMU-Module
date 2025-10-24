#include "iis2mdc.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "IIS2MDC";

static esp_err_t iis2mdc_write_reg(iis2mdc_handle_t *sensor, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    return i2c_master_transmit(sensor->dev_handle, buf, 2, -1);
}

static esp_err_t iis2mdc_read_reg(iis2mdc_handle_t *sensor, uint8_t reg, uint8_t *data, size_t len) {
    esp_err_t ret = i2c_master_transmit(sensor->dev_handle, &reg, 1, -1);
    if (ret != ESP_OK) return ret;
    return i2c_master_receive(sensor->dev_handle, data, len, -1);
}

esp_err_t iis2mdc_init(iis2mdc_handle_t *sensor, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed_hz) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &sensor->bus_handle), TAG, "Failed to create I2C bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IIS2MDC_I2C_ADDR,
        .scl_speed_hz = clk_speed_hz
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(sensor->bus_handle, &dev_cfg, &sensor->dev_handle), TAG, "Failed to add I2C device");

    // Enable temperature compensation, continuous mode, ODR = 100Hz (example)
    return iis2mdc_config(sensor, 0x8C, 0x00, 0x10);
}

esp_err_t iis2mdc_read_who_am_i(iis2mdc_handle_t *sensor, uint8_t *id) {
    return iis2mdc_read_reg(sensor, IIS2MDC_REG_WHO_AM_I, id, 1);
}

esp_err_t iis2mdc_config(iis2mdc_handle_t *sensor, uint8_t cfg_a, uint8_t cfg_b, uint8_t cfg_c) {
    ESP_RETURN_ON_ERROR(iis2mdc_write_reg(sensor, IIS2MDC_REG_CFG_REG_A, cfg_a), TAG, "Write CFG_REG_A failed");
    ESP_RETURN_ON_ERROR(iis2mdc_write_reg(sensor, IIS2MDC_REG_CFG_REG_B, cfg_b), TAG, "Write CFG_REG_B failed");
    ESP_RETURN_ON_ERROR(iis2mdc_write_reg(sensor, IIS2MDC_REG_CFG_REG_C, cfg_c), TAG, "Write CFG_REG_C failed");
    return ESP_OK;
}

esp_err_t iis2mdc_read_magnetic_raw(iis2mdc_handle_t *sensor, iis2mdc_raw_magnetometer_t *mag) {
    uint8_t buf[6];
    ESP_RETURN_ON_ERROR(iis2mdc_read_reg(sensor, IIS2MDC_REG_OUTX_L, buf, 6), TAG, "Failed to read mag data");

    mag->x = (int16_t)((buf[1] << 8) | buf[0]);
    mag->y = (int16_t)((buf[3] << 8) | buf[2]);
    mag->z = (int16_t)((buf[5] << 8) | buf[4]);
    return ESP_OK;
}

esp_err_t iis2mdc_convert_magnetic_raw_to_mg(iis2mdc_raw_magnetometer_t *raw, float *x_mg, float *y_mg, float *z_mg) {

    // Conversion factor for IIS2MDC is 1.5 mG/LSB
    const float conversion_factor = 1.5f;
    *x_mg = (float)(raw->x) * conversion_factor;
    *y_mg = (float)(raw->y) * conversion_factor;   
    *z_mg = (float)(raw->z) * conversion_factor;

    return ESP_OK;
}

esp_err_t iis2mdc_read_temperature_raw(iis2mdc_handle_t *sensor, int16_t *temp) {
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(iis2mdc_read_reg(sensor, IIS2MDC_REG_TEMP_OUT_L, buf, 2), TAG, "Failed to read temperature");
    *temp = (int16_t)((buf[1] << 8) | buf[0]);
    return ESP_OK;
}

esp_err_t iis2mdc_convert_temperature_raw_to_celsius(int16_t raw_temp, float *temp_celsius) {
    // Conversion formula: Temp (C) = (Raw Temp / 8) + 25
    *temp_celsius = ((float)raw_temp / 8.0f) + 25.0f;
    return ESP_OK;
}