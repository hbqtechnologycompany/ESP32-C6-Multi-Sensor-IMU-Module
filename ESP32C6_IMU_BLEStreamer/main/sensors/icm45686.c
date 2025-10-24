#include "icm45686.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

static const char *TAG = "ICM45686_C";

#define SPI_READ_BIT (0x80)
#define DEFAULT_SPI_CLOCK_HZ 6000000
#define GYR_STARTUP_TIME_US 5000
#define DEFAULT_WOM_THS_MG (52 >> 2) /* matches Arduino code */

/* single global pointer used by the inv driver callbacks (matches original design) */
static icm456xx_dev_t *icm_dev_ptr = NULL;

/* forward declarations for transport callbacks (matching inv_imu expected prototypes) */
static int transport_spi_write(uint8_t reg, const uint8_t * wbuffer, uint32_t wlen);
static int transport_spi_read(uint8_t reg, uint8_t * rbuffer, uint32_t rlen);
static void transport_sleep_us(uint32_t us);
static void fifo_sensor_event_cb(inv_imu_sensor_event_t *event);

/* ------------ SPI helper -------------- */
static esp_err_t icm456xx_spi_transmit(icm456xx_dev_t *dev, const uint8_t *txbuf, uint8_t *rxbuf, size_t len_bits)
{
    if (!dev || !dev->spi_handle) return ESP_ERR_INVALID_ARG;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len_bits; /* in bits */
    t.tx_buffer = txbuf;
    t.rx_buffer = rxbuf;
    esp_err_t ret = spi_device_transmit(dev->spi_handle, &t);
    return ret;
}

/* transport write: send reg + payload (CS handled by spi_device) */
static int transport_spi_write(uint8_t reg, const uint8_t * wbuffer, uint32_t wlen)
{
    if (!icm_dev_ptr) return -1;
    icm456xx_dev_t *dev = icm_dev_ptr;

    size_t total = 1 + wlen;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_8BIT);
    if (!buf) return -1;
    buf[0] = reg & 0x7F; /* write bit = 0 */
    if (wlen && wbuffer) memcpy(&buf[1], wbuffer, wlen);

    esp_err_t r = icm456xx_spi_transmit(dev, buf, NULL, total * 8);
    heap_caps_free(buf);
    return (r == ESP_OK) ? 0 : -1;
}

/* transport read: send reg|0x80 then read rlen bytes in same transaction */
static int transport_spi_read(uint8_t reg, uint8_t * rbuffer, uint32_t rlen)
{
    if (!icm_dev_ptr) return -1;
    icm456xx_dev_t *dev = icm_dev_ptr;

    size_t total = 1 + rlen;
    uint8_t *tx = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_8BIT);
    uint8_t *rx = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_8BIT);
    if (!tx || !rx) {
        if (tx) heap_caps_free(tx);
        if (rx) heap_caps_free(rx);
        return -1;
    }
    tx[0] = (reg | SPI_READ_BIT);
    memset(&tx[1], 0, rlen);

    esp_err_t r = icm456xx_spi_transmit(dev, tx, rx, total * 8);
    if (r == ESP_OK) {
        /* rx[0] is garbage (response to tx[0]), subsequent bytes are data */
        if (rlen) memcpy(rbuffer, &rx[1], rlen);
    }
    heap_caps_free(tx);
    heap_caps_free(rx);
    return (r == ESP_OK) ? 0 : -1;
}

/* sleep microseconds callback */
static void transport_sleep_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

/* FIFO sensor event callback (used by inv driver to signal GAF outputs) */
static void fifo_sensor_event_cb(inv_imu_sensor_event_t *event)
{
#if defined(ICM45686S) || defined(ICM45605S)
    if (!icm_dev_ptr) return;
    if (event->sensor_mask & (1 << INV_SENSOR_ES0)) {
        icm_dev_ptr->gaf_status = inv_imu_edmp_gaf_build_outputs(&icm_dev_ptr->icm_driver, (const uint8_t *)event->es0, &icm_dev_ptr->gaf_outputs_internal);
    }
#else
    (void)event;
#endif
}

/* ------------ Public API -------------- */

int icm456xx_init_spi(icm456xx_dev_t *dev, spi_host_device_t host, int cs_gpio, uint32_t clk_hz)
{
    if (!dev) return -1;
    memset(dev, 0, sizeof(*dev));
    dev->spi_host = host;
    dev->cs_gpio = cs_gpio;
    dev->clk_hz = (clk_hz == 0) ? DEFAULT_SPI_CLOCK_HZ : clk_hz;
    dev->step_cnt_ovflw = 0;
    for (int i=0;i<5;i++) dev->apex_enable[i] = false;
#if defined(ICM45686S) || defined(ICM45605S)
    dev->gaf_status = 0;
    memset(&dev->gaf_outputs_internal, 0, sizeof(dev->gaf_outputs_internal));
#endif
    return 0;
}

int icm456xx_begin(icm456xx_dev_t *dev)
{
    if (!dev) return -1;

    /* configure spi_device_interface and add device */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = dev->clk_hz,
        .mode = 3, /* SPI_MODE3 to match Arduino code */
        .spics_io_num = dev->cs_gpio,
        .queue_size = 1,
    };

    esp_err_t ret = spi_bus_add_device(dev->spi_host, &devcfg, &dev->spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
        return -1;
    }

    /* register transport callbacks in icm driver */
    dev->icm_driver.transport.serif_type = UI_SPI4;
    dev->icm_driver.transport.read_reg  = transport_spi_read;
    dev->icm_driver.transport.write_reg = transport_spi_write;
    dev->icm_driver.transport.sleep_us = transport_sleep_us;

    /* set FIFO callback */
    ((inv_imu_adv_var_t *)&dev->icm_driver.adv_var)->sensor_event_cb = fifo_sensor_event_cb;

    /* set global pointer used by callbacks */
    icm_dev_ptr = dev;

    /* small delay like original begin */
    transport_sleep_us(3000);

    int rc = inv_imu_adv_init(&dev->icm_driver);
    if (rc != INV_IMU_OK) {
        ESP_LOGE(TAG, "inv_imu_adv_init failed: %d", rc);
        return rc;
    }

    return 0;
}

/* helper mapping functions (same strategy as C++ original) */
static accel_config0_accel_ui_fs_sel_t accel_fsr_g_to_param(uint16_t accel_fsr_g)
{
    accel_config0_accel_ui_fs_sel_t ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_16_G;
    switch(accel_fsr_g) {
    case 2:  ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_2_G;  break;
    case 4:  ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_4_G;  break;
    case 8:  ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_8_G;  break;
    case 16: ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_16_G; break;
#if INV_IMU_HIGH_FSR_SUPPORTED
    case 32: ret = ACCEL_CONFIG0_ACCEL_UI_FS_SEL_32_G; break;
#endif
    default: break;
    }
    return ret;
}

static gyro_config0_gyro_ui_fs_sel_t gyro_fsr_dps_to_param(uint16_t gyro_fsr_dps)
{
    gyro_config0_gyro_ui_fs_sel_t ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_2000_DPS;
    switch(gyro_fsr_dps) {
    case 15:  ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_15_625_DPS;  break;
    case 31:  ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_31_25_DPS;  break;
    case 62:  ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_62_5_DPS;  break;
    case 125: ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_125_DPS;   break;
    case 250: ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_250_DPS;   break;
    case 500: ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_500_DPS;   break;
    case 1000:ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_1000_DPS;  break;
    case 2000:ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_2000_DPS;  break;
#if INV_IMU_HIGH_FSR_SUPPORTED
    case 4000: ret = GYRO_CONFIG0_GYRO_UI_FS_SEL_4000_DPS; break;
#endif
    default: break;
    }
    return ret;
}

static accel_config0_accel_odr_t accel_freq_to_param(uint16_t accel_freq_hz)
{
    accel_config0_accel_odr_t ret = ACCEL_CONFIG0_ACCEL_ODR_100_HZ;
    switch(accel_freq_hz) {
    case 1:    ret = ACCEL_CONFIG0_ACCEL_ODR_1_5625_HZ;  break;
    case 3:    ret = ACCEL_CONFIG0_ACCEL_ODR_3_125_HZ;  break;
    case 6:    ret = ACCEL_CONFIG0_ACCEL_ODR_6_25_HZ;   break;
    case 12:   ret = ACCEL_CONFIG0_ACCEL_ODR_12_5_HZ;   break;
    case 25:   ret = ACCEL_CONFIG0_ACCEL_ODR_25_HZ;     break;
    case 50:   ret = ACCEL_CONFIG0_ACCEL_ODR_50_HZ;     break;
    case 100:  ret = ACCEL_CONFIG0_ACCEL_ODR_100_HZ;    break;
    case 200:  ret = ACCEL_CONFIG0_ACCEL_ODR_200_HZ;    break;
    case 400:  ret = ACCEL_CONFIG0_ACCEL_ODR_400_HZ;    break;
    case 800:  ret = ACCEL_CONFIG0_ACCEL_ODR_800_HZ;    break;
    case 1600: ret = ACCEL_CONFIG0_ACCEL_ODR_1600_HZ;   break;
    case 3200: ret = ACCEL_CONFIG0_ACCEL_ODR_3200_HZ;   break;
    case 6400: ret = ACCEL_CONFIG0_ACCEL_ODR_6400_HZ;   break;
    default: break;
    }
    return ret;
}

static gyro_config0_gyro_odr_t gyro_freq_to_param(uint16_t gyro_freq_hz)
{
    gyro_config0_gyro_odr_t ret = GYRO_CONFIG0_GYRO_ODR_100_HZ;
    switch(gyro_freq_hz) {
    case 1:   ret = GYRO_CONFIG0_GYRO_ODR_1_5625_HZ;  break;
    case 3:   ret = GYRO_CONFIG0_GYRO_ODR_3_125_HZ;  break;
    case 6:   ret = GYRO_CONFIG0_GYRO_ODR_6_25_HZ;   break;
    case 12:  ret = GYRO_CONFIG0_GYRO_ODR_12_5_HZ;   break;
    case 25:  ret = GYRO_CONFIG0_GYRO_ODR_25_HZ;     break;
    case 50:  ret = GYRO_CONFIG0_GYRO_ODR_50_HZ;     break;
    case 100: ret = GYRO_CONFIG0_GYRO_ODR_100_HZ;    break;
    case 200: ret = GYRO_CONFIG0_GYRO_ODR_200_HZ;    break;
    case 400: ret = GYRO_CONFIG0_GYRO_ODR_400_HZ;    break;
    case 800: ret = GYRO_CONFIG0_GYRO_ODR_800_HZ;    break;
    case 1600:ret = GYRO_CONFIG0_GYRO_ODR_1600_HZ;   break;
    case 3200:ret = GYRO_CONFIG0_GYRO_ODR_3200_HZ;   break;
    case 6400:ret = GYRO_CONFIG0_GYRO_ODR_6400_HZ;   break;
    default: break;
    }
    return ret;
}

/* Accel/Gyro start/stop wrappers */
int icm456xx_start_accel(icm456xx_dev_t *dev, uint16_t odr_hz, uint16_t fsr_g)
{
    if (!dev) return -1;
    int rc = 0;
    rc |= inv_imu_set_accel_fsr(&dev->icm_driver, accel_fsr_g_to_param(fsr_g));
    rc |= inv_imu_set_accel_frequency(&dev->icm_driver, accel_freq_to_param(odr_hz));
    rc |= inv_imu_set_accel_mode(&dev->icm_driver, PWR_MGMT0_ACCEL_MODE_LN);
    return rc;
}

int icm456xx_start_gyro(icm456xx_dev_t *dev, uint16_t odr_hz, uint16_t fsr_dps)
{
    if (!dev) return -1;
    int rc = 0;
    rc |= inv_imu_set_gyro_fsr(&dev->icm_driver, gyro_fsr_dps_to_param(fsr_dps));
    rc |= inv_imu_set_gyro_frequency(&dev->icm_driver, gyro_freq_to_param(odr_hz));
    rc |= inv_imu_set_gyro_mode(&dev->icm_driver, PWR_MGMT0_GYRO_MODE_LN);
    return rc;
}

int icm456xx_stop_accel(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    return inv_imu_set_accel_mode(&dev->icm_driver, PWR_MGMT0_ACCEL_MODE_OFF);
}
int icm456xx_stop_gyro(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    return inv_imu_set_gyro_mode(&dev->icm_driver, PWR_MGMT0_GYRO_MODE_OFF);
}

int icm456xx_get_data_from_registers(icm456xx_dev_t *dev, inv_imu_sensor_data_t *data)
{
    if (!dev || !data) return -1;
    return inv_imu_get_register_data(&dev->icm_driver, data);
}

/* Setup IRQ: configure gpio + isr handler (user_isr gets called from ISR context)
   user_isr signature: void (*user_isr)(void*)
*/
static void gpio_isr_trampoline(void* arg)
{
    /* arg is icm456xx_dev_t* and trampoline stored user handler in dev->icm_driver... we store handler pointer via gpio's arg */
    void (*user_handler)(void*) = (void(*)(void*))arg;
    if (user_handler) user_handler(NULL);
}

int icm456xx_enable_fifo_interrupt(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*), uint8_t fifo_watermark)
{
    if (!dev) return -1;
    inv_imu_int_state_t it_conf;
    const inv_imu_fifo_config_t fifo_config = {
        .gyro_en=true,
        .accel_en=true,
        .hires_en=false,
        .fifo_wm_th=fifo_watermark,
        .fifo_mode=FIFO_CONFIG0_FIFO_MODE_SNAPSHOT,
        .fifo_depth=FIFO_CONFIG0_FIFO_DEPTH_MAX
    };

    /* configure FIFO on driver */
    int rc = inv_imu_set_fifo_config(&dev->icm_driver, &fifo_config);

    /* configure gpio irq: input + install isr service if needed */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << int_gpio,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    /* install service once */
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    /* add handler - pass user_isr pointer as arg through gpio */
    gpio_isr_handler_add(int_gpio, (gpio_isr_t)user_isr, NULL);

    /* configure driver interrupts (similar to Arduino code) */
    memset(&it_conf, INV_IMU_DISABLE, sizeof(it_conf));
    it_conf.INV_FIFO_THS = INV_IMU_ENABLE;
    inv_imu_set_config_int(&dev->icm_driver, INV_IMU_INT1, &it_conf);
    inv_imu_set_pin_config_int(&dev->icm_driver, INV_IMU_INT1, &(inv_imu_int_pin_config_t){
        .int_polarity = INTX_CONFIG2_INTX_POLARITY_HIGH,
        .int_mode = INTX_CONFIG2_INTX_MODE_PULSE,
        .int_drive = INTX_CONFIG2_INTX_DRIVE_PP
    });

    return rc;
}

int icm456xx_get_data_from_fifo(icm456xx_dev_t *dev, inv_imu_fifo_data_t *data)
{
    if (!dev || !data) return -1;
    return inv_imu_get_fifo_frame(&dev->icm_driver, data);
}

/* APEX/GAF wrappers (partial port of original logic) */
#if defined(ICM45686S) || defined(ICM45605S)
int icm456xx_start_gaf(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    int rc = 0;
    inv_imu_edmp_gaf_parameters_t gaf_params;
    inv_imu_edmp_int_state_t apex_int_config;
    const inv_imu_adv_fifo_config_t fifo_config = {
        .base_conf = {
            .gyro_en    = INV_IMU_DISABLE,
            .accel_en   = INV_IMU_DISABLE,
            .hires_en   = INV_IMU_DISABLE,
            .fifo_wm_th = 4,
            .fifo_mode = FIFO_CONFIG0_FIFO_MODE_SNAPSHOT,
            .fifo_depth = FIFO_CONFIG0_FIFO_DEPTH_GAF,
        },
        .fifo_wr_wm_gt_th     = FIFO_CONFIG2_FIFO_WR_WM_EQ_OR_GT_TH,
        .tmst_fsync_en        = INV_IMU_DISABLE,
        .es1_en               = INV_IMU_DISABLE,
        .es0_en               = INV_IMU_ENABLE,
        .es0_6b_9b            = FIFO_CONFIG4_FIFO_ES0_9B,
        .comp_en              = INV_IMU_DISABLE,
        .comp_nc_flow_cfg     = FIFO_CONFIG4_FIFO_COMP_NC_FLOW_CFG_DIS,
        .gyro_dec             = ODR_DECIMATE_CONFIG_GYRO_FIFO_ODR_DEC_1,
        .accel_dec            = ODR_DECIMATE_CONFIG_ACCEL_FIFO_ODR_DEC_1
    };

    icm456xx_stop_accel(dev);
    icm456xx_stop_gyro(dev);

    rc |= inv_imu_edmp_set_frequency(&dev->icm_driver, DMP_EXT_SEN_ODR_CFG_APEX_ODR_100_HZ);
    rc |= inv_imu_edmp_gaf_init(&dev->icm_driver);

    rc |= inv_imu_edmp_gaf_init_parameters(&dev->icm_driver, &gaf_params);
    gaf_params.pdr_us = 10000;
    rc |= inv_imu_edmp_gaf_set_parameters(&dev->icm_driver, &gaf_params);
    if (rc != 0) return rc;

    rc |= icm456xx_start_accel(dev, 100, 16);
    rc |= icm456xx_start_gyro(dev, 100, 2000);
    transport_sleep_us(GYR_STARTUP_TIME_US);

    rc |= inv_imu_adv_set_fifo_config(&dev->icm_driver, &fifo_config);
    /* configure int gpio similarly to start_gaf expectations */
    /* add user gpio isr */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << int_gpio,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    static bool isr_service_installed = false;
    if (!isr_service_installed) { gpio_install_isr_service(0); isr_service_installed = true; }
    gpio_isr_handler_add(int_gpio, (gpio_isr_t)user_isr, NULL);

    rc |= inv_imu_edmp_gaf_enable(&dev->icm_driver);
    rc |= inv_imu_edmp_enable(&dev->icm_driver);
    return rc;
}

int icm456xx_get_gaf_data(icm456xx_dev_t *dev, inv_imu_edmp_gaf_outputs_t *out)
{
    if (!dev || !out) return -1;
    int rc = 0;
    uint16_t fifo_count;
    uint8_t fifo_data[FIFO_MIRRORING_SIZE];
    uint8_t count = 0;
    while ((dev->gaf_status != 1) && (rc == 0) && (count++ < 100)) {
        rc |= inv_imu_adv_get_data_from_fifo(&dev->icm_driver, fifo_data, &fifo_count);
        rc |= inv_imu_adv_parse_fifo_data(&dev->icm_driver, fifo_data, fifo_count);
    }
    if (dev->gaf_status == 1) {
        memcpy(out, &dev->gaf_outputs_internal, sizeof(inv_imu_edmp_gaf_outputs_t));
        dev->gaf_status = 0;
        return 0;
    } else {
        memset(out, 0, sizeof(inv_imu_edmp_gaf_outputs_t));
        return -1;
    }
}

int icm456xx_get_gaf_quat(icm456xx_dev_t *dev, float *w, float *x, float *y, float *z)
{
    if (!dev || !w || !x || !y || !z) return -1;
    inv_imu_edmp_gaf_outputs_t gaf_out;
    int rc = icm456xx_get_gaf_data(dev, &gaf_out);
    if (rc == 0) {
        const float divider = (float)(1ULL << 30);
        *w = (float)gaf_out.grv_quat_q30[0] / divider;
        *x = (float)gaf_out.grv_quat_q30[1] / divider;
        *y = (float)gaf_out.grv_quat_q30[2] / divider;
        *z = (float)gaf_out.grv_quat_q30[3] / divider;
    } else {
        *w=*x=*y=*z=0.0f;
    }
    return rc;
}
#endif /* GAF */

/* APEX convenience functions: mostly wrappers of the original logic */
int icm456xx_set_apex_interrupt(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    inv_imu_int_state_t config_int;
    inv_imu_int_pin_config_t int_pin_config;
    inv_imu_edmp_int_state_t apex_int_config;

    if (!user_isr) return 0;
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << int_gpio,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    static bool isr_service_installed = false;
    if (!isr_service_installed) { gpio_install_isr_service(0); isr_service_installed = true; }
    gpio_isr_handler_add(int_gpio, (gpio_isr_t)user_isr, NULL);

    int_pin_config.int_polarity = INTX_CONFIG2_INTX_POLARITY_HIGH;
    int_pin_config.int_mode = INTX_CONFIG2_INTX_MODE_PULSE;
    int_pin_config.int_drive = INTX_CONFIG2_INTX_DRIVE_PP;
    inv_imu_set_pin_config_int(&dev->icm_driver, INV_IMU_INT1, &int_pin_config);

    inv_imu_get_config_int(&dev->icm_driver, INV_IMU_INT1, &config_int);
    config_int.INV_WOM_X = INV_IMU_DISABLE;
    config_int.INV_WOM_Y = INV_IMU_DISABLE;
    config_int.INV_WOM_Z = INV_IMU_DISABLE;
    config_int.INV_FIFO_THS = INV_IMU_DISABLE;
    config_int.INV_EDMP_EVENT = INV_IMU_ENABLE;
    inv_imu_set_config_int(&dev->icm_driver, INV_IMU_INT1, &config_int);

    memset(&apex_int_config, INV_IMU_DISABLE, sizeof(apex_int_config));
    apex_int_config.INV_TAP = INV_IMU_ENABLE;
    apex_int_config.INV_TILT_DET = INV_IMU_ENABLE;
    apex_int_config.INV_STEP_DET = INV_IMU_ENABLE;
    apex_int_config.INV_STEP_CNT_OVFL = INV_IMU_ENABLE;
    apex_int_config.INV_R2W = INV_IMU_ENABLE;
    apex_int_config.INV_R2W_SLEEP = INV_IMU_ENABLE;
    inv_imu_edmp_set_config_int_apex(&dev->icm_driver, &apex_int_config);

    return 0;
}

int icm456xx_start_apex(icm456xx_dev_t *dev, dmp_ext_sen_odr_cfg_apex_odr_t edmp_odr, accel_config0_accel_odr_t accel_odr)
{
    if (!dev) return -1;
    int rc = 0;
    inv_imu_edmp_apex_parameters_t apex_parameters;

    rc |= inv_imu_edmp_init_apex(&dev->icm_driver);

    /* Keep the highest requested ODR like original logic */
    /* apex_edmp_odr is stored in driver adv_var? but we keep simple: set frequency */
    rc |= inv_imu_edmp_set_frequency(&dev->icm_driver, edmp_odr);
    rc |= inv_imu_set_accel_frequency(&dev->icm_driver, accel_odr);

    rc |= inv_imu_set_accel_ln_bw(&dev->icm_driver, IPREG_SYS2_REG_131_ACCEL_UI_LPFBW_DIV_4);
    rc |= inv_imu_select_accel_lp_clk(&dev->icm_driver, SMC_CONTROL_0_ACCEL_LP_CLK_WUOSC);
    rc |= inv_imu_set_accel_lp_avg(&dev->icm_driver, IPREG_SYS2_REG_129_ACCEL_LP_AVG_1);

    /* disable APEX */
    rc |= inv_imu_edmp_disable_pedometer(&dev->icm_driver);
    rc |= inv_imu_edmp_disable_tilt(&dev->icm_driver);
    rc |= inv_imu_edmp_disable_tap(&dev->icm_driver);
    rc |= inv_imu_adv_disable_wom(&dev->icm_driver);
    rc |= inv_imu_edmp_disable_r2w(&dev->icm_driver);
    rc |= inv_imu_edmp_disable_ff(&dev->icm_driver);
    rc |= inv_imu_edmp_disable(&dev->icm_driver);

    rc |= inv_imu_edmp_recompute_apex_decimation(&dev->icm_driver);

    rc |= inv_imu_edmp_get_apex_parameters(&dev->icm_driver, &apex_parameters);
    apex_parameters.power_save_en = 0;
    rc |= inv_imu_edmp_set_apex_parameters(&dev->icm_driver, &apex_parameters);

    rc |= inv_imu_set_accel_mode(&dev->icm_driver, PWR_MGMT0_ACCEL_MODE_LN);

    if (dev->apex_enable[ICM456XX_APEX_TILT]) rc |= inv_imu_edmp_enable_tilt(&dev->icm_driver);
    if (dev->apex_enable[ICM456XX_APEX_PEDOMETER]) rc |= inv_imu_edmp_enable_pedometer(&dev->icm_driver);
    if (dev->apex_enable[ICM456XX_APEX_TAP]) {
        rc |= inv_imu_edmp_get_apex_parameters(&dev->icm_driver, &apex_parameters);
        apex_parameters.tap_tmax = TAP_TMAX_400HZ;
        apex_parameters.tap_tmin = TAP_TMIN_400HZ;
        apex_parameters.tap_smudge_reject_th = TAP_SMUDGE_REJECT_THR_400HZ;
        rc |= inv_imu_edmp_set_apex_parameters(&dev->icm_driver, &apex_parameters);
        rc |= inv_imu_edmp_enable_tap(&dev->icm_driver);
    }
    if (dev->apex_enable[ICM456XX_APEX_R2W]) {
        rc |= inv_imu_edmp_get_apex_parameters(&dev->icm_driver, &apex_parameters);
        apex_parameters.r2w_sleep_time_out = 6400;
        rc |= inv_imu_edmp_set_apex_parameters(&dev->icm_driver, &apex_parameters);
        rc |= inv_imu_edmp_enable_r2w(&dev->icm_driver);
    }

    rc |= inv_imu_edmp_enable(&dev->icm_driver);
    rc |= inv_imu_adv_enable_accel_ln(&dev->icm_driver);

    return rc;
}

/* wrappers that mirror original API names */
int icm456xx_start_tilt_detection(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    dev->apex_enable[ICM456XX_APEX_TILT] = true;
    icm456xx_set_apex_interrupt(dev, int_gpio, user_isr);
    return icm456xx_start_apex(dev, DMP_EXT_SEN_ODR_CFG_APEX_ODR_50_HZ, ACCEL_CONFIG0_ACCEL_ODR_50_HZ);
}

int icm456xx_start_pedometer(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    dev->apex_enable[ICM456XX_APEX_PEDOMETER] = true;
    dev->step_cnt_ovflw = 0;
    icm456xx_set_apex_interrupt(dev, int_gpio, user_isr);
    return icm456xx_start_apex(dev, DMP_EXT_SEN_ODR_CFG_APEX_ODR_50_HZ, ACCEL_CONFIG0_ACCEL_ODR_50_HZ);
}

int icm456xx_get_pedometer(icm456xx_dev_t *dev, uint32_t *step_count, float *step_cadence, char **activity)
{
    if (!dev || !step_count || !step_cadence || !activity) return -1;
    int rc = 0;
    /* read interrupts and apex status similar to original */
    inv_imu_int_state_t int_state;
    inv_imu_get_int_status(&dev->icm_driver, INV_IMU_INT1, &int_state);

    if (int_state.INV_EDMP_EVENT) {
        inv_imu_edmp_int_state_t apex_state;
        inv_imu_edmp_get_int_apex_status(&dev->icm_driver, &apex_state);
        dev->apex_status.INV_STEP_CNT_OVFL |= apex_state.INV_STEP_CNT_OVFL;
        dev->apex_status.INV_STEP_DET |= apex_state.INV_STEP_DET;
        dev->apex_status.INV_TAP |= apex_state.INV_TAP;
        dev->apex_status.INV_TILT_DET |= apex_state.INV_TILT_DET;
        dev->apex_status.INV_R2W |= apex_state.INV_R2W;
        dev->apex_status.INV_R2W_SLEEP |= apex_state.INV_R2W_SLEEP;
    }

    if (dev->apex_status.INV_STEP_CNT_OVFL) {
        dev->apex_status.INV_STEP_CNT_OVFL = 0;
        dev->step_cnt_ovflw++;
    }
    if (dev->apex_status.INV_STEP_DET) {
        inv_imu_edmp_pedometer_data_t ped_data;
        dev->apex_status.INV_STEP_DET = 0;
        rc |= inv_imu_edmp_get_pedometer_data(&dev->icm_driver, &ped_data);
        if (rc == INV_IMU_OK) {
            *step_count = (uint32_t)ped_data.step_cnt + (dev->step_cnt_ovflw * UINT16_MAX);
            *step_cadence = (ped_data.step_cadence != 0) ? (200.0f / (float)ped_data.step_cadence) : 0.0f;
            if (ped_data.activity_class == INV_IMU_EDMP_RUN) *activity = "Run";
            else if (ped_data.activity_class == INV_IMU_EDMP_WALK) *activity = "Walk";
            else *activity = "Unknown";
            return 0;
        } else return -13;
    } else return -12;
}

int icm456xx_start_wom(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    icm456xx_set_apex_interrupt(dev, int_gpio, user_isr);
    inv_imu_int_state_t config_int;
    inv_imu_get_config_int(&dev->icm_driver, INV_IMU_INT1, &config_int);
    config_int.INV_WOM_X = INV_IMU_ENABLE;
    config_int.INV_WOM_Y = INV_IMU_ENABLE;
    config_int.INV_WOM_Z = INV_IMU_ENABLE;
    config_int.INV_EDMP_EVENT = INV_IMU_DISABLE;
    inv_imu_set_config_int(&dev->icm_driver, INV_IMU_INT1, &config_int);

    for (int i=0;i<ICM456XX_APEX_NB;i++) dev->apex_enable[i] = false;

    int rc = icm456xx_start_apex(dev, DMP_EXT_SEN_ODR_CFG_APEX_ODR_50_HZ, ACCEL_CONFIG0_ACCEL_ODR_50_HZ);
    rc |= inv_imu_adv_configure_wom(&dev->icm_driver, DEFAULT_WOM_THS_MG, DEFAULT_WOM_THS_MG, DEFAULT_WOM_THS_MG,
                                   TMST_WOM_CONFIG_WOM_INT_MODE_ANDED, TMST_WOM_CONFIG_WOM_INT_DUR_1_SMPL);
    rc |= inv_imu_adv_enable_wom(&dev->icm_driver);
    rc |= inv_imu_edmp_enable(&dev->icm_driver);
    rc |= inv_imu_adv_enable_accel_ln(&dev->icm_driver);
    return rc;
}

int icm456xx_start_tap(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    dev->apex_enable[ICM456XX_APEX_TAP] = true;
    dev->step_cnt_ovflw = 0;
    icm456xx_set_apex_interrupt(dev, int_gpio, user_isr);
    return icm456xx_start_apex(dev, DMP_EXT_SEN_ODR_CFG_APEX_ODR_400_HZ, ACCEL_CONFIG0_ACCEL_ODR_400_HZ);
}

int icm456xx_start_raise_to_wake(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*))
{
    if (!dev) return -1;
    dev->apex_enable[ICM456XX_APEX_R2W] = true;
    icm456xx_set_apex_interrupt(dev, int_gpio, user_isr);
    return icm456xx_start_apex(dev, DMP_EXT_SEN_ODR_CFG_APEX_ODR_100_HZ, ACCEL_CONFIG0_ACCEL_ODR_100_HZ);
}

int icm456xx_update_apex(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    inv_imu_int_state_t int_state;
    inv_imu_edmp_int_state_t apex_state = {0};
    int rc = inv_imu_get_int_status(&dev->icm_driver, INV_IMU_INT1, &int_state);
    if (int_state.INV_EDMP_EVENT) {
        rc |= inv_imu_edmp_get_int_apex_status(&dev->icm_driver, &apex_state);
        dev->apex_status.INV_STEP_CNT_OVFL |= apex_state.INV_STEP_CNT_OVFL;
        dev->apex_status.INV_STEP_DET |= apex_state.INV_STEP_DET;
        dev->apex_status.INV_TAP |= apex_state.INV_TAP;
        dev->apex_status.INV_TILT_DET |= apex_state.INV_TILT_DET;
        dev->apex_status.INV_R2W |= apex_state.INV_R2W;
        dev->apex_status.INV_R2W_SLEEP |= apex_state.INV_R2W_SLEEP;
    }
    return rc;
}

int icm456xx_get_tilt(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    icm456xx_update_apex(dev);
    if (dev->apex_status.INV_TILT_DET) {
        dev->apex_status.INV_TILT_DET = 0;
        return 1;
    }
    return 0;
}

int icm456xx_get_tap(icm456xx_dev_t *dev, uint8_t *tap_count, uint8_t *axis, uint8_t *direction)
{
    if (!dev) return -1;
    icm456xx_update_apex(dev);
    if (dev->apex_status.INV_TAP) {
        inv_imu_edmp_tap_data_t tap_data;
        dev->apex_status.INV_TAP = 0;
        int rc = inv_imu_edmp_get_tap_data(&dev->icm_driver, &tap_data);
        if (rc == INV_IMU_OK) {
            *tap_count = tap_data.num;
            *axis = tap_data.axis;
            *direction = tap_data.direction;
            return 0;
        }
        return -13;
    } else {
        return -13;
    }
}

int icm456xx_get_raise_to_wake(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    icm456xx_update_apex(dev);
    if (dev->apex_status.INV_R2W) {
        dev->apex_status.INV_R2W = 0;
        return 1;
    } else if (dev->apex_status.INV_R2W_SLEEP) {
        dev->apex_status.INV_R2W_SLEEP = 0;
        return 0;
    } else {
        return -12;
    }
}

/* deinit: remove spi device */
int icm456xx_deinit(icm456xx_dev_t *dev)
{
    if (!dev) return -1;
    if (dev->spi_handle) {
        spi_bus_remove_device(dev->spi_handle);
        dev->spi_handle = NULL;
    }
    if (icm_dev_ptr == dev) icm_dev_ptr = NULL;
    return 0;
}
