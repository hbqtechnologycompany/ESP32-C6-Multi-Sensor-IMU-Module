#include "imu_ble.h"
#include "ble_stream.h"
#include "data_buffer.h"
#include "led_status.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <limits.h>
#include <string.h>

static const char *TAG = "IMU_BLE";
static const uint8_t FRAME_VERSION = 1;

static imu_ble_config_t s_cfg;
static TaskHandle_t s_producer_task = NULL;
static uint32_t s_frame_seq = 0;
static uint32_t s_last_error_log_ms = 0;
static uint64_t s_last_sent_timestamp = 0;
static bool s_connected = false;
static bool s_notifications_ready = false;

typedef struct __attribute__((packed)) {
    uint16_t frame_len;
    uint8_t  version;
    uint8_t  flags;
    uint16_t sensor_mask;
    uint32_t timestamp_us;
    uint32_t sequence;
} ble_frame_header_t;

enum {
    BLE_SENSOR_IIS3_ACCEL = 1 << 0,
    BLE_SENSOR_ICM_ACCEL  = 1 << 1,
    BLE_SENSOR_ICM_GYRO   = 1 << 2,
    BLE_SENSOR_ICM_TEMP   = 1 << 3,
    BLE_SENSOR_IIS2_MAG   = 1 << 4,
    BLE_SENSOR_IIS2_TEMP  = 1 << 5,
    BLE_SENSOR_SCL_ANGLE  = 1 << 6,
    BLE_SENSOR_SCL_ACCEL  = 1 << 7,
    BLE_SENSOR_SCL_TEMP   = 1 << 8,
};

static inline int16_t clamp_i16(int32_t v)
{
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return (int16_t)v;
}

static inline int16_t float_to_scaled_i16(float value, float scale)
{
    return clamp_i16((int32_t)lrintf(value * scale));
}

static bool append_u8(uint8_t *buf, size_t *offset, size_t max, uint8_t v)
{
    if (*offset + 1 > max) return false;
    buf[(*offset)++] = v;
    return true;
}

static bool append_i16(uint8_t *buf, size_t *offset, size_t max, int16_t v)
{
    if (*offset + sizeof(int16_t) > max) return false;
    memcpy(&buf[*offset], &v, sizeof(int16_t));
    *offset += sizeof(int16_t);
    return true;
}

static bool append_vec3(uint8_t *buf, size_t *offset, size_t max, uint8_t type, int16_t x, int16_t y, int16_t z)
{
    if (!append_u8(buf, offset, max, type)) return false;
    if (!append_u8(buf, offset, max, 6)) return false;
    if (!append_i16(buf, offset, max, x)) return false;
    if (!append_i16(buf, offset, max, y)) return false;
    if (!append_i16(buf, offset, max, z)) return false;
    return true;
}

static bool append_scalar(uint8_t *buf, size_t *offset, size_t max, uint8_t type, int16_t value)
{
    if (!append_u8(buf, offset, max, type)) return false;
    if (!append_u8(buf, offset, max, 2)) return false;
    if (!append_i16(buf, offset, max, value)) return false;
    return true;
}

static void log_error_throttled(const char *context, esp_err_t err)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (now_ms - s_last_error_log_ms > 1000U) {
        ESP_LOGW(TAG, "%s: %s", context, esp_err_to_name(err));
        s_last_error_log_ms = now_ms;
    }
}

static size_t build_frame(const imu_data_t *data, uint8_t *out, size_t max_len)
{
    if (!data || !out || max_len < sizeof(ble_frame_header_t)) {
        return 0;
    }

    size_t offset = sizeof(ble_frame_header_t);
    uint16_t mask = 0;

    if (data->accelerometer.valid && s_cfg.enable_iis3dwb) {
        int16_t ax = float_to_scaled_i16(data->accelerometer.x_g, 16384.0f); // 1g -> 16384
        int16_t ay = float_to_scaled_i16(data->accelerometer.y_g, 16384.0f);
        int16_t az = float_to_scaled_i16(data->accelerometer.z_g, 16384.0f);
        if (!append_vec3(out, &offset, max_len, 0x01, ax, ay, az)) return 0;
        mask |= BLE_SENSOR_IIS3_ACCEL;
    }

    if (data->imu_6axis.valid && s_cfg.enable_icm45686) {
        int16_t ax = float_to_scaled_i16(data->imu_6axis.accel_x_g, 16384.0f);
        int16_t ay = float_to_scaled_i16(data->imu_6axis.accel_y_g, 16384.0f);
        int16_t az = float_to_scaled_i16(data->imu_6axis.accel_z_g, 16384.0f);
        if (!append_vec3(out, &offset, max_len, 0x10, ax, ay, az)) return 0;
        mask |= BLE_SENSOR_ICM_ACCEL;

        int16_t gx = float_to_scaled_i16(data->imu_6axis.gyro_x_dps, 131.072f); // 1dps -> 131
        int16_t gy = float_to_scaled_i16(data->imu_6axis.gyro_y_dps, 131.072f);
        int16_t gz = float_to_scaled_i16(data->imu_6axis.gyro_z_dps, 131.072f);
        if (!append_vec3(out, &offset, max_len, 0x11, gx, gy, gz)) return 0;
        mask |= BLE_SENSOR_ICM_GYRO;

        int16_t temp = float_to_scaled_i16(data->imu_6axis.temperature_c, 100.0f);
        if (!append_scalar(out, &offset, max_len, 0x12, temp)) return 0;
        mask |= BLE_SENSOR_ICM_TEMP;
    }

    if (data->magnetometer.valid && s_cfg.enable_iis2mdc) {
        int16_t mx = float_to_scaled_i16(data->magnetometer.x_mg, 1.0f);  // already mg
        int16_t my = float_to_scaled_i16(data->magnetometer.y_mg, 1.0f);
        int16_t mz = float_to_scaled_i16(data->magnetometer.z_mg, 1.0f);
        if (!append_vec3(out, &offset, max_len, 0x20, mx, my, mz)) return 0;
        mask |= BLE_SENSOR_IIS2_MAG;

        int16_t temp = float_to_scaled_i16(data->magnetometer.temperature_c, 100.0f);
        if (!append_scalar(out, &offset, max_len, 0x21, temp)) return 0;
        mask |= BLE_SENSOR_IIS2_TEMP;
    }

    if (data->inclinometer.valid && s_cfg.enable_scl3300) {
        int16_t ang_x = float_to_scaled_i16(data->inclinometer.angle_x_deg, 100.0f);
        int16_t ang_y = float_to_scaled_i16(data->inclinometer.angle_y_deg, 100.0f);
        int16_t ang_z = float_to_scaled_i16(data->inclinometer.angle_z_deg, 100.0f);
        if (!append_vec3(out, &offset, max_len, 0x30, ang_x, ang_y, ang_z)) return 0;
        mask |= BLE_SENSOR_SCL_ANGLE;

        int16_t acc_x = float_to_scaled_i16(data->inclinometer.accel_x_g, 16384.0f);
        int16_t acc_y = float_to_scaled_i16(data->inclinometer.accel_y_g, 16384.0f);
        int16_t acc_z = float_to_scaled_i16(data->inclinometer.accel_z_g, 16384.0f);
        if (!append_vec3(out, &offset, max_len, 0x31, acc_x, acc_y, acc_z)) return 0;
        mask |= BLE_SENSOR_SCL_ACCEL;

        int16_t temp = float_to_scaled_i16(data->inclinometer.temperature_c, 100.0f);
        if (!append_scalar(out, &offset, max_len, 0x32, temp)) return 0;
        mask |= BLE_SENSOR_SCL_TEMP;
    }

    if (mask == 0) {
        return 0;
    }

    ble_frame_header_t header = {
        .frame_len = (uint16_t)offset,
        .version = FRAME_VERSION,
        .flags = 0,
        .sensor_mask = mask,
        .timestamp_us = (uint32_t)(data->timestamp_us & 0xFFFFFFFF),
        .sequence = s_frame_seq++,
    };
    memcpy(out, &header, sizeof(header));

    return offset;
}

static void producer_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(s_cfg.packet_interval_ms);
    TickType_t last = xTaskGetTickCount();

    ESP_LOGI(TAG, "Producer started (interval=%ums)", (unsigned)s_cfg.packet_interval_ms);

    imu_data_t sample;
    uint8_t frame[244];

    while (true) {
        if (!s_connected || !s_notifications_ready) {
            vTaskDelayUntil(&last, period);
            continue;
        }

        esp_err_t ret = data_buffer_get_latest(&sample);
        if (ret == ESP_OK) {
            if (sample.timestamp_us != s_last_sent_timestamp) {
                size_t len = build_frame(&sample, frame, sizeof(frame));
                if (len > 0) {
                    s_last_sent_timestamp = sample.timestamp_us;
                    led_status_data_pulse_start();
                    esp_err_t ble_ret = ble_stream_notify(frame, (uint16_t)len);
                    led_status_data_pulse_end();

                    if (ble_ret != ESP_OK && ble_ret != ESP_ERR_INVALID_STATE) {
                        log_error_throttled("BLE notify failed", ble_ret);
                    }
                }
            }
        } else if (ret != ESP_ERR_NOT_FOUND) {
            log_error_throttled("Buffer read failed", ret);
        }

        vTaskDelayUntil(&last, period);
    }
}

esp_err_t imu_ble_init(const imu_ble_config_t *cfg)
{
    if (!cfg || cfg->packet_interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->packet_interval_ms < 10) {
        ESP_LOGW(TAG, "packet_interval_ms=%u too low, using minimum 10ms", cfg->packet_interval_ms);
        s_cfg = *cfg;
        s_cfg.packet_interval_ms = 10;
    } else {
        s_cfg = *cfg;
    }

    if (s_producer_task) {
        ESP_LOGW(TAG, "imu_ble already initialised");
        return ESP_OK;
    }

    s_connected = false;
    s_notifications_ready = false;
    s_frame_seq = 0;
    s_last_sent_timestamp = 0;
    s_last_error_log_ms = 0;

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        producer_task,
        "imu_ble_producer",
        4096,
        NULL,
        5,
        &s_producer_task,
        0
    );

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create producer task");
        s_producer_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void imu_ble_on_ble_connect(void)
{
    s_connected = true;
    ESP_LOGI(TAG, "Central connected");
}

void imu_ble_on_ble_disconnect(void)
{
    s_connected = false;
    s_notifications_ready = false;
    s_frame_seq = 0;
    s_last_sent_timestamp = 0;
    s_last_error_log_ms = 0;
    ESP_LOGI(TAG, "Central disconnected");
}

void imu_ble_on_notifications_changed(bool enabled)
{
    s_notifications_ready = enabled;
    if (enabled) {
        s_frame_seq = 0;
        s_last_sent_timestamp = 0;
        s_last_error_log_ms = 0;
        ESP_LOGI(TAG, "Notifications enabled, streaming resumes");
    } else {
        ESP_LOGI(TAG, "Notifications disabled, streaming paused");
    }
}
