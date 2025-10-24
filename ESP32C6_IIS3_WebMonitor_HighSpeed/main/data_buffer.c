#include "data_buffer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
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

        if (data->accelerometer.valid) {
            cJSON *accel_g = cJSON_CreateObject();
            cJSON_AddNumberToObject(accel_g, "x_g", data->accelerometer.x_g);
            cJSON_AddNumberToObject(accel_g, "y_g", data->accelerometer.y_g);
            cJSON_AddNumberToObject(accel_g, "z_g", data->accelerometer.z_g);
            cJSON_AddNumberToObject(accel_g, "magnitude_g", data->accelerometer.magnitude_g);
            cJSON_AddItemToObject(sample, "accelerometer_g", accel_g);

            const float g_to_ms2 = 9.80665f;
            cJSON *accel_ms2 = cJSON_CreateObject();
            cJSON_AddNumberToObject(accel_ms2, "x_ms2", data->accelerometer.x_g * g_to_ms2);
            cJSON_AddNumberToObject(accel_ms2, "y_ms2", data->accelerometer.y_g * g_to_ms2);
            cJSON_AddNumberToObject(accel_ms2, "z_ms2", data->accelerometer.z_g * g_to_ms2);
            cJSON_AddNumberToObject(accel_ms2, "magnitude_ms2", data->accelerometer.magnitude_g * g_to_ms2);
            cJSON_AddItemToObject(sample, "accelerometer_ms2", accel_ms2);
        }

        cJSON *stats_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(stats_obj, "fifo_level", data->stats.fifo_level);
        cJSON_AddNumberToObject(stats_obj, "samples_read", data->stats.samples_read);
        cJSON_AddNumberToObject(stats_obj, "odr_hz", data->stats.odr_hz);
        cJSON_AddNumberToObject(stats_obj, "batch_interval_us", data->stats.batch_interval_us);
        cJSON_AddNumberToObject(stats_obj, "samples_per_second", data->stats.samples_per_second);
        cJSON_AddItemToObject(sample, "sensor_stats", stats_obj);

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
        "timestamp_us,accel_x_g,accel_y_g,accel_z_g,accel_magnitude_g,"
        "accel_x_ms2,accel_y_ms2,accel_z_ms2,accel_magnitude_ms2,"
        "fifo_level,samples_read,odr_hz,batch_interval_us,samples_per_second\n");
    
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
        
        float ax_g = data->accelerometer.valid ? data->accelerometer.x_g : 0.0f;
        float ay_g = data->accelerometer.valid ? data->accelerometer.y_g : 0.0f;
        float az_g = data->accelerometer.valid ? data->accelerometer.z_g : 0.0f;
        float mag_g = data->accelerometer.valid ? data->accelerometer.magnitude_g : 0.0f;
        const float g_to_ms2 = 9.80665f;
        int row_len = snprintf(csv_buffer + offset, buffer_size - offset,
            "%llu,%.5f,%.5f,%.5f,%.5f,"
            "%.5f,%.5f,%.5f,%.5f,"
            "%u,%u,%.2f,%.2f,%.2f\n",
            (unsigned long long)data->timestamp_us,
            ax_g,
            ay_g,
            az_g,
            mag_g,
            ax_g * g_to_ms2,
            ay_g * g_to_ms2,
            az_g * g_to_ms2,
            mag_g * g_to_ms2,
            data->stats.fifo_level,
            data->stats.samples_read,
            data->stats.odr_hz,
            data->stats.batch_interval_us,
            data->stats.samples_per_second);
        
        if (row_len >= (buffer_size - offset)) {
            break; // Buffer full
        }
        offset += row_len;
    }
    
    xSemaphoreGive(buffer_mutex);
    return ESP_OK;
}
