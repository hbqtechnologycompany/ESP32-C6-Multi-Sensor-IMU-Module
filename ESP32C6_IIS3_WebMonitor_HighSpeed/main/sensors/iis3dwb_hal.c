/**
 * @file    iis3dwb_hal.c
 * @brief   This file contains the HAL layer for the IIS3DWB sensor
 */

#include "iis3dwb_hal.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ===== TAG FOR LOGGING =====
static const char *TAG = "IIS3DWB_HAL";

// ===== PRIVATE FUNCTION PROTOTYPES =====
static int32_t platform_write(void *handle, uint8_t reg,
                            const uint8_t *bufp, uint16_t len);
static int32_t platform_read(void *handle, uint8_t reg,
                            uint8_t *bufp, uint16_t len);
static void platform_delay(uint32_t ms);
static esp_err_t iis3dwb_hal_read_polling_data(stmdev_ctx_t *ctx, iis3dwb_hal_data_t *data, uint8_t sample);
#if FIFO_MODE
static esp_err_t iis3dwb_hal_read_fifo_data(stmdev_ctx_t *ctx, iis3dwb_hal_data_t *data);
#endif

// ===== PUBLIC FUNCTIONS =====
esp_err_t iis3dwb_hal_init(stmdev_ctx_t *dev_ctx, spi_host_device_t host, gpio_num_t cs_pin)
{
    esp_err_t ret;
    spi_device_handle_t spi_device_handle;
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = IIS3DWB_SPI_FREQ_HZ,
        .mode = IIS3DWB_SPI_MODE,
        .spics_io_num = cs_pin,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(host, &devcfg, &spi_device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize the device context
    dev_ctx->handle = (void *)spi_device_handle;
    dev_ctx->read_reg = platform_read;
    dev_ctx->write_reg = platform_write;
    dev_ctx->mdelay = platform_delay;

    uint8_t whoamI;
    ESP_ERROR_CHECK(iis3dwb_device_id_get(dev_ctx, &whoamI));
    if (whoamI != IIS3DWB_ID) {
        ESP_LOGE(TAG, "IIS3DWB not found. Expected ID: 0x%02X, Read ID: 0x%02X", IIS3DWB_ID, whoamI);
        return ESP_ERR_NOT_FOUND;
    }
        
    // ESP_LOGI(TAG, "IIS3DWB found. ID: 0x%02X", whoamI);

    return ESP_OK;
}

esp_err_t iis3dwb_hal_deinit(stmdev_ctx_t *dev_ctx){
    esp_err_t ret;

    // Deinitialize the SPI bus
    spi_device_handle_t spi = (spi_device_handle_t)dev_ctx->handle;
    ret = spi_bus_remove_device(spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Clear the device context
    memset(dev_ctx, 0, sizeof(stmdev_ctx_t));

    return ESP_OK;
}

esp_err_t iis3dwb_hal_configure(stmdev_ctx_t *dev_ctx, iis3dwb_hal_cfg_t *cfg)
{
    // Restore default configuration
    ESP_ERROR_CHECK(iis3dwb_reset_set(dev_ctx, 1));
    uint8_t rst;
    do {
        ESP_ERROR_CHECK(iis3dwb_reset_get(dev_ctx, &rst));
    } while (rst);

    // Enable Block Data Update
    ESP_ERROR_CHECK(iis3dwb_block_data_update_set(dev_ctx, cfg->bdu));
    // Set output data rate
    ESP_ERROR_CHECK(iis3dwb_xl_data_rate_set(dev_ctx, cfg->odr));
    // Set full scale
    ESP_ERROR_CHECK(iis3dwb_xl_full_scale_set(dev_ctx, cfg->fs));
    // Set filtering chain
    ESP_ERROR_CHECK(iis3dwb_xl_filt_path_on_out_set(dev_ctx, cfg->filter));

#if FIFO_MODE
    // Set FIFO mode 
    ESP_ERROR_CHECK(iis3dwb_fifo_mode_set(dev_ctx, cfg->fifo_mode));
    // Set FIFO watermark
    ESP_ERROR_CHECK(iis3dwb_fifo_watermark_set(dev_ctx, cfg->fifo_watermark));
    // FIFO depth is limited to threshold level
    ESP_ERROR_CHECK(iis3dwb_fifo_stop_on_wtm_set(dev_ctx, PROPERTY_ENABLE));
    // Set accelerometer batching
    ESP_ERROR_CHECK(iis3dwb_fifo_xl_batch_set(dev_ctx, cfg->fifo_xl_batch));
    // Set temperature batching
    ESP_ERROR_CHECK(iis3dwb_fifo_temp_batch_set(dev_ctx, cfg->fifo_temp_batch));
    // Set timestamp batching
    ESP_ERROR_CHECK(iis3dwb_fifo_timestamp_batch_set(dev_ctx, cfg->fifo_timestamp_batch));
    // Enable timestamp batching
    ESP_ERROR_CHECK(iis3dwb_timestamp_set(dev_ctx, cfg->fifo_timestamp_batch));  
#endif

    return ESP_OK;

}

esp_err_t iis3dwb_hal_read_data(stmdev_ctx_t *dev_ctx, iis3dwb_hal_data_t *data)
{
#if FIFO_MODE
    esp_err_t ret = iis3dwb_hal_read_fifo_data(dev_ctx, data);
    if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "FIFO timeout, falling back to polling mode");
        return iis3dwb_hal_read_polling_data(dev_ctx, data, 1);
    }
    return ret;
#else
    return iis3dwb_hal_read_polling_data(dev_ctx, data, 20); // Average over 20 samples
#endif
}

esp_err_t iis3dwb_hal_read_polling_single(stmdev_ctx_t *dev_ctx, iis3dwb_hal_data_t *data, uint8_t sample_count)
{
    return iis3dwb_hal_read_polling_data(dev_ctx, data, sample_count);
}

static esp_err_t iis3dwb_hal_read_polling_data(stmdev_ctx_t *ctx, iis3dwb_hal_data_t *data, uint8_t sample)
{
    uint8_t reg;
    int16_t data_raw_acceleration[3] = {0};
    int16_t data_raw_temperature = 0;
    float data_accel[3] = {0};
    float data_temp = 0;

    for(int i = 0; i < sample; i++){
        /* Read output only if new value is available */
        do{
            ESP_ERROR_CHECK(iis3dwb_xl_flag_data_ready_get(ctx, &reg));
        }while(!reg);
        
        /* Read acceleration data */
        ESP_ERROR_CHECK(iis3dwb_acceleration_raw_get(ctx, data_raw_acceleration));
        data_accel[0] += (float)data_raw_acceleration[0];
        data_accel[1] += (float)data_raw_acceleration[1];
        data_accel[2] += (float)data_raw_acceleration[2];

        do{
            ESP_ERROR_CHECK(iis3dwb_temp_flag_data_ready_get(ctx, &reg));
        }while(!reg);

        /* Read temperature data */
        ESP_ERROR_CHECK(iis3dwb_temperature_raw_get(ctx, &data_raw_temperature));
        data_temp += (float)data_raw_temperature;
    }

    // Convert acceleration data to mg
    iis3dwb_fs_xl_t full_scale;
    ESP_ERROR_CHECK(iis3dwb_xl_full_scale_get(ctx, &full_scale));
    switch(full_scale){
        case IIS3DWB_2g:
            data->x_mg = iis3dwb_from_fs2g_to_mg((int16_t)(data_accel[0]/sample));
            data->y_mg = iis3dwb_from_fs2g_to_mg((int16_t)(data_accel[1]/sample));
            data->z_mg = iis3dwb_from_fs2g_to_mg((int16_t)(data_accel[2]/sample));
            break;
        case IIS3DWB_4g:
            data->x_mg = iis3dwb_from_fs4g_to_mg((int16_t)(data_accel[0]/sample));
            data->y_mg = iis3dwb_from_fs4g_to_mg((int16_t)(data_accel[1]/sample));
            data->z_mg = iis3dwb_from_fs4g_to_mg((int16_t)(data_accel[2]/sample));
            break;
        case IIS3DWB_8g:
            data->x_mg = iis3dwb_from_fs8g_to_mg((int16_t)(data_accel[0]/sample));
            data->y_mg = iis3dwb_from_fs8g_to_mg((int16_t)(data_accel[1]/sample));
            data->z_mg = iis3dwb_from_fs8g_to_mg((int16_t)(data_accel[2]/sample));
            break;
        case IIS3DWB_16g:
            data->x_mg = iis3dwb_from_fs16g_to_mg((int16_t)(data_accel[0]/sample));
            data->y_mg = iis3dwb_from_fs16g_to_mg((int16_t)(data_accel[1]/sample));
            data->z_mg = iis3dwb_from_fs16g_to_mg((int16_t)(data_accel[2]/sample));
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    // Convert temperature data to degC
    data->temperature_degC = iis3dwb_from_lsb_to_celsius((int16_t)(data_temp/sample));

    return ESP_OK;
}

#if FIFO_MODE
static esp_err_t iis3dwb_hal_read_fifo_data(stmdev_ctx_t *ctx, iis3dwb_hal_data_t *data)
{
    uint16_t num_samples = 0;
    iis3dwb_fifo_status_t fifo_status;

    /* Variables for calculating sum and counting samples for averaging */
    float acc_x_sum = 0.0f, acc_y_sum = 0.0f, acc_z_sum = 0.0f;
    float temp_sum = 0.0f;
    uint16_t acc_count = 0;
    uint16_t temp_count = 0;
    uint16_t timestamp_count = 0;
    uint32_t last_timestamp_raw = 0;

    /* Wait until watermark flag is set with timeout */
    ESP_LOGI(TAG, "Waiting for FIFO watermark...");
    uint32_t timeout_count = 0;
    const uint32_t max_timeout = 1000; // 10 seconds timeout (10ms * 1000)
    
    do {
        esp_err_t ret = iis3dwb_fifo_status_get(ctx, &fifo_status);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get FIFO status");
            return ret;
        }
        
        if (!fifo_status.fifo_th) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms
            timeout_count++;
            if (timeout_count >= max_timeout) {
                ESP_LOGW(TAG, "FIFO watermark timeout! Current FIFO level: %d", fifo_status.fifo_level);
                // If we have some data, use it; otherwise return an error
                if (fifo_status.fifo_level > 0) {
                    break; // Use available data
                } else {
                    ESP_LOGE(TAG, "No FIFO data available after timeout");
                    return ESP_ERR_TIMEOUT;
                }
            }
        }
    } while (!fifo_status.fifo_th);

    num_samples = fifo_status.fifo_level;
    ESP_LOGI(TAG, "FIFO has %u samples. Reading and averaging...", num_samples);

    /* Read and process each data sample from FIFO */
    for (uint16_t i = 0; i < num_samples; i++) {
        iis3dwb_fifo_out_raw_t fifo_entry;

        /* Read one 7-byte entry from FIFO */
        esp_err_t ret = iis3dwb_fifo_out_raw_get(ctx, &fifo_entry);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read FIFO entry at index %u", i);
            continue;
        }

        /* Determine data type based on tag */
        iis3dwb_fifo_tag_t tag = (iis3dwb_fifo_tag_t)(fifo_entry.tag >> 3);
        iis3dwb_fs_xl_t full_scale;
        ESP_ERROR_CHECK(iis3dwb_xl_full_scale_get(ctx, &full_scale));
        switch (tag) {
            case IIS3DWB_XL_TAG: {
                int16_t ax = (int16_t)(fifo_entry.data[1] << 8 | fifo_entry.data[0]);
                int16_t ay = (int16_t)(fifo_entry.data[3] << 8 | fifo_entry.data[2]);
                int16_t az = (int16_t)(fifo_entry.data[5] << 8 | fifo_entry.data[4]);

                /* Accumulate values for averaging later */
                switch(full_scale){
                    case IIS3DWB_2g:
                        acc_x_sum += iis3dwb_from_fs2g_to_mg(ax);
                        acc_y_sum += iis3dwb_from_fs2g_to_mg(ay);
                        acc_z_sum += iis3dwb_from_fs2g_to_mg(az);
                        acc_count++;
                        break;
                    case IIS3DWB_4g:
                        acc_x_sum += iis3dwb_from_fs4g_to_mg(ax);
                        acc_y_sum += iis3dwb_from_fs4g_to_mg(ay);
                        acc_z_sum += iis3dwb_from_fs4g_to_mg(az);
                        acc_count++;
                        break;
                    case IIS3DWB_8g:
                        acc_x_sum += iis3dwb_from_fs8g_to_mg(ax);
                        acc_y_sum += iis3dwb_from_fs8g_to_mg(ay);
                        acc_z_sum += iis3dwb_from_fs8g_to_mg(az);
                        acc_count++;
                        break;
                    case IIS3DWB_16g:
                        acc_x_sum += iis3dwb_from_fs16g_to_mg(ax);
                        acc_y_sum += iis3dwb_from_fs16g_to_mg(ay);
                        acc_z_sum += iis3dwb_from_fs16g_to_mg(az);
                        acc_count++;
                        break;
                    default:
                        ESP_LOGW(TAG, "Sample %u: Unknown full scale setting: %d", i, full_scale);
                        break;
                }
                
                break;
            }

            case IIS3DWB_TEMPERATURE_TAG: {
                int16_t temp_raw = (int16_t)(fifo_entry.data[1] << 8 | fifo_entry.data[0]);
                
                /* Accumulate temperature values */
                temp_sum += iis3dwb_from_lsb_to_celsius(temp_raw);
                temp_count++;
                break;
            }

            case IIS3DWB_TIMESTAMP_TAG: {
                /* Reconstruct 32-bit timestamp from 4 data bytes */
                uint32_t timestamp_raw = (uint32_t)fifo_entry.data[3] << 24 |
                                         (uint32_t)fifo_entry.data[2] << 16 |
                                         (uint32_t)fifo_entry.data[1] << 8  |
                                         (uint32_t)fifo_entry.data[0];
                
                /* Save only the last timestamp */
                timestamp_count++;
                last_timestamp_raw = timestamp_raw;
                break;
            }

            default:
                ESP_LOGW(TAG, "Sample %u: Unknown FIFO tag: 0x%02X", i, tag);
                break;
        }
    }

    /* --- Calculate and return averaged results --- */
    
    // Process acceleration data
    if (acc_count > 0) {
        data->x_mg = acc_x_sum / acc_count;
        data->y_mg = acc_y_sum / acc_count;
        data->z_mg = acc_z_sum / acc_count;
    } else {
        data->x_mg = 0; data->y_mg = 0; data->z_mg = 0; // Default if no data
    }

    // Process temperature data
    if (temp_count > 0) {
        data->temperature_degC = temp_sum / temp_count;
    } else {
        data->temperature_degC = 0; // Default if no data
    }

    // Assign last timestamp
    data->timestamp_ms = last_timestamp_raw;
    
    // ESP_LOGI(TAG, "--- Averaged FIFO Result ---");
    // ESP_LOGI(TAG, "Processed %u samples from FIFO", num_samples);
    // if (acc_count > 0) {
    //     ESP_LOGI(TAG, "Avg Accel [mg]: X=%.2f, Y=%.2f, Z=%.2f (from %u samples)",
    //              data->x_mg, data->y_mg, data->z_mg, acc_count);
    // }
    // if (temp_count > 0) {
    //     ESP_LOGI(TAG, "Avg Temp [degC]: %.2f (from %u samples)", data->temperature_degC, temp_count);
    // }
    // if (last_timestamp_raw != 0) {
    //     ESP_LOGI(TAG, "Timestamp count = %u, Last Timestamp [raw]: %" PRIu32, timestamp_count, last_timestamp_raw);
    // }
    
    return ESP_OK;
}
#endif

esp_err_t iis3dwb_hal_self_test(stmdev_ctx_t *dev_ctx, uint8_t *result){
    int16_t data_raw[3];
    float_t val_st_off[3];
    float_t val_st_on[3];
    float_t test_val[3];
    uint8_t drdy, rst, i, j;

    ESP_LOGI(TAG, "Starting IIS3DWB self-test...");

    /* Restore default configuration */
    ESP_ERROR_CHECK(iis3dwb_reset_set(dev_ctx, PROPERTY_ENABLE));

    do {
        ESP_ERROR_CHECK(iis3dwb_reset_get(dev_ctx, &rst));
    } while (rst);

    /* Enable Block Data Update */
    ESP_ERROR_CHECK(iis3dwb_block_data_update_set(dev_ctx, PROPERTY_ENABLE));
    /*
    * Accelerometer Self Test
    */
    /* Set Output Data Rate */
    ESP_ERROR_CHECK(iis3dwb_xl_data_rate_set(dev_ctx, IIS3DWB_XL_ODR_26k7Hz));
    /* Set full scale */
    ESP_ERROR_CHECK(iis3dwb_xl_full_scale_set(dev_ctx, IIS3DWB_4g));
    /* Wait stable output */
    platform_delay(100);

    ESP_LOGI(TAG, "Reading baseline values (self-test OFF)...");

    /* Check if new value available */
    do {
        ESP_ERROR_CHECK(iis3dwb_xl_flag_data_ready_get(dev_ctx, &drdy));
    } while (!drdy);

    /* Read dummy data and discard it */
    ESP_ERROR_CHECK(iis3dwb_acceleration_raw_get(dev_ctx, data_raw));
    /* Read 5 sample and get the average vale for each axis */
    memset(val_st_off, 0x00, 3 * sizeof(float));

    for (i = 0; i < 5; i++) {
        /* Check if new value available */
        do {
        ESP_ERROR_CHECK(iis3dwb_xl_flag_data_ready_get(dev_ctx, &drdy));
        } while (!drdy);

        /* Read data and accumulate the mg value */
        ESP_ERROR_CHECK(iis3dwb_acceleration_raw_get(dev_ctx, data_raw));

        for (j = 0; j < 3; j++) {
        val_st_off[j] += iis3dwb_from_fs4g_to_mg(data_raw[j]);
        }
    }

    /* Calculate the mg average values */
    for (i = 0; i < 3; i++) {
        val_st_off[i] /= 5.0f;
    }

    /* Enable Self Test positive (or negative) */
    ESP_LOGI(TAG, "Enabling self-test and reading test values...");
    ESP_ERROR_CHECK(iis3dwb_xl_self_test_set(dev_ctx, IIS3DWB_XL_ST_POSITIVE));
    //iis3dwb_xl_self_test_set(&dev_ctx, IIS3DWB_XL_ST_NEGATIVE);
    /* Wait stable output */
    platform_delay(100);

    /* Check if new value available */
    do {
        ESP_ERROR_CHECK(iis3dwb_xl_flag_data_ready_get(dev_ctx, &drdy));
    } while (!drdy);

    /* Read dummy data and discard it */
    ESP_ERROR_CHECK(iis3dwb_acceleration_raw_get(dev_ctx, data_raw));
    /* Read 5 sample and get the average vale for each axis */
    memset(val_st_on, 0x00, 3 * sizeof(float));

    for (i = 0; i < 5; i++) {
        /* Check if new value available */
        do {
        ESP_ERROR_CHECK(iis3dwb_xl_flag_data_ready_get(dev_ctx, &drdy));
        } while (!drdy);

        /* Read data and accumulate the mg value */
        ESP_ERROR_CHECK(iis3dwb_acceleration_raw_get(dev_ctx, data_raw));

        for (j = 0; j < 3; j++) {
        val_st_on[j] += iis3dwb_from_fs4g_to_mg(data_raw[j]);
        }
    }

    /* Calculate the mg average values */
    for (i = 0; i < 3; i++) {
        val_st_on[i] /= 5.0f;
    }

    /* Calculate the mg values for self test */
    for (i = 0; i < 3; i++) {
        test_val[i] = fabsf((val_st_on[i] - val_st_off[i]));
    }

    ESP_LOGI(TAG, "Self-test results:");
    ESP_LOGI(TAG, "  Baseline [mg]: X=%.2f, Y=%.2f, Z=%.2f", val_st_off[0], val_st_off[1], val_st_off[2]);
    ESP_LOGI(TAG, "  Self-test [mg]: X=%.2f, Y=%.2f, Z=%.2f", val_st_on[0], val_st_on[1], val_st_on[2]);
    ESP_LOGI(TAG, "  Difference [mg]: X=%.2f, Y=%.2f, Z=%.2f", test_val[0], test_val[1], test_val[2]);
    ESP_LOGI(TAG, "  Limits [mg]: %.2f - %.2f", MIN_ST_LIMIT_mg, MAX_ST_LIMIT_mg);

    /* Check self test limit */
    *result = (uint8_t)ST_PASS;

    for (i = 0; i < 3; i++) {
        if (( MIN_ST_LIMIT_mg > test_val[i] ) ||
            ( test_val[i] > MAX_ST_LIMIT_mg)) {
        *result = (uint8_t)ST_FAIL;
        ESP_LOGW(TAG, "  Axis %d FAILED: %.2f mg (outside limits)", i, test_val[i]);
        }
    }

    if (*result == ST_PASS) {
        ESP_LOGI(TAG, "Self-test PASSED");
    } else {
        ESP_LOGE(TAG, "Self-test FAILED");
    }

    /* Disable Self Test */
    ESP_ERROR_CHECK(iis3dwb_xl_self_test_set(dev_ctx, IIS3DWB_XL_ST_DISABLE));
    /* Disable sensor. */
    ESP_ERROR_CHECK(iis3dwb_xl_data_rate_set(dev_ctx, IIS3DWB_XL_ODR_OFF));

    ESP_LOGI(TAG, "Self-test completed");
    return ESP_OK;
}

static int32_t platform_write(void *handle, uint8_t reg,
                              const uint8_t *bufp, uint16_t len)
{
    spi_device_handle_t spi = (spi_device_handle_t)handle;
    uint8_t *tx = malloc(len + 1);  // Allocate on heap
    if (!tx) {
        ESP_LOGE(TAG, "Malloc failed for tx buffer");
        return ESP_ERR_NO_MEM;
    }
    tx[0] = reg & 0x7F;  // bit7=0 để ghi
    memcpy(&tx[1], bufp, len);
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    free(tx);  // Free after use
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI write error: %d", ret);
    }
    return ret;
}



static int32_t platform_read(void *handle, uint8_t reg,
                             uint8_t *bufp, uint16_t len)
{
    spi_device_handle_t spi = (spi_device_handle_t)handle;
    uint8_t *tx = malloc(len + 1);
    uint8_t *rx = malloc(len + 1);
    if (!tx || !rx) {
        ESP_LOGE(TAG, "Malloc failed for tx/rx buffer");
        free(tx); free(rx);  // Free if one succeeded
        return ESP_ERR_NO_MEM;
    }
    tx[0] = reg | 0x80;  // bit7=1 để đọc
    memset(&tx[1], 0x00, len);

    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret == ESP_OK) {
        memcpy(bufp, &rx[1], len);
    } else {
        ESP_LOGE(TAG, "SPI read error: %d", ret);
    }
    free(tx);
    free(rx);
    return ret;
}

static void platform_delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}