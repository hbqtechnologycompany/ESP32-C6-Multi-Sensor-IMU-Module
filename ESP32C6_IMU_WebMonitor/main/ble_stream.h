#ifndef BLE_STREAM_H
#define BLE_STREAM_H

#include "esp_err.h"

// GATT parameters
#define BLE_STREAM_SERVICE_UUID        0x1815  // example UUID (custom in production)
#define BLE_STREAM_CHAR_DATA_UUID      0x2A58  // placeholder
#define BLE_STREAM_DEVICE_NAME         "IMU-BLE"

esp_err_t ble_stream_init(void);
esp_err_t ble_stream_start(void);
esp_err_t ble_stream_notify(const uint8_t *data, uint16_t len);

#endif // BLE_STREAM_H
