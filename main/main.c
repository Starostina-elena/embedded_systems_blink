#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "led.h"
#include "button.h"
#include "hx711.h"

// Default pin configuration: change to match your board/wiring
static const gpio_num_t LED_PINS[4] = { GPIO_NUM_2, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17 };
static const gpio_num_t BUTTON_PINS[4] = { GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27 };
// HX711 DT and SCK pins (per sensor). Change these to match your wiring.
static const gpio_num_t HX711_DT[4]  = { GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21 };
static const gpio_num_t HX711_SCK[4] = { GPIO_NUM_4, GPIO_NUM_23, GPIO_NUM_22, GPIO_NUM_25 };

static const char *TAG = "app";

static void on_button_pressed(size_t idx)
{
    ESP_LOGI(TAG, "Button %d pressed — toggling LED %d", (int)idx, (int)idx);
    led_toggle(idx);
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
    button_init(BUTTON_PINS, 4, on_button_pressed);
    hx711_init(HX711_DT, HX711_SCK, 4);

    // set example calibration factors (adjust after calibration)
    for (size_t i = 0; i < hx711_count(); ++i) hx711_set_calibration(i, 420.0f);

    // start sensor reader
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Application initialized");
}
