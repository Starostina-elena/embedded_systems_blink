#pragma once
#include "driver/gpio.h"
#include <stddef.h>
#include "esp_err.h"

typedef void (*button_cb_t)(size_t index);

esp_err_t button_init(const gpio_num_t *pins, size_t count, button_cb_t cb);
