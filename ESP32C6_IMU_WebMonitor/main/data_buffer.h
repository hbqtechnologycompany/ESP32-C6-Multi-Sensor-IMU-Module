#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

#include "esp_err.h"
#include "imu_manager.h"
#include <stdint.h>
#include <stdbool.h>

// Buffer configuration
#define DATA_BUFFER_SIZE 1000  // Number of samples to keep in buffer
#define DATA_BUFFER_OVERWRITE true  // Overwrite oldest data when full

// Statistics structure
typedef struct {
    uint32_t total_samples;
    uint32_t dropped_samples;
    uint32_t buffer_overflows;
    uint64_t last_timestamp_us;
    float avg_processing_time_us;
} buffer_stats_t;

// Data buffer API
esp_err_t data_buffer_init(void);
esp_err_t data_buffer_add(const imu_data_t *data);
esp_err_t data_buffer_get(imu_data_t *data);
esp_err_t data_buffer_get_latest(imu_data_t *data);
esp_err_t data_buffer_get_range(imu_data_t *data, uint32_t start_idx, uint32_t count);
esp_err_t data_buffer_get_stats(buffer_stats_t *stats);
esp_err_t data_buffer_clear(void);
uint32_t data_buffer_get_count(void);
bool data_buffer_is_full(void);
bool data_buffer_is_empty(void);

// JSON export functions
esp_err_t data_buffer_export_json(char *json_buffer, size_t buffer_size, uint32_t max_samples);
esp_err_t data_buffer_export_csv(char *csv_buffer, size_t buffer_size, uint32_t max_samples);
esp_err_t data_buffer_export_json_dynamic(char **out_buf, size_t *out_len, uint32_t max_samples);
esp_err_t data_buffer_export_csv_dynamic(char **out_buf, size_t *out_len, uint32_t max_samples);

#endif // DATA_BUFFER_H
