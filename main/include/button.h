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

// Initialize buttons. Default behavior (button_init) assumes active-low wiring (pressed = 0).
// Use `button_init_ex` to specify `active_low = false` for active-high buttons.
#include <stdbool.h>
esp_err_t button_init_ex(const gpio_num_t *pins, size_t count, button_cb_t cb, bool active_low);
esp_err_t button_init(const gpio_num_t *pins, size_t count, button_cb_t cb);
esp_err_t button_deinit(void);
