#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO1 GPIO_NUM_27
#define BLINK_GPIO2 GPIO_NUM_2
#define BUTTON_GPIO GPIO_NUM_13

static const char *TAG = "blink_debug";

static QueueHandle_t gpio_evt_queue = NULL;

// ISR: post gpio number to queue
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) (uintptr_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Task to handle button events with simple debounce
static void button_task(void *arg)
{
    uint32_t io_num;
    const TickType_t debounce_delay = pdMS_TO_TICKS(50);

    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            // simple debounce: wait, then confirm level
            vTaskDelay(debounce_delay);
            int level = gpio_get_level((gpio_num_t)io_num);
            // assuming button pulls pin to 0 when pressed (pull-up enabled)
            if (level == 0) {
                ESP_LOGI(TAG, "Button on GPIO%d pressed", io_num);
            } else {
                ESP_LOGI(TAG, "GPIO%d event but level=%d (ignored)", io_num, level);
            }
        }
    }
}

// Optional polling task to help debug button hardware/config
static void button_poll_task(void *arg)
{
    int last_level = gpio_get_level(BUTTON_GPIO);
    ESP_LOGI(TAG, "Button poll start, initial level=%d", last_level);
    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        if (level != last_level) {
            ESP_LOGI(TAG, "Button poll: GPIO%d level changed to %d", BUTTON_GPIO, level);
            last_level = level;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    // Prepare both pins for testing
    gpio_reset_pin(BLINK_GPIO1);
    gpio_reset_pin(BLINK_GPIO2);
    gpio_set_direction(BLINK_GPIO1, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLINK_GPIO2, GPIO_MODE_OUTPUT);

    // Configure button GPIO13 as input with internal pull-up and falling edge interrupt
    gpio_reset_pin(BUTTON_GPIO);
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    // Create queue to handle gpio event from ISR
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void *) BUTTON_GPIO);
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    // start poll task for debugging (can be removed when ISR works)
    xTaskCreate(button_poll_task, "button_poll", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Starting blink test: GPIO27 then GPIO2");
    

    while (1) {
        // Blink GPIO27
        // ESP_LOGI(TAG, "GPIO27 HIGH");
        gpio_set_level(BLINK_GPIO1, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BLINK_GPIO1, 0);
        vTaskDelay(pdMS_TO_TICKS(200));

        // Blink GPIO2
        // ESP_LOGI(TAG, "GPIO2 HIGH");
        gpio_set_level(BLINK_GPIO2, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BLINK_GPIO2, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
