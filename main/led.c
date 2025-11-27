#include "led.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdlib.h>

static gpio_num_t *led_pins = NULL;
static size_t led_count = 0;
static const char *TAG = "led_mod";
static bool led_active_low = false;

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
    {
        char buf[128];
        int off = 0;
        off += snprintf(buf + off, sizeof(buf) - off, "pins=");
        for (size_t i = 0; i < count && off < (int)sizeof(buf); ++i) {
            off += snprintf(buf + off, sizeof(buf) - off, "%d%s", (int)led_pins[i], (i + 1 < count) ? "," : "");
        }
        ESP_LOGI(TAG, "%s", buf);
    }
    return ESP_OK;
}

esp_err_t led_set(size_t idx, int level)
{
    if (!led_pins || idx >= led_count) return ESP_ERR_INVALID_ARG;
    int phys = (level ? 1 : 0);
    if (led_active_low) phys = !phys;
    gpio_set_level(led_pins[idx], phys);
    int read_back = gpio_get_level(led_pins[idx]);
    ESP_LOGI(TAG, "led_set idx=%d req=%d phys=%d read=%d gpio=%d", (int)idx, level ? 1 : 0, phys, read_back, (int)led_pins[idx]);
    return ESP_OK;
}

esp_err_t led_toggle(size_t idx)
{
    if (!led_pins || idx >= led_count) return ESP_ERR_INVALID_ARG;
    int cur = gpio_get_level(led_pins[idx]);
    int newv = !cur;
    gpio_set_level(led_pins[idx], newv);
    int read_back = gpio_get_level(led_pins[idx]);
    ESP_LOGI(TAG, "led_toggle idx=%d gpio=%d from=%d to=%d read=%d", (int)idx, (int)led_pins[idx], cur, newv, read_back);
    return ESP_OK;
}

esp_err_t led_get_level(size_t idx, int *out_level)
{
    if (!led_pins || idx >= led_count || !out_level) return ESP_ERR_INVALID_ARG;
    *out_level = gpio_get_level(led_pins[idx]);
    return ESP_OK;
}

void led_dump(void)
{
    if (!led_pins) {
        ESP_LOGI(TAG, "led_dump: not initialized");
        return;
    }
    for (size_t i = 0; i < led_count; ++i) {
        int lvl = gpio_get_level(led_pins[i]);
        int logical = led_active_low ? !lvl : lvl;
        ESP_LOGI(TAG, "led[%d] gpio=%d phys=%d logical=%d", (int)i, (int)led_pins[i], lvl, logical);
    }
}

void led_set_active_low(bool active_low)
{
    led_active_low = active_low;
    ESP_LOGI(TAG, "led_set_active_low=%d", (int)led_active_low);
}
