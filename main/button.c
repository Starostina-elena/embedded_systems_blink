#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "button_mod";
static gpio_num_t *btn_pins = NULL;
static size_t btn_count = 0;
static TimerHandle_t *btn_timers = NULL;
static TimerHandle_t *btn_long_timers = NULL;
static button_cb_t user_cb = NULL;
static QueueHandle_t btn_evt_queue = NULL;
static TaskHandle_t btn_evt_task = NULL;
static bool g_active_low = true;

typedef struct {
    size_t idx;
    button_event_t ev;
} btn_event_t;

// Debounce timer callback runs in timer/daemon task context
static void btn_timer_cb(TimerHandle_t xTimer)
{
    size_t idx = (size_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    // Read stable level after debounce interval
    int level = gpio_get_level(btn_pins[idx]);
    int pressed_level = g_active_low ? 0 : 1;
    if (level == pressed_level) {
        ESP_LOGI(TAG, "Button %u pressed (debounced), level=%d", idx, level);
        // start long-press timer
        if (btn_long_timers && btn_long_timers[idx]) {
            xTimerStart(btn_long_timers[idx], 0);
        }
        if (btn_evt_queue) {
            btn_event_t e = { idx, BUTTON_EVENT_PRESS };
            xQueueSend(btn_evt_queue, &e, 0);
        }
    } else {
        ESP_LOGI(TAG, "Button %u released (debounced), level=%d", idx, level);
        // cancel long-press if running
        if (btn_long_timers && btn_long_timers[idx]) {
            xTimerStop(btn_long_timers[idx], 0);
        }
        if (btn_evt_queue) {
            btn_event_t e = { idx, BUTTON_EVENT_RELEASE };
            xQueueSend(btn_evt_queue, &e, 0);
        }
    }
}

// long-press timer callback
static void btn_long_timer_cb(TimerHandle_t xTimer)
{
    size_t idx = (size_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    ESP_LOGI(TAG, "Button %u long-press detected", idx);
    if (btn_evt_queue) {
        btn_event_t e = { idx, BUTTON_EVENT_LONG_PRESS };
        xQueueSend(btn_evt_queue, &e, 0);
    }
}

static void btn_event_task(void *arg)
{
    btn_event_t e;
    while (1) {
        if (xQueueReceive(btn_evt_queue, &e, portMAX_DELAY) == pdTRUE) {
            if (user_cb) user_cb(e.idx, e.ev);
        }
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

// Default init wrapper keeps old API: active_low = true
esp_err_t button_init(const gpio_num_t *pins, size_t count, button_cb_t cb)
{
    return button_init_ex(pins, count, cb, true);
}

esp_err_t button_init_ex(const gpio_num_t *pins, size_t count, button_cb_t cb, bool active_low)
{
    if (!pins || count == 0 || !cb) return ESP_ERR_INVALID_ARG;
    if (btn_pins) return ESP_ERR_INVALID_STATE;

    g_active_low = active_low;

    btn_pins = malloc(sizeof(gpio_num_t) * count);
    if (!btn_pins) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) btn_pins[i] = pins[i];
    btn_count = count;
    user_cb = cb;

    // configure pins
    uint64_t mask = 0;
    for (size_t i = 0; i < count; ++i) mask |= (1ULL << btn_pins[i]);
    gpio_config_t io_conf = {
        .intr_type = (g_active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE),
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = mask,
        .pull_down_en = (g_active_low ? 0 : 1),
        .pull_up_en = (g_active_low ? 1 : 0),
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

    // Create event queue and handler task
    btn_evt_queue = xQueueCreate(count * 4, sizeof(btn_event_t));
    if (!btn_evt_queue) return ESP_ERR_NO_MEM;
    BaseType_t ok = xTaskCreate(btn_event_task, "btn_evt", 4096, NULL, 5, &btn_evt_task);
    if (ok != pdPASS) {
        vQueueDelete(btn_evt_queue);
        btn_evt_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Create long-press timers (5 seconds)
    btn_long_timers = malloc(sizeof(TimerHandle_t) * count);
    if (!btn_long_timers) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) {
        char lname[32];
        snprintf(lname, sizeof(lname), "btn_long_%zu", i);
        btn_long_timers[i] = xTimerCreate(lname, pdMS_TO_TICKS(5000), pdFALSE, (void *)(uintptr_t)i, btn_long_timer_cb);
        if (!btn_long_timers[i]) {
            for (size_t j = 0; j < i; ++j) xTimerDelete(btn_long_timers[j], 0);
            free(btn_long_timers);
            btn_long_timers = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Button module initialized (%d buttons), active_low=%d", (int)count, g_active_low);
    return ESP_OK;
}

esp_err_t button_deinit(void)
{
    if (!btn_pins) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < btn_count; ++i) {
        gpio_isr_handler_remove(btn_pins[i]);
        if (btn_timers && btn_timers[i]) xTimerDelete(btn_timers[i], 0);
        if (btn_long_timers && btn_long_timers[i]) xTimerDelete(btn_long_timers[i], 0);
    }
    if (btn_timers) { free(btn_timers); btn_timers = NULL; }
    if (btn_long_timers) { free(btn_long_timers); btn_long_timers = NULL; }
    if (btn_evt_task) {
        vTaskDelete(btn_evt_task);
        btn_evt_task = NULL;
    }
    if (btn_evt_queue) {
        vQueueDelete(btn_evt_queue);
        btn_evt_queue = NULL;
    }
    free(btn_pins); btn_pins = NULL; btn_count = 0; user_cb = NULL;
    return ESP_OK;
}
