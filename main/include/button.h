#pragma once
#include "driver/gpio.h"
#include <stddef.h>
#include "esp_err.h"

typedef enum {
	BUTTON_EVENT_PRESS = 0,
	BUTTON_EVENT_RELEASE = 1,
	BUTTON_EVENT_LONG_PRESS = 2,
} button_event_t;

typedef void (*button_cb_t)(size_t index, button_event_t event);

esp_err_t button_init(const gpio_num_t *pins, size_t count, button_cb_t cb);
esp_err_t button_deinit(void);
