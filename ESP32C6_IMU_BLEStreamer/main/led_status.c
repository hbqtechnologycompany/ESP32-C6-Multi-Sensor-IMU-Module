/**
 * @file led_status.c
 * @brief LED Status - GPIO18 Active LOW (0=ON, 1=OFF)
 */

#include "led_status.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED_STATUS";
#define LED_GPIO 18

static TaskHandle_t s_blink_task = NULL;

static void blink_task(void *arg)
{
    while (1) {
        gpio_set_level(LED_GPIO, 0);  // LED ON
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 1);  // LED OFF
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t led_status_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d", LED_GPIO);
        return ret;
    }
    
    led_on();  // Start with LED ON (disconnected state)
    ESP_LOGI(TAG, "LED initialized on GPIO%d (active-low)", LED_GPIO);
    return ESP_OK;
}

void led_on(void)
{
    gpio_set_level(LED_GPIO, 0);  // Active low: 0 = ON
}

void led_off(void)
{
    gpio_set_level(LED_GPIO, 1);  // Active low: 1 = OFF
}

void led_start_blink(void)
{
    if (s_blink_task == NULL) {
        xTaskCreate(blink_task, "led_blink", 1024, NULL, 1, &s_blink_task);
    }
}

void led_stop_blink(void)
{
    if (s_blink_task != NULL) {
        vTaskDelete(s_blink_task);
        s_blink_task = NULL;
    }
}