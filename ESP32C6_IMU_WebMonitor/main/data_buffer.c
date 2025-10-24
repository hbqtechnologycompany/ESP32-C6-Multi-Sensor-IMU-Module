#include "data_buffer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_timer.h"

static const char *TAG = "DATA_BUFFER";

// Circular buffer structure
typedef struct {
    imu_data_t data[DATA_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    bool full;
    buffer_stats_t stats;
} data_buffer_t;

static data_buffer_t buffer;
static SemaphoreHandle_t buffer_mutex = NULL;

esp_err_t data_buffer_init(void)
{
    ESP_LOGI(TAG, "Initializing data buffer...");
    
    // Create mutex
    buffer_mutex = xSemaphoreCreateMutex();
    if (buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex");
        return ESP_FAIL;
    }
    
    // Initialize buffer
    memset(&buffer, 0, sizeof(buffer));
    buffer.head = 0;
    buffer.tail = 0;
    buffer.count = 0;
    buffer.full = false;
    
    // Initialize statistics
    buffer.stats.total_samples = 0;
    buffer.stats.dropped_samples = 0;
    buffer.stats.buffer_overflows = 0;
    buffer.stats.last_timestamp_us = 0;
    buffer.stats.avg_processing_time_us = 0.0f;
    
    ESP_LOGI(TAG, "Data buffer initialized with size %d", DATA_BUFFER_SIZE);
    return ESP_OK;
}

esp_err_t data_buffer_add(const imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        buffer.stats.dropped_samples++;
        return ESP_ERR_TIMEOUT;
    }
    
    int64_t start_time = esp_timer_get_time();
    
    // Check if buffer is full
    if (buffer.full && !DATA_BUFFER_OVERWRITE) {
        buffer.stats.dropped_samples++;
        xSemaphoreGive(buffer_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Add data to buffer
    buffer.data[buffer.head] = *data;
    buffer.head = (buffer.head + 1) % DATA_BUFFER_SIZE;
    
    if (buffer.full) {
        buffer.tail = (buffer.tail + 1) % DATA_BUFFER_SIZE;
        buffer.stats.buffer_overflows++;
    } else {
        buffer.count++;
        if (buffer.head == buffer.tail) {
            buffer.full = true;
        }
    }
    
    // Update statistics
    buffer.stats.total_samples++;
    buffer.stats.last_timestamp_us = data->timestamp_us;
    
    int64_t end_time = esp_timer_get_time();
    float processing_time = (float)(end_time - start_time);
    buffer.stats.avg_processing_time_us = (buffer.stats.avg_processing_time_us * 0.9f) + (processing_time * 0.1f);
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_get(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (buffer.count == 0 && !buffer.full) {
        xSemaphoreGive(buffer_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    *data = buffer.data[buffer.tail];
    buffer.tail = (buffer.tail + 1) % DATA_BUFFER_SIZE;
    buffer.count--;
    buffer.full = false;
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_get_latest(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Buffer is empty only when count is 0 AND not full
    // If full=true, buffer has DATA_BUFFER_SIZE valid samples
    if (buffer.count == 0 && !buffer.full) {
        xSemaphoreGive(buffer_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get the most recent data
    // When full, head points to next write position, so last valid is head-1
    // When not full, head-1 is also the last written
    uint32_t latest_idx = (buffer.head - 1 + DATA_BUFFER_SIZE) % DATA_BUFFER_SIZE;
    *data = buffer.data[latest_idx];
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_get_range(imu_data_t *data, uint32_t start_idx, uint32_t count)
{
    if (data == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint32_t available_count = buffer.full ? DATA_BUFFER_SIZE : buffer.count;
    if (start_idx >= available_count) {
        xSemaphoreGive(buffer_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t actual_count = (start_idx + count > available_count) ? 
                           (available_count - start_idx) : count;
    
    for (uint32_t i = 0; i < actual_count; i++) {
        uint32_t idx = (buffer.tail + start_idx + i) % DATA_BUFFER_SIZE;
        data[i] = buffer.data[idx];
    }
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_get_stats(buffer_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *stats = buffer.stats;
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_clear(void)
{
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    buffer.head = 0;
    buffer.tail = 0;
    buffer.count = 0;
    buffer.full = false;
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

uint32_t data_buffer_get_count(void)
{
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }
    
    uint32_t count = buffer.full ? DATA_BUFFER_SIZE : buffer.count;
    xSemaphoreGive(buffer_mutex);
    return count;
}

bool data_buffer_is_full(void)
{
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    
    bool full = buffer.full;
    xSemaphoreGive(buffer_mutex);
    return full;
}

bool data_buffer_is_empty(void)
{
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return true;
    }
    
    bool empty = (buffer.count == 0 && !buffer.full);
    xSemaphoreGive(buffer_mutex);
    return empty;
}

esp_err_t data_buffer_export_json(char *json_buffer, size_t buffer_size, uint32_t max_samples)
{
    if (json_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON *samples = cJSON_CreateArray();
    cJSON *stats = cJSON_CreateObject();
    
    // Add statistics
    cJSON_AddNumberToObject(stats, "total_samples", buffer.stats.total_samples);
    cJSON_AddNumberToObject(stats, "dropped_samples", buffer.stats.dropped_samples);
    cJSON_AddNumberToObject(stats, "buffer_overflows", buffer.stats.buffer_overflows);
    cJSON_AddNumberToObject(stats, "last_timestamp_us", buffer.stats.last_timestamp_us);
    cJSON_AddNumberToObject(stats, "avg_processing_time_us", buffer.stats.avg_processing_time_us);
    cJSON_AddItemToObject(root, "statistics", stats);
    
    // Add samples
    uint32_t available_count = buffer.full ? DATA_BUFFER_SIZE : buffer.count;
    uint32_t export_count = (max_samples > 0 && max_samples < available_count) ? 
                           max_samples : available_count;
    
    for (uint32_t i = 0; i < export_count; i++) {
        uint32_t idx = (buffer.tail + i) % DATA_BUFFER_SIZE;
        imu_data_t *data = &buffer.data[idx];
        
        cJSON *sample = cJSON_CreateObject();
        cJSON_AddNumberToObject(sample, "timestamp_us", data->timestamp_us);
        
        // Magnetometer data
        if (data->magnetometer.valid) {
            cJSON *mag = cJSON_CreateObject();
            cJSON_AddNumberToObject(mag, "x_mg", data->magnetometer.x_mg);
            cJSON_AddNumberToObject(mag, "y_mg", data->magnetometer.y_mg);
            cJSON_AddNumberToObject(mag, "z_mg", data->magnetometer.z_mg);
            cJSON_AddNumberToObject(mag, "temperature_c", data->magnetometer.temperature_c);
            cJSON_AddItemToObject(sample, "magnetometer", mag);
        }
        
        // Accelerometer data
        if (data->accelerometer.valid) {
            cJSON *accel = cJSON_CreateObject();
            cJSON_AddNumberToObject(accel, "x_g", data->accelerometer.x_g);
            cJSON_AddNumberToObject(accel, "y_g", data->accelerometer.y_g);
            cJSON_AddNumberToObject(accel, "z_g", data->accelerometer.z_g);
            cJSON_AddItemToObject(sample, "accelerometer", accel);
        }
        
        // IMU 6-axis data
        if (data->imu_6axis.valid) {
            cJSON *imu = cJSON_CreateObject();
            cJSON *accel = cJSON_CreateObject();
            cJSON *gyro = cJSON_CreateObject();
            
            cJSON_AddNumberToObject(accel, "x_g", data->imu_6axis.accel_x_g);
            cJSON_AddNumberToObject(accel, "y_g", data->imu_6axis.accel_y_g);
            cJSON_AddNumberToObject(accel, "z_g", data->imu_6axis.accel_z_g);
            cJSON_AddNumberToObject(gyro, "x_dps", data->imu_6axis.gyro_x_dps);
            cJSON_AddNumberToObject(gyro, "y_dps", data->imu_6axis.gyro_y_dps);
            cJSON_AddNumberToObject(gyro, "z_dps", data->imu_6axis.gyro_z_dps);
            cJSON_AddNumberToObject(imu, "temperature_c", data->imu_6axis.temperature_c);
            
            cJSON_AddItemToObject(imu, "accelerometer", accel);
            cJSON_AddItemToObject(imu, "gyroscope", gyro);
            cJSON_AddItemToObject(sample, "imu_6axis", imu);
        }
        
        // Inclinometer data
        if (data->inclinometer.valid) {
            cJSON *incl = cJSON_CreateObject();
            cJSON *angles = cJSON_CreateObject();
            cJSON *accel = cJSON_CreateObject();
            
            cJSON_AddNumberToObject(angles, "x_deg", data->inclinometer.angle_x_deg);
            cJSON_AddNumberToObject(angles, "y_deg", data->inclinometer.angle_y_deg);
            cJSON_AddNumberToObject(angles, "z_deg", data->inclinometer.angle_z_deg);
            cJSON_AddNumberToObject(accel, "x_g", data->inclinometer.accel_x_g);
            cJSON_AddNumberToObject(accel, "y_g", data->inclinometer.accel_y_g);
            cJSON_AddNumberToObject(accel, "z_g", data->inclinometer.accel_z_g);
            cJSON_AddNumberToObject(incl, "temperature_c", data->inclinometer.temperature_c);
            
            cJSON_AddItemToObject(incl, "angles", angles);
            cJSON_AddItemToObject(incl, "accelerometer", accel);
            cJSON_AddItemToObject(sample, "inclinometer", incl);
        }
        
        cJSON_AddItemToArray(samples, sample);
    }
    
    cJSON_AddItemToObject(root, "samples", samples);
    cJSON_AddNumberToObject(root, "sample_count", export_count);
    
    char *json_string = cJSON_Print(root);
    if (json_string != NULL) {
        strncpy(json_buffer, json_string, buffer_size - 1);
        json_buffer[buffer_size - 1] = '\0';
        free(json_string);
    }
    
    cJSON_Delete(root);
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_export_csv(char *csv_buffer, size_t buffer_size, uint32_t max_samples)
{
    if (csv_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // CSV header
    int offset = snprintf(csv_buffer, buffer_size,
        "timestamp_us,mag_x_mg,mag_y_mg,mag_z_mg,mag_temp_c,"
        "accel_x_g,accel_y_g,accel_z_g,"
        "imu_accel_x_g,imu_accel_y_g,imu_accel_z_g,"
        "imu_gyro_x_dps,imu_gyro_y_dps,imu_gyro_z_dps,imu_temp_c,"
        "incl_angle_x_deg,incl_angle_y_deg,incl_angle_z_deg,"
        "incl_accel_x_g,incl_accel_y_g,incl_accel_z_g,incl_temp_c\n");
    
    if (offset >= buffer_size) {
        xSemaphoreGive(buffer_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Add data rows
    uint32_t available_count = buffer.full ? DATA_BUFFER_SIZE : buffer.count;
    uint32_t export_count = (max_samples > 0 && max_samples < available_count) ? 
                           max_samples : available_count;
    
    for (uint32_t i = 0; i < export_count; i++) {
        uint32_t idx = (buffer.tail + i) % DATA_BUFFER_SIZE;
        imu_data_t *data = &buffer.data[idx];
        
        int row_len = snprintf(csv_buffer + offset, buffer_size - offset,
            "%llu,%.3f,%.3f,%.3f,%.2f,"
            "%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.2f,"
            "%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.2f\n",
            data->timestamp_us,
            data->magnetometer.valid ? data->magnetometer.x_mg : 0.0f,
            data->magnetometer.valid ? data->magnetometer.y_mg : 0.0f,
            data->magnetometer.valid ? data->magnetometer.z_mg : 0.0f,
            data->magnetometer.valid ? data->magnetometer.temperature_c : 0.0f,
            data->accelerometer.valid ? data->accelerometer.x_g : 0.0f,
            data->accelerometer.valid ? data->accelerometer.y_g : 0.0f,
            data->accelerometer.valid ? data->accelerometer.z_g : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.accel_x_g : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.accel_y_g : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.accel_z_g : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.gyro_x_dps : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.gyro_y_dps : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.gyro_z_dps : 0.0f,
            data->imu_6axis.valid ? data->imu_6axis.temperature_c : 0.0f,
            data->inclinometer.valid ? data->inclinometer.angle_x_deg : 0.0f,
            data->inclinometer.valid ? data->inclinometer.angle_y_deg : 0.0f,
            data->inclinometer.valid ? data->inclinometer.angle_z_deg : 0.0f,
            data->inclinometer.valid ? data->inclinometer.accel_x_g : 0.0f,
            data->inclinometer.valid ? data->inclinometer.accel_y_g : 0.0f,
            data->inclinometer.valid ? data->inclinometer.accel_z_g : 0.0f,
            data->inclinometer.valid ? data->inclinometer.temperature_c : 0.0f);
        
        if (row_len >= (buffer_size - offset)) {
            break; // Buffer full
        }
        offset += row_len;
    }
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}

esp_err_t data_buffer_export_csv_dynamic(char **out_buf, size_t *out_len, uint32_t max_samples) {
    if (!out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_buf = NULL;
    *out_len = 0;
    size_t capacity = 8192;
    char *buffer = malloc(capacity);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = data_buffer_export_csv(buffer, capacity, max_samples);
    if (ret == ESP_ERR_NO_MEM) {
        size_t new_capacity = capacity * 2;
        while (new_capacity < capacity * 8) {
            char *new_buf = realloc(buffer, new_capacity);
            if (!new_buf) {
                free(buffer);
                return ESP_ERR_NO_MEM;
            }
            buffer = new_buf;
            ret = data_buffer_export_csv(buffer, new_capacity, max_samples);
            if (ret != ESP_ERR_NO_MEM) {
                break;
            }
            new_capacity *= 2;
        }
    }
    if (ret == ESP_OK) {
        *out_buf = buffer;
        *out_len = strlen(buffer);
        return ESP_OK;
    }
    free(buffer);
    return ret;
}

esp_err_t data_buffer_export_json_dynamic(char **out_buf, size_t *out_len, uint32_t max_samples) {
    if (!out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_buf = NULL;
    *out_len = 0;
    size_t capacity = 8192;
    char *buffer = malloc(capacity);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = data_buffer_export_json(buffer, capacity, max_samples);
    if (ret == ESP_ERR_NO_MEM) {
        size_t new_capacity = capacity * 2;
        while (new_capacity < capacity * 8) {
            char *new_buf = realloc(buffer, new_capacity);
            if (!new_buf) {
                free(buffer);
                return ESP_ERR_NO_MEM;
            }
            buffer = new_buf;
            ret = data_buffer_export_json(buffer, new_capacity, max_samples);
            if (ret != ESP_ERR_NO_MEM) {
                break;
            }
            new_capacity *= 2;
        }
    }
    if (ret == ESP_OK) {
        *out_buf = buffer;
        *out_len = strlen(buffer);
        return ESP_OK;
    }
    free(buffer);
    return ret;
}
