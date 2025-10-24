/**
 * @file led_status.c
 * @brief LED Status for WiFi Web Monitor - GPIO18 Active LOW (0=ON, 1=OFF)
 * 
 * States:
 * - NO_WIFI: LED ON solid (chưa có WiFi)
 * - WIFI_CONNECTED: LED blink 0.5s period (có WiFi và mDNS)
 * - DATA_SENDING: LED ON (đang gửi dữ liệu)
 * - DATA_IDLE: LED OFF (không gửi dữ liệu)
 */

#include "led_status.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED_STATUS";
static int s_led_gpio = 18;  // Default GPIO

static led_status_state_t s_current_state = LED_STATUS_NO_WIFI;
static TaskHandle_t s_blink_task = NULL;
static bool s_task_should_run = false;

static void led_blink_task(void *arg)
{
    while (s_task_should_run) {
        if (s_current_state == LED_STATUS_WIFI_CONNECTED) {
            gpio_set_level(s_led_gpio, 0);  // LED ON
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(s_led_gpio, 1);  // LED OFF
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            // Not in blink state, just wait
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    s_blink_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t led_status_init(int gpio_num)
{
    if (gpio_num > 0) {
        s_led_gpio = gpio_num;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_led_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d", s_led_gpio);
        return ret;
    }
    
    // Start with NO_WIFI state (LED ON)
    gpio_set_level(s_led_gpio, 0);
    ESP_LOGI(TAG, "LED initialized on GPIO%d (active-low)", s_led_gpio);
    return ESP_OK;
}

void led_status_set_state(led_status_state_t state)
{
    led_status_state_t prev_state = s_current_state;
    s_current_state = state;
    
    ESP_LOGI(TAG, "LED state change: %d -> %d", prev_state, state);
    
    switch (state) {
        case LED_STATUS_NO_WIFI:
            // Stop blink task if running
            if (s_blink_task != NULL) {
                s_task_should_run = false;
                vTaskDelay(pdMS_TO_TICKS(100)); // Wait for task to exit
            }
            // LED ON solid
            gpio_set_level(s_led_gpio, 0);
            break;
            
        case LED_STATUS_WIFI_CONNECTED:
            // Start blink task if not running
            if (s_blink_task == NULL) {
                s_task_should_run = true;
                xTaskCreate(led_blink_task, "led_blink", 1536, NULL, 1, &s_blink_task);
            }
            break;
            
        case LED_STATUS_DATA_SENDING:
            // Stop blink task if running
            if (s_blink_task != NULL) {
                s_task_should_run = false;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // LED ON
            gpio_set_level(s_led_gpio, 0);
            break;
            
        case LED_STATUS_DATA_IDLE:
            // Stop blink task if running
            if (s_blink_task != NULL) {
                s_task_should_run = false;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // LED OFF
            gpio_set_level(s_led_gpio, 1);
            break;
    }
}

void led_status_data_pulse_start(void)
{
    // Only control LED if we're in data transmission mode
    if (s_current_state == LED_STATUS_DATA_IDLE || s_current_state == LED_STATUS_DATA_SENDING) {
        gpio_set_level(s_led_gpio, 0);  // LED ON when sending
    }
}

void led_status_data_pulse_end(void)
{
    // Only control LED if we're in data transmission mode
    if (s_current_state == LED_STATUS_DATA_IDLE || s_current_state == LED_STATUS_DATA_SENDING) {
        gpio_set_level(s_led_gpio, 1);  // LED OFF when done
    }
}
