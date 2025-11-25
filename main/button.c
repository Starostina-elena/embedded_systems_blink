#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "button_mod";
static gpio_num_t *btn_pins = NULL;
static size_t btn_count = 0;
static TimerHandle_t *btn_timers = NULL;
static button_cb_t user_cb = NULL;

// Debounce timer callback runs in timer/daemon task context
static void btn_timer_cb(TimerHandle_t xTimer)
{
    size_t idx = (size_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    // Read stable level after debounce interval
    int level = gpio_get_level(btn_pins[idx]);
    if (level == 0) { // pressed (assuming pull-up)
        ESP_LOGI(TAG, "Button %u pressed (debounced)", idx);
        if (user_cb) user_cb(idx);
    } else {
        ESP_LOGI(TAG, "Button %u bounce ignored, level=%d", idx, level);
    }
}

// ISR: restart debounce timer for the button index
static void IRAM_ATTR isr_handler(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (btn_timers && idx < btn_count && btn_timers[idx]) {
        // Try to reset the timer; if reset fails (timer not active), start it
        if (xTimerResetFromISR(btn_timers[idx], &xHigherPriorityTaskWoken) != pdPASS) {
            xTimerStartFromISR(btn_timers[idx], &xHigherPriorityTaskWoken);
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

esp_err_t button_init(const gpio_num_t *pins, size_t count, button_cb_t cb)
{
    if (!pins || count == 0 || !cb) return ESP_ERR_INVALID_ARG;
    if (btn_pins) return ESP_ERR_INVALID_STATE;

    btn_pins = malloc(sizeof(gpio_num_t) * count);
    if (!btn_pins) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) btn_pins[i] = pins[i];
    btn_count = count;
    user_cb = cb;

    // configure pins
    uint64_t mask = 0;
    for (size_t i = 0; i < count; ++i) mask |= (1ULL << btn_pins[i]);
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = mask,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    // Create debounce timers
    btn_timers = malloc(sizeof(TimerHandle_t) * count);
    if (!btn_timers) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "btn_db_%zu", i);
        btn_timers[i] = xTimerCreate(name, pdMS_TO_TICKS(50), pdFALSE, (void *)(uintptr_t)i, btn_timer_cb);
        if (!btn_timers[i]) {
            // cleanup previously created timers
            for (size_t j = 0; j < i; ++j) xTimerDelete(btn_timers[j], 0);
            free(btn_timers);
            btn_timers = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_install_isr_service(0);
    for (size_t i = 0; i < count; ++i) {
        gpio_isr_handler_add(btn_pins[i], isr_handler, (void *)(uintptr_t)i);
    }

    ESP_LOGI(TAG, "Button module initialized (%d buttons)", (int)count);
    return ESP_OK;
}
