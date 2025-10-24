#ifndef ICM45686_C_H
#define ICM45686_C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

/* InvenSense C driver headers (đã có trong thư mục imu/) */
#include "../imu/inv_imu_driver_advanced.h"
#include "../imu/inv_imu_edmp.h"
#if defined(ICM45686S) || defined(ICM45605S)
#include "../imu/inv_imu_edmp_gaf.h"
#endif

/* Public opaque device handle */
typedef struct icm456xx_dev_t {
    inv_imu_device_t icm_driver;          /* driver instance (from inv_imu driver) */
    spi_device_handle_t spi_handle;       /* handle spi device (ESP-IDF) */
    spi_host_device_t spi_host;           /* SPI peripheral used */
    int cs_gpio;                          /* chip select gpio */
    uint32_t clk_hz;                      /* spi clock */
    /* internal state */
    uint32_t step_cnt_ovflw;
    bool apex_enable[5];
#if defined(ICM45686S) || defined(ICM45605S)
    inv_imu_edmp_gaf_outputs_t gaf_outputs_internal;
    int gaf_status;
#endif
    inv_imu_edmp_int_state_t apex_status;
} icm456xx_dev_t;

/* APEX indices used internally for apex_enable[] */
#ifndef ICM456XX_APEX_ENUMS
#define ICM456XX_APEX_ENUMS

enum {
    ICM456XX_APEX_TILT = 0,
    ICM456XX_APEX_PEDOMETER = 1,
    ICM456XX_APEX_TAP = 2,
    ICM456XX_APEX_R2W = 3,
    /* thêm nếu cần các APEX khác */
    ICM456XX_APEX_NB = 4
};

#endif /* ICM456XX_APEX_ENUMS */


/* Init device structure and add spi device to bus.
   - host: SPI2_HOST / SPI3_HOST etc
   - cs_gpio: GPIO number for CS
   - clk_hz: desired SPI clock (e.g. 6M, 12M)
*/
int icm456xx_init_spi(icm456xx_dev_t *dev, spi_host_device_t host, int cs_gpio, uint32_t clk_hz);

/* Starts communication / init internal driver */
int icm456xx_begin(icm456xx_dev_t *dev);

/* Accel/Gyro control */
int icm456xx_start_accel(icm456xx_dev_t *dev, uint16_t odr_hz, uint16_t fsr_g);
int icm456xx_start_gyro(icm456xx_dev_t *dev, uint16_t odr_hz, uint16_t fsr_dps);
int icm456xx_stop_accel(icm456xx_dev_t *dev);
int icm456xx_stop_gyro(icm456xx_dev_t *dev);

/* Read registers (wrapper to inv driver) */
int icm456xx_get_data_from_registers(icm456xx_dev_t *dev, inv_imu_sensor_data_t *data);

/* FIFO functions */
int icm456xx_enable_fifo_interrupt(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*), uint8_t fifo_watermark);
int icm456xx_get_data_from_fifo(icm456xx_dev_t *dev, inv_imu_fifo_data_t *data);

/* APEX / GAF (only available when compiled with appropriate defines) */
#if defined(ICM45686S) || defined(ICM45605S)
int icm456xx_start_gaf(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_get_gaf_data(icm456xx_dev_t *dev, inv_imu_edmp_gaf_outputs_t *out);
int icm456xx_get_gaf_quat(icm456xx_dev_t *dev, float *w, float *x, float *y, float *z);
#endif

/* APEX features */
int icm456xx_start_tilt_detection(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_start_pedometer(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_get_pedometer(icm456xx_dev_t *dev, uint32_t *step_count, float *step_cadence, char **activity);
int icm456xx_start_wom(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_start_tap(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_start_raise_to_wake(icm456xx_dev_t *dev, int int_gpio, void (*user_isr)(void*));
int icm456xx_get_tilt(icm456xx_dev_t *dev);
int icm456xx_get_tap(icm456xx_dev_t *dev, uint8_t *tap_count, uint8_t *axis, uint8_t *direction);
int icm456xx_get_raise_to_wake(icm456xx_dev_t *dev);

/* utility: deinit (remove spi device) */
int icm456xx_deinit(icm456xx_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* ICM45686_C_H */
