#pragma once
#include "esp_err.h"
#include <stddef.h>

typedef size_t (*ble_read_cb_t)(uint8_t *buf, size_t maxlen);

esp_err_t ble_init(ble_read_cb_t read_cb);
esp_err_t ble_start_advertising(void);
