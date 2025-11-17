#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO1 GPIO_NUM_27
#define BLINK_GPIO2 GPIO_NUM_2

static const char *TAG = "blink_debug";

void app_main(void)
{
    // Prepare both pins for testing
    gpio_reset_pin(BLINK_GPIO1);
    gpio_reset_pin(BLINK_GPIO2);
    gpio_set_direction(BLINK_GPIO1, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLINK_GPIO2, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "Starting blink test: GPIO27 then GPIO2");

    while (1) {
        // Blink GPIO27
        ESP_LOGI(TAG, "GPIO27 HIGH");
        gpio_set_level(BLINK_GPIO1, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BLINK_GPIO1, 0);
        vTaskDelay(pdMS_TO_TICKS(200));

        // Blink GPIO2
        ESP_LOGI(TAG, "GPIO2 HIGH");
        gpio_set_level(BLINK_GPIO2, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BLINK_GPIO2, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
