#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_sntp.h"
#include <sys/time.h>


#include "led.h"
#include "button.h"
#include "hx711.h"
#include "ble.h"
#include "esp_timer.h"
#include <string.h>


#define WEIGHT_DECREASE_THRESHOLD 2.0f 

static const gpio_num_t LED_PINS[4] = { GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_16, GPIO_NUM_17 };
static const gpio_num_t BUTTON_PINS[4] = { GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27 };
static const gpio_num_t HX711_DT[4]  = { GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21 };
static const gpio_num_t HX711_SCK[4] = { GPIO_NUM_4, GPIO_NUM_23, GPIO_NUM_22, GPIO_NUM_25 };

static const char *TAG = "app";

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

uint64_t get_epoch_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

typedef struct {
    uint64_t ts_ms;
    uint8_t val;
} record_t;

static record_t records[64];
static size_t records_head = 0;

static void record_event(uint8_t val)
{
    uint64_t ts;
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        ts = get_epoch_ms();
    } else {
        ts = (uint64_t)(esp_timer_get_time() / 1000);
    }
    records[records_head].ts_ms = ts;
    records[records_head].val = val & 0x3;
    records_head = (records_head + 1) % (sizeof(records)/sizeof(records[0]));
}

static void on_button_event(size_t idx, button_event_t event)
{
    if (event == BUTTON_EVENT_PRESS) {
        ESP_LOGI(TAG, "Button %d press -> turn LED%d OFF", (int)idx, (int)idx);
        led_set(idx, 0);
    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        ESP_LOGI(TAG, "Button %d long-press -> start pairing", (int)idx);
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
        // each record: 8 bytes timestamp (little-endian) + 1 byte value
        if (pos + 9 > maxlen) break;
        uint64_t t = r->ts_ms;
        buf[pos++] = (uint8_t)(t & 0xFF);
        buf[pos++] = (uint8_t)((t >> 8) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 16) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 24) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 32) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 40) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 48) & 0xFF);
        buf[pos++] = (uint8_t)((t >> 56) & 0xFF);
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

    // Выполняем тарировку (обнуление)
    for (size_t i = 0; i < hx711_count(); ++i) {
        ESP_LOGI(TAG, "Taring sensor %d...", (int)i);
        hx711_tare(i, 20);  // 20 измерений для усреднения
    }

    float prev_weights[4] = {0};

    while (1) {
        for (size_t i = 0; i < hx711_count(); ++i) {
            float weight = hx711_get_weight(i);  // вес в калиброванных единицах 

            if (prev_weights[i] - weight >= WEIGHT_DECREASE_THRESHOLD) {
                ESP_LOGI(TAG, "Sensor %d: weight decreased from %.2f to %.2f (Δ = %.2f) → LED ON", 
                         (int)i, prev_weights[i], weight, prev_weights[i] - weight);

                led_set(i, 1); 
                record_event((uint8_t)i);
                ESP_LOGI(TAG, "LED %d turned ON due to weight decrease on sensor %d", (int)i, (int)i);
            }
            prev_weights[i] = weight;
            ESP_LOGI(TAG, "Sensor %d: %.2f g", (int)i, weight);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    initialize_sntp();
    int retry = 0;
    const int retry_count = 10;
    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for SNTP sync...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "SNTP sync failed - time not set");
    }

    // initialize NVS (required before starting BT/WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // init modules
    led_init(LED_PINS, 4);
    // Ensure default active-high logic for LEDs during tests and turn LED0 on
    led_set_active_low(false);
    led_set(0, 1);
    button_init(BUTTON_PINS, 4, on_button_event);
    hx711_init(HX711_DT, HX711_SCK, 4);
    ble_init(ble_record_read_cb);

    // set example calibration factors (adjust after calibration)
    for (size_t i = 0; i < hx711_count(); ++i) hx711_set_calibration(i, 420.0f);

    // start sensor reader
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    // LED test task removed — LED behaviour is controlled by button and startup state

    ESP_LOGI(TAG, "Application initialized");
}
