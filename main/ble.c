#include "ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sys/param.h"
#include "os_mbuf.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ble_mod";
static ble_read_cb_t g_read_cb = NULL;
static bool g_ble_synced = false;

// Simple custom 16-bit service/char UUIDs (private)
#define BLE_SVC_UUID 0xA000
#define BLE_CHAR_UUID 0xA001

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (!g_read_cb) return BLE_ATT_ERR_UNLIKELY;
        uint8_t buf[512];
        size_t len = g_read_cb(buf, sizeof(buf));
        if (len > 0) {
            os_mbuf_append(ctxt->om, buf, len);
        }
        return 0;
    }
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CHAR_UUID),
                .access_cb = gatt_svr_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },
    { 0 }
};

static void ble_app_on_sync(void)
{
    int rc;
    uint8_t addr_type;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed %d", rc);
        return;
    }

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed %d", rc);
    } else {
        ESP_LOGI(TAG, "ble_gatts_count_cfg OK");
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed %d", rc);
    } else {
        ESP_LOGI(TAG, "GATT services added");
    }

    ESP_LOGI(TAG, "BLE stack synced");
    g_ble_synced = true;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "BLE connected");
        } else {
            ESP_LOGI(TAG, "BLE connection failed; status=%d", event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected; reason=%d", event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        break;
    default:
        break;
    }
    return 0;
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_init(ble_read_cb_t read_cb)
{
    g_read_cb = read_cb;

    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.reset_cb = NULL;

    // initialize GATT services after stack in sync callback

    // start the nimble host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE initialized (NimBLE)");
    return ESP_OK;
}

esp_err_t ble_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = "PILL_DEVICE";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed %d", rc);
        return ESP_FAIL;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    if (!g_ble_synced) {
        ESP_LOGW(TAG, "BLE not synced yet; cannot start advertising");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t addr_type;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Using addr_type=%u for advertising", (unsigned)addr_type);

    /* Stop any existing advertising first (ignore errors) */
    int rc_stop = ble_gap_adv_stop();
    if (rc_stop == 0) {
        ESP_LOGI(TAG, "Stopped previous advertising");
    } else if (rc_stop == BLE_HS_EALREADY) {
        /* nothing running */
    } else {
        ESP_LOGW(TAG, "ble_gap_adv_stop returned %d", rc_stop);
    }

    rc = ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BLE advertising started");
    return ESP_OK;
}
