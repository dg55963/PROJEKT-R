#include "ble_service.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_nimble_hci.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t temp_hum_handle;
static uint8_t own_addr_type;
static bool ble_ready = false;

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x181A),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = BLE_UUID16_DECLARE(0x2A6E),
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &temp_hum_handle,
                },
                {0},
            },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_CONNECT)
    {
        conn_handle = event->connect.conn_handle;
    }
    else if (event->type == BLE_GAP_EVENT_DISCONNECT)
    {
        conn_handle = 0;
        struct ble_gap_adv_params adv_params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
        if (rc != 0)
        {
            printf("adv_restart failed: %d\n", rc);
        }
    }
    return 0;
}

static void on_sync(void)
{
    // Determine proper address type (public or random) before advertising.
    ble_hs_id_infer_auto(0, &own_addr_type);
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
    if (rc != 0)
    {
        printf("adv_start failed: %d\n", rc);
    }
}

static void ble_task(void *param)
{
    nimble_port_run();
}

void ble_service_init(void)
{
    // Init NVS and BLE controller/HCI before NimBLE host.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK)
    {
        printf("controller init failed: %d\n", err);
        return;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK)
    {
        printf("controller enable failed: %d\n", err);
        return;
    }

    err = esp_nimble_hci_init();
    if (err != ESP_OK)
    {
        printf("nimble hci init failed: %d\n", err);
        return;
    }

    err = nimble_port_init();
    if (err != 0)
    {
        printf("nimble_port_init failed: %d\n", err);
        return;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("ESP32-DHT");
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(ble_task);
    ble_ready = true;
    printf("BLE service initialized\n");
}

void ble_service_send_data(float temperature, float humidity)
{
    if (!ble_ready || conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return;
    }
    uint8_t data[8];
    memcpy(data, &temperature, 4);
    memcpy(data + 4, &humidity, 4);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, 8);
    ble_gattc_notify_custom(conn_handle, temp_hum_handle, om);
}