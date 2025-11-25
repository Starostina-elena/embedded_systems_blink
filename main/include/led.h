#pragma once
#include "driver/gpio.h"
#include <stddef.h>
#include "esp_err.h"

esp_err_t led_init(const gpio_num_t *pins, size_t count);
esp_err_t led_set(size_t idx, int level);
esp_err_t led_toggle(size_t idx);
esp_err_t led_get_level(size_t idx, int *out_level);
void led_dump(void);
void led_set_active_low(bool active_low);
