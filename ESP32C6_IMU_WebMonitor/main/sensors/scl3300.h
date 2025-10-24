#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ==== Command definitions (Murata SCL3300 datasheet) ====
#define RdAccX      0x040000f7
#define RdAccY      0x080000fd
#define RdAccZ      0x0c0000fb
#define RdSTO       0x100000e9
#define EnaAngOut   0xb0001f6f
#define RdAngX      0x240000c7
#define RdAngY      0x280000cd
#define RdAngZ      0x2c0000cb
#define RdTemp      0x140000ef
#define RdStatSum   0x180000e5
#define RdErrFlg1   0x1c0000e3
#define RdErrFlg2   0x200000c1
#define RdCMD       0x340000df
#define ChgMode1    0xb400001f
#define ChgMode2    0xb4000102
#define ChgMode3    0xb4000225
#define ChgMode4    0xb4000338
#define SetPwrDwn   0xb400046b
#define WakeUp      0xb400001f
#define SWreset     0xb4002098
#define RdWHOAMI    0x40000091
#define RdSer1      0x640000a7
#define RdSer2      0x680000ad
#define RdCurBank   0x7c0000b3
#define SwtchBnk0   0xfc000073
#define SwtchBnk1   0xfc00016e


#define SCL3300_NOP   0x00000000

// === Data structure for raw readings ===
typedef struct {
    int16_t AccX;
    int16_t AccY;
    int16_t AccZ;
    int16_t STO;
    int16_t TEMP;
    int16_t AngX;
    int16_t AngY;
    int16_t AngZ;
    uint16_t StatusSum;
    uint16_t WHOAMI;
} scl3300_data_t;

// === Device context ===
typedef struct {
    spi_device_handle_t spi;
    gpio_num_t cs_pin;
    uint8_t mode;       // 1..4
    bool fast_read;
    bool crcerr;
    bool statuserr;
    uint16_t last_data;
    uint8_t last_cmd;
    uint8_t last_crc;
    scl3300_data_t data;
} scl3300_t;

// === API ===
esp_err_t scl3300_init(spi_host_device_t host, gpio_num_t cs_pin, scl3300_t *dev);
esp_err_t scl3300_set_mode(scl3300_t *dev, uint8_t mode);
esp_err_t scl3300_available(scl3300_t *dev);   // read all data
bool      scl3300_is_connected(scl3300_t *dev);

uint16_t  scl3300_get_errflag1(scl3300_t *dev);
uint16_t  scl3300_get_errflag2(scl3300_t *dev);
uint32_t  scl3300_get_serial_number(scl3300_t *dev);

uint16_t  scl3300_powerdown(scl3300_t *dev);
uint16_t  scl3300_wakeup(scl3300_t *dev);
uint16_t  scl3300_reset(scl3300_t *dev);

// === Calculated values ===
double scl3300_get_angle_x(scl3300_t *dev);
double scl3300_get_angle_y(scl3300_t *dev);
double scl3300_get_angle_z(scl3300_t *dev);

double scl3300_get_accel_x(scl3300_t *dev);
double scl3300_get_accel_y(scl3300_t *dev);
double scl3300_get_accel_z(scl3300_t *dev);

double scl3300_get_temp_c(scl3300_t *dev);
double scl3300_get_temp_f(scl3300_t *dev);
