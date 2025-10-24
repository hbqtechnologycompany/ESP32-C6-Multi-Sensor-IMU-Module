#include "imu_manager.h"
#include "sensors/iis2mdc.h"
#include "sensors/iis3dwb.h"
#include "sensors/icm45686.h"
#include "sensors/scl3300.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "IMU_MANAGER";

// Sensor handles
static iis2mdc_handle_t mag_sensor;
static iis3dwb_handle_t accel_sensor;
static icm456xx_dev_t imu_6axis_sensor;
static scl3300_t inclinometer_sensor;

// Configuration
static uint32_t sampling_rate_hz = 100;
static uint16_t fifo_watermark = 32;
static uint8_t enabled_sensors = 0x00; // Start with all sensors disabled, enable after successful init

// Synchronization
static SemaphoreHandle_t sensor_mutex = NULL;

// GPIO Configuration
#define I2C_MASTER_BUS          I2C_NUM_0
#define I2C_MASTER_SDA          23
#define I2C_MASTER_SCL          22
#define I2C_MASTER_CLK_SPEED    400000

// All SPI sensors share the same bus (SPI2_HOST) with same MISO/MOSI/CLK
#define SPI_HOST_1              SPI2_HOST
#define SPI_HOST_2              SPI2_HOST
#define SPI_HOST_3              SPI2_HOST

// Shared SPI pins (MISO, MOSI, CLK must be same for all devices on same bus)
#define PIN_NUM_MISO            2
#define PIN_NUM_MOSI            7
#define PIN_NUM_CLK             6

// Individual CS pins for each sensor
#define PIN_NUM_CS_IIS3DWB      19      // IIS3DWB Accelerometer
#define PIN_NUM_CS_ICM45686     20      // ICM45686 IMU 6-axis
#define PIN_NUM_CS_SCL3300      11      // SCL3300 Inclinometer

#define SPI_CLOCK_HZ            6000000

esp_err_t imu_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing IMU Manager...");
    
    // Create mutex for thread safety
    sensor_mutex = xSemaphoreCreateMutex();
    if (sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        return ESP_FAIL;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Initialize SPI bus (shared by all SPI sensors)
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    
    ret = spi_bus_initialize(SPI_HOST_1, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully (MISO=%d, MOSI=%d, CLK=%d)", 
             PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK);
    
    // Initialize IIS2MDC (Magnetometer)
    ret = iis2mdc_init(&mag_sensor, I2C_MASTER_BUS, I2C_MASTER_SDA, 
                      I2C_MASTER_SCL, I2C_MASTER_CLK_SPEED);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IIS2MDC not detected: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "IIS2MDC initialized successfully");
        enabled_sensors |= SENSOR_MAGNETOMETER;
    }
    
    // Initialize IIS3DWB (Accelerometer)
    ret = iis3dwb_init_spi(&accel_sensor, SPI_HOST_1, PIN_NUM_CS_IIS3DWB);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IIS3DWB SPI init skipped: %s", esp_err_to_name(ret));
    } else {
        ret = iis3dwb_device_init(&accel_sensor);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "IIS3DWB device init failed: %s", esp_err_to_name(ret));
        } else {
            ret = iis3dwb_configure(&accel_sensor, IIS3DWB_FS_2G, IIS3DWB_ODR_26K7HZ);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "IIS3DWB configuration failed: %s", esp_err_to_name(ret));
            } else {
                ret = iis3dwb_fifo_config(&accel_sensor, fifo_watermark, 0x06);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "IIS3DWB FIFO configuration failed: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "IIS3DWB initialized successfully");
                    enabled_sensors |= SENSOR_ACCELEROMETER;
                }
            }
        }
    }
    
    // Initialize ICM45686 (IMU 6-axis)
    ret = icm456xx_init_spi(&imu_6axis_sensor, SPI_HOST_2, PIN_NUM_CS_ICM45686, SPI_CLOCK_HZ);
    if (ret != 0) {
        ESP_LOGW(TAG, "ICM45686 not detected: %d", ret);
    } else {
        ret = icm456xx_begin(&imu_6axis_sensor);
        if (ret != 0) {
            ESP_LOGW(TAG, "ICM45686 begin failed: %d", ret);
        } else {
            icm456xx_start_accel(&imu_6axis_sensor, sampling_rate_hz, 16);
            icm456xx_start_gyro(&imu_6axis_sensor, sampling_rate_hz, 2000);
            // Disable FIFO interrupt for now (pass GPIO_NUM_NC = -1 is causing error)
            // ret = icm456xx_enable_fifo_interrupt(&imu_6axis_sensor, PIN_NUM_INT_ICM45686, NULL, fifo_watermark);
            // if (ret != 0) {
            //     ESP_LOGW(TAG, "ICM45686 FIFO interrupt setup returned %d", ret);
            // }
            ESP_LOGI(TAG, "ICM45686 initialized successfully");
            enabled_sensors |= SENSOR_IMU_6AXIS;
        }
    }
    
    // Initialize SCL3300 (Inclinometer)
    ret = scl3300_init(SPI_HOST_3, PIN_NUM_CS_SCL3300, &inclinometer_sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SCL3300 not detected: %s", esp_err_to_name(ret));
    } else {
        ret = scl3300_set_mode(&inclinometer_sensor, 1); // Mode 1 for high accuracy
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SCL3300 mode configuration failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "SCL3300 initialized successfully");
            enabled_sensors |= SENSOR_INCLINOMETER;
        }
    }
    
    ESP_LOGI(TAG, "IMU Manager initialized. Enabled sensors: 0x%02X", enabled_sensors);
    return ESP_OK;
}

esp_err_t imu_manager_read_all(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Set timestamp
    data->timestamp_us = esp_timer_get_time();
    
    // Read all enabled sensors
    if (enabled_sensors & SENSOR_MAGNETOMETER) {
        imu_manager_read_magnetometer(data);
    }
    
    if (enabled_sensors & SENSOR_ACCELEROMETER) {
        imu_manager_read_accelerometer(data);
    }
    
    if (enabled_sensors & SENSOR_IMU_6AXIS) {
        imu_manager_read_imu_6axis(data);
    }
    
    if (enabled_sensors & SENSOR_INCLINOMETER) {
        imu_manager_read_inclinometer(data);
    }
    
    xSemaphoreGive(sensor_mutex);
    return ESP_OK;
}

esp_err_t imu_manager_read_magnetometer(imu_data_t *data)
{
    if (!(enabled_sensors & SENSOR_MAGNETOMETER)) {
        data->magnetometer.valid = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    iis2mdc_raw_magnetometer_t raw_mag;
    int16_t temp_raw;
    
    esp_err_t ret = iis2mdc_read_magnetic_raw(&mag_sensor, &raw_mag);
    if (ret == ESP_OK) {
        iis2mdc_convert_magnetic_raw_to_mg(&raw_mag, 
                                         &data->magnetometer.x_mg,
                                         &data->magnetometer.y_mg,
                                         &data->magnetometer.z_mg);
        
        ret = iis2mdc_read_temperature_raw(&mag_sensor, &temp_raw);
        if (ret == ESP_OK) {
            iis2mdc_convert_temperature_raw_to_celsius(temp_raw, &data->magnetometer.temperature_c);
        }
        
        data->magnetometer.valid = true;
    } else {
        data->magnetometer.valid = false;
    }
    
    return ret;
}

esp_err_t imu_manager_read_accelerometer(imu_data_t *data)
{
    if (!(enabled_sensors & SENSOR_ACCELEROMETER)) {
        data->accelerometer.valid = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    
    esp_err_t ret = iis3dwb_read_accel(&accel_sensor, &ax, &ay, &az);
    if (ret == ESP_OK) {
        data->accelerometer.x_g = ax;
        data->accelerometer.y_g = ay;
        data->accelerometer.z_g = az;
        data->accelerometer.valid = true;
    } else {
        data->accelerometer.valid = false;
    }
    
    return ret;
}

esp_err_t imu_manager_read_imu_6axis(imu_data_t *data)
{
    if (!(enabled_sensors & SENSOR_IMU_6AXIS)) {
        data->imu_6axis.valid = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    inv_imu_sensor_data_t sensor_data;
    int ret = icm456xx_get_data_from_registers(&imu_6axis_sensor, &sensor_data);
    
    if (ret == 0) {
        // Convert raw data to engineering units
        // Assuming 16-bit data and ±16g range for accel, ±2000dps for gyro
        const float accel_scale = 16.0f / 32768.0f;  // ±16g
        const float gyro_scale = 2000.0f / 32768.0f; // ±2000dps
        
        data->imu_6axis.accel_x_g = sensor_data.accel_data[0] * accel_scale;
        data->imu_6axis.accel_y_g = sensor_data.accel_data[1] * accel_scale;
        data->imu_6axis.accel_z_g = sensor_data.accel_data[2] * accel_scale;
        
        data->imu_6axis.gyro_x_dps = sensor_data.gyro_data[0] * gyro_scale;
        data->imu_6axis.gyro_y_dps = sensor_data.gyro_data[1] * gyro_scale;
        data->imu_6axis.gyro_z_dps = sensor_data.gyro_data[2] * gyro_scale;
        
        // Temperature conversion (assuming 8-bit temp)
        data->imu_6axis.temperature_c = sensor_data.temp_data + 25.0f;
        
        data->imu_6axis.valid = true;
        return ESP_OK;
    } else {
        data->imu_6axis.valid = false;
        return ESP_FAIL;
    }
}

esp_err_t imu_manager_read_inclinometer(imu_data_t *data)
{
    if (!(enabled_sensors & SENSOR_INCLINOMETER)) {
        data->inclinometer.valid = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    esp_err_t ret = scl3300_available(&inclinometer_sensor);
    if (ret == ESP_OK) {
        data->inclinometer.angle_x_deg = scl3300_get_angle_x(&inclinometer_sensor);
        data->inclinometer.angle_y_deg = scl3300_get_angle_y(&inclinometer_sensor);
        data->inclinometer.angle_z_deg = scl3300_get_angle_z(&inclinometer_sensor);
        
        data->inclinometer.accel_x_g = scl3300_get_accel_x(&inclinometer_sensor);
        data->inclinometer.accel_y_g = scl3300_get_accel_y(&inclinometer_sensor);
        data->inclinometer.accel_z_g = scl3300_get_accel_z(&inclinometer_sensor);
        
        data->inclinometer.temperature_c = scl3300_get_temp_c(&inclinometer_sensor);
        data->inclinometer.valid = true;
    } else {
        data->inclinometer.valid = false;
    }
    
    return ret;
}

esp_err_t imu_manager_set_sampling_rate(uint32_t rate_hz)
{
    sampling_rate_hz = rate_hz;
    ESP_LOGI(TAG, "Sampling rate set to %lu Hz", rate_hz);
    return ESP_OK;
}

esp_err_t imu_manager_set_fifo_watermark(uint16_t watermark)
{
    fifo_watermark = watermark;
    ESP_LOGI(TAG, "FIFO watermark set to %u", watermark);
    return ESP_OK;
}

esp_err_t imu_manager_enable_sensor(uint8_t sensor_id, bool enable)
{
    if (enable) {
        enabled_sensors |= sensor_id;
    } else {
        enabled_sensors &= ~sensor_id;
    }
    
    ESP_LOGI(TAG, "Sensor 0x%02X %s", sensor_id, enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t imu_manager_deinit(void)
{
    if (sensor_mutex) {
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "IMU Manager deinitialized");
    return ESP_OK;
}
