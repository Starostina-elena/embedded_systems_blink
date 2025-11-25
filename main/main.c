#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"

#include "led.h"
#include "button.h"
#include "hx711.h"
#include "ble.h"
#include "esp_timer.h"
#include <string.h>

// Default pin configuration: change to match your board/wiring
// Default LED pins — choose GPIOs that are not used by HX711 or buttons
static const gpio_num_t LED_PINS[4] = { GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_16, GPIO_NUM_17 };
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
            //int32_t raw = hx711_read_raw(i);
            //float weight = hx711_get_weight(i);
            // ESP_LOGI(TAG, "Sensor %d: raw=%ld weight=%.2fg", (int)i, (long)raw, weight);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void led_test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "led_test_task started (blinking LED_PINS[0] then LED_PINS[1])");
    while (1) {
        // Blink first LED with read-back logging
        ESP_LOGI(TAG, "GPIO%d HIGH (request)", LED_PINS[0]);
        gpio_set_level(LED_PINS[0], 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        {
            int rb = gpio_get_level(LED_PINS[0]);
            ESP_LOGI(TAG, "GPIO%d read after set=1 -> %d", LED_PINS[0], rb);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP_LOGI(TAG, "GPIO%d LOW (request)", LED_PINS[0]);
        gpio_set_level(LED_PINS[0], 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        {
            int rb = gpio_get_level(LED_PINS[0]);
            ESP_LOGI(TAG, "GPIO%d read after set=0 -> %d", LED_PINS[0], rb);
        }
        vTaskDelay(pdMS_TO_TICKS(150));

        // Blink second LED with read-back
        ESP_LOGI(TAG, "GPIO%d HIGH (request)", LED_PINS[1]);
        gpio_set_level(LED_PINS[1], 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        {
            int rb = gpio_get_level(LED_PINS[1]);
            ESP_LOGI(TAG, "GPIO%d read after set=1 -> %d", LED_PINS[1], rb);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP_LOGI(TAG, "GPIO%d LOW (request)", LED_PINS[1]);
        gpio_set_level(LED_PINS[1], 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        {
            int rb = gpio_get_level(LED_PINS[1]);
            ESP_LOGI(TAG, "GPIO%d read after set=0 -> %d", LED_PINS[1], rb);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void app_main(void)
{
    // initialize NVS (required before starting BT/WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // init modules
    led_init(LED_PINS, 4);
    // Ensure default active-high logic for LEDs during tests
    led_set_active_low(false);
    // Quick startup self-test for LED pins: set, dump, toggle
    ESP_LOGI(TAG, "Performing LED startup self-test");
    led_dump();
    led_set(0, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    led_dump();
    led_set(0, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_dump();
    button_init(BUTTON_PINS, 4, on_button_event);
    hx711_init(HX711_DT, HX711_SCK, 4);
    ble_init(ble_record_read_cb);

    // set example calibration factors (adjust after calibration)
    for (size_t i = 0; i < hx711_count(); ++i) hx711_set_calibration(i, 420.0f);

    // start sensor reader
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    // Simple LED verify task: toggle LED0 every 2s to test control independent of button
    xTaskCreate(led_test_task, "led_test", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Application initialized");
}
