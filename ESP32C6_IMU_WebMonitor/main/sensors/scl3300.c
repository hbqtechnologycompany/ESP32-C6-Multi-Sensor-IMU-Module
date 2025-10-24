#include "scl3300.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <esp_log.h>
#include "esp_check.h" 
#include <inttypes.h>

static const char *TAG = "scl3300.c";

// --- CRC calculation from datasheet ---
// --- CRC8 helper (the same as datasheet) ---
static uint8_t scl3300_crc8(uint8_t bitValue, uint8_t crc)
{
    uint8_t temp = (uint8_t)(crc & 0x80);
    if (bitValue) {
        temp ^= 0x80;
    }
    crc <<= 1;
    if (temp) {
        crc ^= 0x1D;
    }
    return crc;
}

// --- Calculate CRC for 24 MSBs (datasheet version) ---
static uint8_t scl3300_calculate_crc(uint32_t data)
{
    uint8_t crc = 0xFF;

    // Take bits [31:8], skip [7:0] (CRC field itself)
    for (int bitIndex = 31; bitIndex > 7; bitIndex--) {
        uint8_t bit = (data >> bitIndex) & 0x01;
        crc = scl3300_crc8(bit, crc);
    }

    crc = (uint8_t)~crc;  // invert result
    return crc;
}
// --- SPI transfer (32-bit command) ---
static esp_err_t scl3300_transfer(scl3300_t *dev, uint32_t cmd, uint32_t *resp)
{
    uint32_t tx = __builtin_bswap32(cmd);  // đảo byte order: MSB first
    uint32_t rx = 0;

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = &tx,
        .rx_buffer = &rx,
    };

    esp_err_t ret = spi_device_transmit(dev->spi, &t);
    if (ret != ESP_OK) return ret;

    uint32_t val = __builtin_bswap32(rx);  // đảo lại thành host order
    if (resp) *resp = val;

    dev->last_cmd  = (val >> 24) & 0xFF;
    dev->last_data = (val >> 8)  & 0xFFFF;
    dev->last_crc  = val & 0xFF;

    uint8_t calc_crc = scl3300_calculate_crc(val);
    dev->crcerr = (dev->last_crc != calc_crc);
    dev->statuserr= ((dev->last_cmd & 0x03) != 0x03);

    return ESP_OK;
}

esp_err_t scl3300_read_reg(scl3300_t *dev, uint32_t cmd, int16_t *out)
{
    uint32_t resp;

    // Gửi lệnh
    ESP_RETURN_ON_ERROR(scl3300_transfer(dev, cmd, &resp), TAG, "transfer failed");

    // Dummy read để lấy kết quả
    ESP_RETURN_ON_ERROR(scl3300_transfer(dev, SCL3300_NOP, &resp), TAG, "dummy transfer failed");

    if (dev->crcerr) {
        ESP_LOGE(TAG, "CRC error on reg 0x%08" PRIX32 , cmd);
        return ESP_FAIL;
    }
    if (dev->statuserr) {
        ESP_LOGE(TAG, "Status error on reg 0x%08" PRIX32, cmd);
        return ESP_FAIL;
    }

    *out = (int16_t)dev->last_data;
    return ESP_OK;
}


esp_err_t scl3300_init(spi_host_device_t host, gpio_num_t cs_pin, scl3300_t *dev)
{
    memset(dev, 0, sizeof(*dev));
    dev->cs_pin = cs_pin;
    dev->mode   = 1;    // Mode 1 theo datasheet
    dev->fast_read = false;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000, // 4 MHz
        .mode           = 0,
        .spics_io_num   = cs_pin,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(host, &devcfg, &dev->spi));

    uint32_t resp, dummy;

    ESP_LOGI(TAG, "Waiting for power-up (10 ms)...");
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- Software reset ---
    ESP_LOGI(TAG, "Sending SWRESET...");
    scl3300_transfer(dev, SWreset, &resp);
    vTaskDelay(pdMS_TO_TICKS(2));
    scl3300_transfer(dev, SCL3300_NOP, &dummy);  // flush pipeline

    // --- Switch to bank 0 ---
    ESP_LOGI(TAG, "Switching to bank 0...");
    scl3300_transfer(dev, SwtchBnk0, &resp);
    scl3300_transfer(dev, SCL3300_NOP, &dummy);  // flush pipeline
    // ESP_LOGI(TAG, "Bank0 resp=0x%08X (data=0x%04X)", resp, dev->last_data);
    ESP_LOGI(TAG, "SwtchBnk0 resp=0x%08" PRIX32 ", data=0x%04X",
         resp, dev->last_data);

    // --- Select operating mode ---
    ESP_LOGI(TAG, "Setting mode %d...", dev->mode);
    scl3300_set_mode(dev, dev->mode);
    vTaskDelay(pdMS_TO_TICKS(50));
    scl3300_transfer(dev, SCL3300_NOP, &dummy);  // flush
    ESP_LOGI(TAG, "Mode set, data=0x%04X", dev->last_data);

    // --- Enable angle outputs ---
    ESP_LOGI(TAG, "Enabling angle outputs...");
    scl3300_transfer(dev, EnaAngOut, &resp);
    vTaskDelay(pdMS_TO_TICKS(50));
    scl3300_transfer(dev, SCL3300_NOP, &dummy);  // flush
    ESP_LOGI(TAG, "EnaAngOut done, data=0x%04X", dev->last_data);

    // --- Read WHOAMI ---
    ESP_LOGI(TAG, "Reading WHOAMI...");
    scl3300_transfer(dev, RdWHOAMI, &resp);   // send WHOAMI cmd
    scl3300_transfer(dev, SCL3300_NOP, &resp);        // next read is result

    ESP_LOGI(TAG, "WHOAMI data=0x%04X, crc_err=%d, status_err=%d",
             dev->last_data, dev->crcerr, dev->statuserr);

    if (dev->last_data != 0x00C1 || dev->crcerr || dev->statuserr) {
        ESP_LOGE(TAG, "SCL3300 init FAIL! Expected WHOAMI=0x00C1, got=0x%04X",
                 dev->last_data);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SCL3300 initialized OK, WHOAMI=0x%04X", dev->last_data);
    return ESP_OK;
}

esp_err_t scl3300_set_mode(scl3300_t *dev, uint8_t mode) {
    if (mode < 1 || mode > 4) return ESP_ERR_INVALID_ARG;
    dev->mode = mode;
    uint32_t resp;
    uint32_t cmd[5] = {0, ChgMode1, ChgMode2, ChgMode3, ChgMode4};
    return scl3300_transfer(dev, cmd[mode], &resp);
}

bool scl3300_is_connected(scl3300_t *dev) {
    uint32_t resp;
    scl3300_transfer(dev, RdWHOAMI, &resp);
    return (dev->last_data == 0x00C1 && !dev->crcerr && !dev->statuserr);
}

esp_err_t scl3300_available(scl3300_t *dev) {
    if (scl3300_read_reg(dev, RdAccX, &dev->data.AccX) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdAccY, &dev->data.AccY) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdAccZ, &dev->data.AccZ) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdTemp, &dev->data.TEMP) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdAngX, &dev->data.AngX) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdAngY, &dev->data.AngY) != ESP_OK) return ESP_FAIL;
    if (scl3300_read_reg(dev, RdAngZ, &dev->data.AngZ) != ESP_OK) return ESP_FAIL;

    return ESP_OK;
}

// === Error registers ===
uint16_t scl3300_get_errflag1(scl3300_t *dev) { uint32_t r; scl3300_transfer(dev, RdErrFlg1, &r); return dev->last_data; }
uint16_t scl3300_get_errflag2(scl3300_t *dev) { uint32_t r; scl3300_transfer(dev, RdErrFlg2, &r); return dev->last_data; }

uint32_t scl3300_get_serial_number(scl3300_t *dev) {
    uint32_t r; uint32_t serial = 0;
    scl3300_transfer(dev, SwtchBnk1, &r);
    scl3300_transfer(dev, RdSer1, &r);
    uint16_t low = dev->last_data;
    scl3300_transfer(dev, RdSer2, &r);
    uint16_t high = dev->last_data;
    scl3300_transfer(dev, SwtchBnk0, &r);
    serial = ((uint32_t)high << 16) | low;
    return serial;
}

// === Power management ===
uint16_t scl3300_powerdown(scl3300_t *dev) { uint32_t r; scl3300_transfer(dev, SetPwrDwn, &r); return dev->last_data; }
uint16_t scl3300_wakeup(scl3300_t *dev)    { uint32_t r; scl3300_transfer(dev, WakeUp, &r); return dev->last_data; }
uint16_t scl3300_reset(scl3300_t *dev)     { uint32_t r; scl3300_transfer(dev, SWreset, &r); vTaskDelay(pdMS_TO_TICKS(2)); return dev->last_data; }

// === Conversion helpers ===
static double scl3300_angle(int16_t raw) {
    return (raw / 16384.0) * 90.0;
}
static double scl3300_accel(scl3300_t *dev, int16_t raw) {
    switch (dev->mode) {
        case 1: return raw / 6000.0;
        case 2: return raw / 3000.0;
        case 3: return raw / 12000.0;
        case 4: return raw / 12000.0;
        default: return raw / 12000.0;
    }
}

double scl3300_get_angle_x(scl3300_t *dev) { return scl3300_angle(dev->data.AngX); }
double scl3300_get_angle_y(scl3300_t *dev) { return scl3300_angle(dev->data.AngY); }
double scl3300_get_angle_z(scl3300_t *dev) { return scl3300_angle(dev->data.AngZ); }

double scl3300_get_accel_x(scl3300_t *dev) { return scl3300_accel(dev, dev->data.AccX); }
double scl3300_get_accel_y(scl3300_t *dev) { return scl3300_accel(dev, dev->data.AccY); }
double scl3300_get_accel_z(scl3300_t *dev) { return scl3300_accel(dev, dev->data.AccZ); }

double scl3300_get_temp_c(scl3300_t *dev) {
    return -273.0 + (dev->data.TEMP / 18.9);
}
double scl3300_get_temp_f(scl3300_t *dev) {
    return scl3300_get_temp_c(dev) * 9.0/5.0 + 32.0;
}
