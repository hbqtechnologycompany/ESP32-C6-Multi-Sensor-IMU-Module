#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    LED_STATUS_NO_WIFI,        // Chưa có WiFi - LED sáng
    LED_STATUS_WIFI_CONNECTED, // Có WiFi và mDNS - LED chớp 0.5s
    LED_STATUS_DATA_SENDING,   // Đang gửi dữ liệu - LED sáng
    LED_STATUS_DATA_IDLE       // Không gửi dữ liệu - LED tắt
} led_status_state_t;

/**
 * @brief Initialize LED status indicator
 * @param gpio_num GPIO pin number for LED (default: 18)
 * @return ESP_OK on success
 */
esp_err_t led_status_init(int gpio_num);

/**
 * @brief Set LED status state
 * @param state New state
 */
void led_status_set_state(led_status_state_t state);

/**
 * @brief Pulse LED briefly during data transmission
 */
void led_status_data_pulse_start(void);
void led_status_data_pulse_end(void);

#endif // LED_STATUS_H
