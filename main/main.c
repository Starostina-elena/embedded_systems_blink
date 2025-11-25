#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "led.h"
#include "button.h"
#include "hx711.h"
#include "ble.h"
#include "esp_timer.h"
#include <string.h>

// Default pin configuration: change to match your board/wiring
static const gpio_num_t LED_PINS[4] = { GPIO_NUM_2, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17 };
static const gpio_num_t BUTTON_PINS[4] = { GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27 };
// HX711 DT and SCK pins (per sensor). Change these to match your wiring.
static const gpio_num_t HX711_DT[4]  = { GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21 };
static const gpio_num_t HX711_SCK[4] = { GPIO_NUM_4, GPIO_NUM_23, GPIO_NUM_22, GPIO_NUM_25 };

static const char *TAG = "app";

// Event record: timestamp (ms) + value (0..3)
typedef struct {
    uint32_t ts_ms;
    uint8_t val; // 0..3
} record_t;

static record_t records[64];
static size_t records_head = 0;

static void record_event(uint8_t val)
{
    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
    records[records_head].ts_ms = ts;
    records[records_head].val = val & 0x3;
    records_head = (records_head + 1) % (sizeof(records)/sizeof(records[0]));
}

static void on_button_event(size_t idx, button_event_t event)
{
    if (event == BUTTON_EVENT_PRESS) {
        ESP_LOGI(TAG, "Button %d press", (int)idx);
        // toggle LED on press
        led_toggle(idx);
        record_event((uint8_t)idx);
    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        ESP_LOGI(TAG, "Button %d long-press -> start pairing", (int)idx);
        // start BLE advertising; client will read the GATT characteristic to receive records
        ble_start_advertising();
    }
}

// BLE read callback: fill buffer with [count:uint8][records...]
static size_t ble_record_read_cb(uint8_t *buf, size_t maxlen)
{
    if (!buf || maxlen < 1) return 0;
    size_t count = sizeof(records)/sizeof(records[0]);
    size_t pos = 1; // leave space for count
    uint8_t send_count = 0;
    for (size_t i = 0; i < count; ++i) {
        record_t *r = &records[i];
        if (r->ts_ms == 0) continue;
        if (pos + 5 > maxlen) break;
        uint32_t t = r->ts_ms;
        buf[pos++] = (uint8_t)(t & 0xFF);
        buf[pos++] = (uint8_t)((t >> 8) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 16) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 24) & 0xFF);
        buf[pos++] = r->val;
        send_count++;
    }
    if (send_count == 0) return 0;
    buf[0] = send_count;
    return pos;
}

static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");
    // perform taring for each sensor
    for (size_t i = 0; i < hx711_count(); ++i) {
        hx711_tare(i, 20);
    }

    while (1) {
        for (size_t i = 0; i < hx711_count(); ++i) {
            int32_t raw = hx711_read_raw(i);
            float weight = hx711_get_weight(i);
            ESP_LOGI(TAG, "Sensor %d: raw=%ld weight=%.2fg", (int)i, (long)raw, weight);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    // init modules
    led_init(LED_PINS, 4);
    button_init(BUTTON_PINS, 4, on_button_event);
    hx711_init(HX711_DT, HX711_SCK, 4);
    ble_init(ble_record_read_cb);

    // set example calibration factors (adjust after calibration)
    for (size_t i = 0; i < hx711_count(); ++i) hx711_set_calibration(i, 420.0f);

    // start sensor reader
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Application initialized");
}
