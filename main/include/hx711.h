#pragma once
#include "driver/gpio.h"
#include "esp_err.h"
#include <stddef.h>

esp_err_t hx711_init(const gpio_num_t *dt_pins, const gpio_num_t *sck_pins, size_t count);
int32_t hx711_read_raw(size_t idx);
esp_err_t hx711_tare(size_t idx, int samples);
esp_err_t hx711_set_calibration(size_t idx, float factor);
float hx711_get_weight(size_t idx);
size_t hx711_count(void);
