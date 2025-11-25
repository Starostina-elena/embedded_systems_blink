#include "bt_spp.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bt_spp";
static uint32_t spp_handle = 0;
static uint32_t client_handle = 0;

static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(TAG, "SPP initialized");
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "PILL_DEVICE");
        break;
    case ESP_SPP_START_EVT:
        ESP_LOGI(TAG, "SPP server started");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "Client connected");
        client_handle = param->srv_open.handle;
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "SPP connection closed");
        client_handle = 0;
        break;
    default:
        break;
    }
}

esp_err_t bt_init(void)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) return ret;
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret) return ret;
    ret = esp_bluedroid_init();
    if (ret) return ret;
    ret = esp_bluedroid_enable();
    if (ret) return ret;
    esp_spp_register_callback(spp_cb);
    esp_spp_init(ESP_SPP_MODE_CB);
    esp_bt_dev_set_device_name("PILL_DEVICE");
    ESP_LOGI(TAG, "BT initialized");
    return ESP_OK;
}

esp_err_t bt_start_pairing_mode(void)
{
    // Make device discoverable/connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    ESP_LOGI(TAG, "Bluetooth pairing mode started (discoverable)");
    return ESP_OK;
}

esp_err_t bt_send_records(const uint8_t *data, size_t len)
{
    if (!client_handle) return ESP_ERR_INVALID_STATE;
    esp_err_t r = esp_spp_write(client_handle, len, (uint8_t *)data);
    return r == ESP_OK ? ESP_OK : ESP_FAIL;
}
