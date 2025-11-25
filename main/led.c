#include "led.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdlib.h>

static gpio_num_t *led_pins = NULL;
static size_t led_count = 0;
static const char *TAG = "led_mod";

esp_err_t led_init(const gpio_num_t *pins, size_t count)
{
    if (!pins || count == 0) return ESP_ERR_INVALID_ARG;
    if (led_pins) return ESP_ERR_INVALID_STATE;

    led_pins = malloc(sizeof(gpio_num_t) * count);
    if (!led_pins) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) {
        led_pins[i] = pins[i];
        gpio_reset_pin(led_pins[i]);
        gpio_set_direction(led_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(led_pins[i], 0);
    }
    led_count = count;
    ESP_LOGI(TAG, "LED module initialized (%d LEDs)", (int)count);
    return ESP_OK;
}

esp_err_t led_set(size_t idx, int level)
{
    if (!led_pins || idx >= led_count) return ESP_ERR_INVALID_ARG;
    gpio_set_level(led_pins[idx], level ? 1 : 0);
    ESP_LOGI(TAG, "led_set idx=%d level=%d gpio=%d", (int)idx, level ? 1 : 0, led_pins[idx]);
    return ESP_OK;
}

esp_err_t led_toggle(size_t idx)
{
    if (!led_pins || idx >= led_count) return ESP_ERR_INVALID_ARG;
    int cur = gpio_get_level(led_pins[idx]);
    gpio_set_level(led_pins[idx], !cur);
    ESP_LOGI(TAG, "led_toggle idx=%d gpio=%d from=%d to=%d", (int)idx, led_pins[idx], cur, !cur);
    return ESP_OK;
}
