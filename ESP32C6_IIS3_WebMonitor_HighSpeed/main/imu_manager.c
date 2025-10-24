#include "imu_manager.h"
#include "sensors/iis3dwb_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "IMU_MANAGER";

#define IIS3DWB_MAX_ODR_HZ        26670.0f
#define IIS3DWB_SPI_HOST          SPI2_HOST
#define IIS3DWB_SPI_MISO          2
#define IIS3DWB_SPI_MOSI          7
#define IIS3DWB_SPI_CLK           6
#define IIS3DWB_SPI_CS            19
#define IIS3DWB_MAX_SAMPLES_BATCH 64

static SemaphoreHandle_t sensor_mutex = NULL;
static uint16_t fifo_watermark = IIS3DWB_MAX_SAMPLES_BATCH;
static float configured_odr_hz = IIS3DWB_MAX_ODR_HZ;
static uint64_t last_batch_timestamp_us = 0;
static stmdev_ctx_t accel_ctx = {0};
static iis3dwb_fs_xl_t current_full_scale = IIS3DWB_2g;
static imu_manager_full_scale_t current_full_scale_g = IMU_MANAGER_FS_2G;
static bool sensor_initialized = false;
static volatile bool pending_scale_change = false;
static volatile imu_manager_full_scale_t pending_scale = IMU_MANAGER_FS_2G;
_Static_assert(IMU_MANAGER_MAX_SAMPLES == IIS3DWB_MAX_SAMPLES_BATCH, "IMU manager sample configuration mismatch");
static float recent_ax[IIS3DWB_MAX_SAMPLES_BATCH];
static float recent_ay[IIS3DWB_MAX_SAMPLES_BATCH];
static float recent_az[IIS3DWB_MAX_SAMPLES_BATCH];
static uint16_t recent_samples = 0;
static uint16_t recent_fifo_level = 0;
static uint64_t recent_timestamp_us = 0;
static uint32_t recent_sequence = 0;

#define IIS3DWB_FIFO_SAMPLE_BYTES 7

static inline esp_err_t st_to_esp_err(int32_t ret)
{
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

static float convert_raw_to_g(int16_t raw)
{
    float mg = 0.0f;

    switch (current_full_scale) {
        case IIS3DWB_2g:
            mg = iis3dwb_from_fs2g_to_mg(raw);
            break;
        case IIS3DWB_4g:
            mg = iis3dwb_from_fs4g_to_mg(raw);
            break;
        case IIS3DWB_8g:
            mg = iis3dwb_from_fs8g_to_mg(raw);
            break;
        case IIS3DWB_16g:
            mg = iis3dwb_from_fs16g_to_mg(raw);
            break;
        default:
            mg = 0.0f;
            break;
    }

    return mg / 1000.0f;
}

static imu_manager_full_scale_t iis3dwb_to_manager_fs(iis3dwb_fs_xl_t fs)
{
    switch (fs) {
        case IIS3DWB_2g:
            return IMU_MANAGER_FS_2G;
        case IIS3DWB_4g:
            return IMU_MANAGER_FS_4G;
        case IIS3DWB_8g:
            return IMU_MANAGER_FS_8G;
        case IIS3DWB_16g:
            return IMU_MANAGER_FS_16G;
        default:
            return IMU_MANAGER_FS_2G;
    }
}

static bool manager_fs_is_valid(imu_manager_full_scale_t scale)
{
    return scale == IMU_MANAGER_FS_2G ||
           scale == IMU_MANAGER_FS_4G ||
           scale == IMU_MANAGER_FS_8G ||
           scale == IMU_MANAGER_FS_16G;
}

static iis3dwb_fs_xl_t manager_to_iis3dwb_fs(imu_manager_full_scale_t scale)
{
    switch (scale) {
        case IMU_MANAGER_FS_2G:
            return IIS3DWB_2g;
        case IMU_MANAGER_FS_4G:
            return IIS3DWB_4g;
        case IMU_MANAGER_FS_8G:
            return IIS3DWB_8g;
        case IMU_MANAGER_FS_16G:
            return IIS3DWB_16g;
        default:
            return IIS3DWB_2g;
    }
}

static esp_err_t iis3dwb_get_fifo_level(uint16_t *level, bool *overflowed)
{
    iis3dwb_fifo_status_t status;
    esp_err_t ret = st_to_esp_err(iis3dwb_fifo_status_get(&accel_ctx, &status));
    if (ret != ESP_OK) {
        return ret;
    }

    if (level) {
        *level = status.fifo_level;
    }
    if (overflowed) {
        *overflowed = status.fifo_ovr != 0;
    }

    return ESP_OK;
}

float imu_manager_get_configured_odr(void)
{
    return configured_odr_hz;
}

uint16_t imu_manager_get_fifo_watermark(void)
{
    return fifo_watermark;
}

esp_err_t imu_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing IMU Manager...");

    if (sensor_mutex != NULL) {
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
    }

    sensor_mutex = xSemaphoreCreateMutex();
    if (sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        return ESP_FAIL;
    }

    esp_err_t ret;

    const spi_bus_config_t buscfg = {
        .miso_io_num = IIS3DWB_SPI_MISO,
        .mosi_io_num = IIS3DWB_SPI_MOSI,
        .sclk_io_num = IIS3DWB_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
    };

    ret = spi_bus_initialize(IIS3DWB_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully (MISO=%d, MOSI=%d, CLK=%d)",
             IIS3DWB_SPI_MISO, IIS3DWB_SPI_MOSI, IIS3DWB_SPI_CLK);

    ret = iis3dwb_hal_init(&accel_ctx, IIS3DWB_SPI_HOST, IIS3DWB_SPI_CS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB HAL init failed: %s", esp_err_to_name(ret));
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    current_full_scale = manager_to_iis3dwb_fs(current_full_scale_g);

    iis3dwb_hal_cfg_t cfg = {
        .bdu = PROPERTY_ENABLE,
        .odr = IIS3DWB_XL_ODR_26k7Hz,
        .fs = current_full_scale,
        .filter = IIS3DWB_LP_ODR_DIV_100,
    };

    ret = iis3dwb_hal_configure(&accel_ctx, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB HAL configure failed: %s", esp_err_to_name(ret));
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_auto_increment_set(&accel_ctx, PROPERTY_ENABLE));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable register auto-increment");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_watermark_set(&accel_ctx, fifo_watermark));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set FIFO watermark");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_mode_set(&accel_ctx, IIS3DWB_BYPASS_MODE));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to put FIFO in bypass mode");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_stop_on_wtm_set(&accel_ctx, PROPERTY_DISABLE));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure FIFO stop on watermark");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_xl_batch_set(&accel_ctx, IIS3DWB_XL_BATCHED_AT_26k7Hz));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure accelerometer batching");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_temp_batch_set(&accel_ctx, IIS3DWB_TEMP_NOT_BATCHED));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable temperature batching");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_timestamp_batch_set(&accel_ctx, IIS3DWB_NO_DECIMATION));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timestamp batching");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_timestamp_set(&accel_ctx, PROPERTY_DISABLE));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable timestamp counter");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_fifo_mode_set(&accel_ctx, IIS3DWB_STREAM_MODE));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set FIFO stream mode");
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
        return ret;
    }

    ret = st_to_esp_err(iis3dwb_xl_full_scale_get(&accel_ctx, &current_full_scale));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read back accelerometer full-scale setting, defaulting to configured value");
        current_full_scale = cfg.fs;
        current_full_scale_g = iis3dwb_to_manager_fs(current_full_scale);
    } else {
        current_full_scale_g = iis3dwb_to_manager_fs(current_full_scale);
    }

    recent_samples = 0;
    recent_fifo_level = 0;
    recent_timestamp_us = 0;
    recent_sequence = 0;
    configured_odr_hz = IIS3DWB_MAX_ODR_HZ;
    ESP_LOGI(TAG, "IIS3DWB initialized at %.2f Hz ODR (watermark=%u)", configured_odr_hz, fifo_watermark);
    last_batch_timestamp_us = esp_timer_get_time();
    sensor_initialized = true;
    return ESP_OK;
}

esp_err_t imu_manager_read_all(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Apply pending scale change if requested (safe to do here in MAIN task)
    if (pending_scale_change) {
        iis3dwb_fs_xl_t desired_fs = manager_to_iis3dwb_fs(pending_scale);
        esp_err_t scale_ret = st_to_esp_err(iis3dwb_xl_full_scale_set(&accel_ctx, desired_fs));
        if (scale_ret == ESP_OK) {
            current_full_scale = desired_fs;
            current_full_scale_g = pending_scale;
            ESP_LOGI(TAG, "Full scale updated to +/- %dg", (int)pending_scale);
        } else {
            ESP_LOGE(TAG, "Failed to update full scale: %s", esp_err_to_name(scale_ret));
        }
        pending_scale_change = false;
    }
    
    // Don't lock mutex for entire FIFO read - only lock when updating recent_ arrays
    esp_err_t ret;
    data->timestamp_us = esp_timer_get_time();
    ret = imu_manager_read_accelerometer(data);

    return ret;
}

esp_err_t imu_manager_read_accelerometer(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor_initialized) {
        data->accelerometer.valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    if (data->timestamp_us == 0) {
        data->timestamp_us = esp_timer_get_time();
    }

    uint16_t fifo_level = 0;
    bool overflow = false;
    esp_err_t ret = iis3dwb_get_fifo_level(&fifo_level, &overflow);
    if (ret != ESP_OK) {
        data->accelerometer.valid = false;
        return ret;
    }

    if (overflow) {
        static uint32_t overflow_log_count = 0;
        if ((overflow_log_count++ % 100) == 0) {
            ESP_LOGW(TAG, "IIS3DWB FIFO overflow detected (level=%u)", fifo_level);
        }
    }

    const uint16_t fifo_level_before = fifo_level;
    if (fifo_level_before == 0) {
        // No samples waiting, fall back to direct read
        int16_t raw[3] = {0};
        ret = st_to_esp_err(iis3dwb_acceleration_raw_get(&accel_ctx, raw));
        if (ret == ESP_OK) {
            const float ax = convert_raw_to_g(raw[0]);
            const float ay = convert_raw_to_g(raw[1]);
            const float az = convert_raw_to_g(raw[2]);

            data->accelerometer.x_g = ax;
            data->accelerometer.y_g = ay;
            data->accelerometer.z_g = az;
            data->accelerometer.magnitude_g = sqrtf(ax * ax + ay * ay + az * az);
            data->accelerometer.valid = true;
            data->stats.fifo_level = 0;
            data->stats.samples_read = 1;
            data->stats.odr_hz = configured_odr_hz;
            data->stats.batch_interval_us = 1e6f / configured_odr_hz;
            data->stats.samples_per_second = configured_odr_hz;

            recent_ax[0] = ax;
            recent_ay[0] = ay;
            recent_az[0] = az;
            recent_samples = 1;
            recent_fifo_level = 0;
            recent_timestamp_us = data->timestamp_us;
            last_batch_timestamp_us = data->timestamp_us;
            recent_sequence++;
        } else {
            data->accelerometer.valid = false;
        }
        return ret;
    }

    if (fifo_level_before > IIS3DWB_MAX_SAMPLES_BATCH) {
        static uint32_t high_fifo_log_count = 0;
        if ((high_fifo_log_count++ % 1000) == 0) {
            ESP_LOGI(TAG,
                     "High FIFO level detected (%u > %u), draining without dropping samples",
                     fifo_level_before, IIS3DWB_MAX_SAMPLES_BATCH);
        }
    }

    uint8_t fifo_raw[IIS3DWB_MAX_SAMPLES_BATCH * IIS3DWB_FIFO_SAMPLE_BYTES];
    float ax_buf[IIS3DWB_MAX_SAMPLES_BATCH];
    float ay_buf[IIS3DWB_MAX_SAMPLES_BATCH];
    float az_buf[IIS3DWB_MAX_SAMPLES_BATCH];

    uint32_t total_accel_count = 0;
    float last_ax = 0.0f;
    float last_ay = 0.0f;
    float last_az = 0.0f;
    uint16_t last_chunk_count = 0;

    uint16_t remaining_entries = fifo_level_before;
    while (remaining_entries > 0) {
        const uint16_t chunk_entries = remaining_entries > IIS3DWB_MAX_SAMPLES_BATCH
                                           ? IIS3DWB_MAX_SAMPLES_BATCH
                                           : remaining_entries;
        remaining_entries = (remaining_entries > chunk_entries)
                                ? (uint16_t)(remaining_entries - chunk_entries)
                                : 0;

        ret = st_to_esp_err(iis3dwb_read_reg(&accel_ctx, IIS3DWB_FIFO_DATA_OUT_TAG,
                                             fifo_raw, chunk_entries * IIS3DWB_FIFO_SAMPLE_BYTES));
        if (ret != ESP_OK) {
            data->accelerometer.valid = false;
            return ret;
        }

        size_t accel_count = 0;
        for (uint16_t i = 0; i < chunk_entries; i++) {
            const size_t offset = i * IIS3DWB_FIFO_SAMPLE_BYTES;
            const uint8_t tag_raw = fifo_raw[offset];
            const iis3dwb_fifo_tag_t tag = (iis3dwb_fifo_tag_t)(tag_raw >> 3);

            if (tag != IIS3DWB_XL_TAG) {
                continue;
            }

            const int16_t raw_x = (int16_t)(fifo_raw[offset + 2] << 8 | fifo_raw[offset + 1]);
            const int16_t raw_y = (int16_t)(fifo_raw[offset + 4] << 8 | fifo_raw[offset + 3]);
            const int16_t raw_z = (int16_t)(fifo_raw[offset + 6] << 8 | fifo_raw[offset + 5]);

            if (accel_count < IIS3DWB_MAX_SAMPLES_BATCH) {
                ax_buf[accel_count] = convert_raw_to_g(raw_x);
                ay_buf[accel_count] = convert_raw_to_g(raw_y);
                az_buf[accel_count] = convert_raw_to_g(raw_z);
                accel_count++;
            }
        }

        if (accel_count == 0) {
            continue;
        }

        total_accel_count += accel_count;
        const uint16_t last_index = (uint16_t)(accel_count - 1);
        last_ax = ax_buf[last_index];
        last_ay = ay_buf[last_index];
        last_az = az_buf[last_index];
        last_chunk_count = (uint16_t)accel_count;

        // Lock mutex only when updating recent_ arrays (minimize lock time)
        if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            recent_sequence++;
            memcpy(recent_ax, ax_buf, accel_count * sizeof(float));
            memcpy(recent_ay, ay_buf, accel_count * sizeof(float));
            memcpy(recent_az, az_buf, accel_count * sizeof(float));
            recent_samples = (uint16_t)accel_count;
            xSemaphoreGive(sensor_mutex);
        }
    }

    if (total_accel_count == 0) {
        data->accelerometer.valid = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    data->accelerometer.x_g = last_ax;
    data->accelerometer.y_g = last_ay;
    data->accelerometer.z_g = last_az;
    data->accelerometer.magnitude_g = sqrtf(last_ax * last_ax + last_ay * last_ay + last_az * last_az);
    data->accelerometer.valid = true;

    const uint64_t now_us = data->timestamp_us;
    float samples_per_second = configured_odr_hz;
    if (last_batch_timestamp_us != 0 && now_us > last_batch_timestamp_us) {
        const float elapsed_us = (float)(now_us - last_batch_timestamp_us);
        samples_per_second = (total_accel_count * 1e6f) / elapsed_us;
    }
    last_batch_timestamp_us = now_us;

    data->stats.fifo_level = fifo_level_before;
    data->stats.samples_read = (total_accel_count > UINT16_MAX) ? UINT16_MAX : (uint16_t)total_accel_count;
    data->stats.odr_hz = configured_odr_hz;
    data->stats.batch_interval_us = (total_accel_count * 1e6f) / configured_odr_hz;
    data->stats.samples_per_second = samples_per_second;

    if (samples_per_second > configured_odr_hz * 1.1f || samples_per_second < configured_odr_hz * 0.1f) {
        ESP_LOGW(TAG, "Unexpected sample throughput: %.1f sps (expected %.1f)", samples_per_second, configured_odr_hz);
    }

    recent_fifo_level = fifo_level_before;
    recent_timestamp_us = data->timestamp_us;

    return ESP_OK;
}

imu_manager_full_scale_t imu_manager_get_full_scale(void)
{
    return current_full_scale_g;
}

uint8_t imu_manager_get_full_scale_g(void)
{
    return (uint8_t)current_full_scale_g;
}

esp_err_t imu_manager_set_full_scale(imu_manager_full_scale_t scale)
{
    if (!manager_fs_is_valid(scale)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor_initialized) {
        iis3dwb_fs_xl_t desired_fs = manager_to_iis3dwb_fs(scale);
        current_full_scale = desired_fs;
        current_full_scale_g = scale;
        return ESP_OK;
    }

    // Defer the scale change to avoid SPI race condition
    // The MAIN task will apply it on next read cycle
    pending_scale = scale;
    pending_scale_change = true;
    ESP_LOGI(TAG, "Scheduled full scale change to +/- %dg", (int)scale);
    
    return ESP_OK;
}

esp_err_t imu_manager_deinit(void)
{
    if (sensor_initialized) {
        iis3dwb_hal_deinit(&accel_ctx);
        spi_bus_free(IIS3DWB_SPI_HOST);
        sensor_initialized = false;
    }

    if (sensor_mutex) {
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
    }
    
    last_batch_timestamp_us = 0;
    recent_samples = 0;
    recent_fifo_level = 0;
    recent_timestamp_us = 0;
    recent_sequence = 0;
    
    ESP_LOGI(TAG, "IMU Manager deinitialized");
    return ESP_OK;
}

uint16_t imu_manager_copy_recent_samples(float *x_g, float *y_g, float *z_g,
                                         uint16_t max_samples, uint64_t *timestamp_us,
                                         uint16_t *fifo_level, uint32_t *sequence_id)
{
    if (x_g == NULL || y_g == NULL || z_g == NULL || max_samples == 0) {
        return 0;
    }

    if (sensor_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex in copy_recent_samples (timeout)");
        return 0;
    }

    uint16_t count = recent_samples;
    if (count > max_samples) {
        count = max_samples;
    }

    if (count > 0) {
        memcpy(x_g, recent_ax, count * sizeof(float));
        memcpy(y_g, recent_ay, count * sizeof(float));
        memcpy(z_g, recent_az, count * sizeof(float));
    }

    if (timestamp_us) {
        *timestamp_us = recent_timestamp_us;
    }
    if (fifo_level) {
        *fifo_level = recent_fifo_level;
    }
    if (sequence_id) {
        *sequence_id = recent_sequence;
    }

    xSemaphoreGive(sensor_mutex);
    return count;
}
