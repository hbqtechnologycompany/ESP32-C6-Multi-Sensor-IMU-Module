
#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "esp_err.h"

esp_err_t led_status_init(void);
void led_on(void);
void led_off(void);
void led_start_blink(void);
void led_stop_blink(void);

#endif // LED_STATUS_H
