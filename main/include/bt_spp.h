#pragma once
#include <stddef.h>
#include "esp_err.h"

esp_err_t bt_init(void);
esp_err_t bt_start_pairing_mode(void);
esp_err_t bt_send_records(const uint8_t *data, size_t len);
