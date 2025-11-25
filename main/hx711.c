#include "hx711.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdint.h>

static gpio_num_t *dt_pins = NULL;
static gpio_num_t *sck_pins = NULL;
static int32_t *offsets = NULL;
static float *cal_factors = NULL;
static size_t hx_count = 0;
static const char *TAG = "hx711_mod";

esp_err_t hx711_init(const gpio_num_t *dt, const gpio_num_t *sck, size_t count)
{
    if (!dt || !sck || count == 0) return ESP_ERR_INVALID_ARG;
    if (dt_pins) return ESP_ERR_INVALID_STATE;

    dt_pins = malloc(sizeof(gpio_num_t) * count);
    sck_pins = malloc(sizeof(gpio_num_t) * count);
    offsets = malloc(sizeof(int32_t) * count);
    cal_factors = malloc(sizeof(float) * count);
    if (!dt_pins || !sck_pins || !offsets || !cal_factors) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < count; ++i) {
        dt_pins[i] = dt[i];
        sck_pins[i] = sck[i];
        offsets[i] = 0;
        cal_factors[i] = 1.0f;
        gpio_set_direction(dt_pins[i], GPIO_MODE_INPUT);
        gpio_set_direction(sck_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(sck_pins[i], 0);
    }
    hx_count = count;
    ESP_LOGI(TAG, "HX711 module initialized (%d sensors)", (int)count);
    return ESP_OK;
}

static int32_t hx711_read_raw_pin(gpio_num_t dt_pin, gpio_num_t sck_pin)
{
    // wait for ready (DOUT low) with short timeout
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(dt_pin) == 1) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(20)) return 0x7FFFFFFF;
    }

    uint32_t data = 0;
    for (int i = 0; i < 24; ++i) {
        gpio_set_level(sck_pin, 1);
        gpio_set_level(sck_pin, 0);
        data <<= 1;
        if (gpio_get_level(dt_pin)) data |= 1;
    }
    // one extra pulse to set gain/channel
    gpio_set_level(sck_pin, 1);
    gpio_set_level(sck_pin, 0);

    return (data & 0x800000) ? (int32_t)(data | 0xFF000000) : (int32_t)data;
}

int32_t hx711_read_raw(size_t idx)
{
    if (idx >= hx_count) return 0x7FFFFFFE;
    return hx711_read_raw_pin(dt_pins[idx], sck_pins[idx]);
}

esp_err_t hx711_tare(size_t idx, int samples)
{
    if (idx >= hx_count || samples <= 0) return ESP_ERR_INVALID_ARG;
    int64_t sum = 0;
    int got = 0;
    for (int i = 0; i < samples; ++i) {
        int32_t v = hx711_read_raw(idx);
        if (v == 0x7FFFFFFF) continue;
        sum += v;
        ++got;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (got == 0) return ESP_FAIL;
    offsets[idx] = (int32_t)(sum / got);
    return ESP_OK;
}

esp_err_t hx711_set_calibration(size_t idx, float factor)
{
    if (idx >= hx_count || factor == 0.0f) return ESP_ERR_INVALID_ARG;
    cal_factors[idx] = factor;
    return ESP_OK;
}

float hx711_get_weight(size_t idx)
{
    if (idx >= hx_count) return 0.0f;
    int32_t raw = hx711_read_raw(idx);
    if (raw == 0x7FFFFFFF) return 0.0f;
    int32_t net = raw - offsets[idx];
    return (float)net / cal_factors[idx];
}

size_t hx711_count(void)
{
    return hx_count;
}
