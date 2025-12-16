/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gatt_svc.h"

#include "common.h"

/* Private function declarations */
static int temperature_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int humidity_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);

/* Private variables */
/* DHT Sensor service */
static const ble_uuid128_t dht_service_uuid = BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);

/* Temperature characteristic */
static uint8_t temperature_chr_val[64] = {0};
static uint16_t temperature_chr_val_handle;
static const ble_uuid128_t temperature_chr_uuid = BLE_UUID128_INIT(0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);

/* Humidity characteristic */
static uint8_t humidity_chr_val[64] = {0};
static uint16_t humidity_chr_val_handle;
static const ble_uuid128_t humidity_chr_uuid = BLE_UUID128_INIT(0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);

/* Connection tracking */
static uint16_t dht_chr_conn_handle = 0;
static bool dht_chr_conn_handle_inited = false;
static bool temperature_notify_status = false;
static bool humidity_notify_status = false;

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* DHT Sensor service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &dht_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){{/* Temperature characteristic */
                                                     .uuid = &temperature_chr_uuid.u,
                                                     .access_cb = temperature_chr_access,
                                                     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                     .val_handle = &temperature_chr_val_handle},
                                                    {/* Humidity characteristic */
                                                     .uuid = &humidity_chr_uuid.u,
                                                     .access_cb = humidity_chr_access,
                                                     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                     .val_handle = &humidity_chr_val_handle},
                                                    {
                                                        0, /* No more characteristics in this service. */
                                                    }}},

    {
        0, /* No more services. */
    },
};

/* Private functions */
static int temperature_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    switch (ctxt->op) {
        /* Read characteristic event */
        case BLE_GATT_ACCESS_OP_READ_CHR:
            /* Verify connection handle */
            if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ESP_LOGI(TAG, "temperature read; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);
            } else {
                ESP_LOGI(TAG, "temperature read by nimble stack; attr_handle=%d", attr_handle);
            }

            /* Verify attribute handle */
            if (attr_handle == temperature_chr_val_handle) {
                /* Send current temperature value */
                rc = os_mbuf_append(ctxt->om, &temperature_chr_val, sizeof(temperature_chr_val));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            goto error;

        /* Unknown event */
        default:
            goto error;
    }

error:
    ESP_LOGE(TAG, "unexpected access operation to temperature characteristic, opcode: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int humidity_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    switch (ctxt->op) {
        /* Read characteristic event */
        case BLE_GATT_ACCESS_OP_READ_CHR:
            /* Verify connection handle */
            if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ESP_LOGI(TAG, "humidity read; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);
            } else {
                ESP_LOGI(TAG, "humidity read by nimble stack; attr_handle=%d", attr_handle);
            }

            /* Verify attribute handle */
            if (attr_handle == humidity_chr_val_handle) {
                /* Send current humidity value */
                rc = os_mbuf_append(ctxt->om, &humidity_chr_val, sizeof(humidity_chr_val));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            goto error;

        /* Unknown event */
        default:
            goto error;
    }

error:
    ESP_LOGE(TAG, "unexpected access operation to humidity characteristic, opcode: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

/* Public functions */
void send_temperature_humidity_notification(float temperature, float humidity) {
    char temp_str[16];
    char hum_str[16];
    int temp_len, hum_len;

    /* Format temperature as string */
    temp_len = snprintf((char*)temperature_chr_val, sizeof(temperature_chr_val), "%.1f°C", temperature);

    /* Format humidity as string */
    hum_len = snprintf((char*)humidity_chr_val, sizeof(humidity_chr_val), "%.1f%%", humidity);

    /* Send notifications if client is subscribed */
    if (dht_chr_conn_handle_inited) {
        if (temperature_notify_status) {
            ble_gatts_notify(dht_chr_conn_handle, temperature_chr_val_handle);
        }
        if (humidity_notify_status) {
            ble_gatts_notify(dht_chr_conn_handle, humidity_chr_val_handle);
        }
        ESP_LOGI(TAG, "DHT data sent: Temp=%.1fC, Humidity=%.1f%%", temperature, humidity);
    }
}

/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt* ctxt, void* arg) {
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op) {
        /* Service register event */
        case BLE_GATT_REGISTER_OP_SVC:
            ESP_LOGD(TAG, "registered service %s with handle=%d", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
            break;

        /* Characteristic register event */
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGD(TAG,
                     "registering characteristic %s with "
                     "def_handle=%d val_handle=%d",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        /* Descriptor register event */
        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGD(TAG, "registering descriptor %s with handle=%d", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
            break;

        /* Unknown event */
        default:
            assert(0);
            break;
    }
}

/*
 *  GATT server subscribe event callback
 *      1. Update heart rate subscription status
 */

void gatt_svr_subscribe_cb(struct ble_gap_event* event) {
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d", event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d", event->subscribe.attr_handle);
    }

    /* Update connection handle */
    dht_chr_conn_handle = event->subscribe.conn_handle;
    dht_chr_conn_handle_inited = true;

    /* Check attribute handle and update subscription status */
    if (event->subscribe.attr_handle == temperature_chr_val_handle) {
        temperature_notify_status = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "temperature notifications %s", temperature_notify_status ? "enabled" : "disabled");
    } else if (event->subscribe.attr_handle == humidity_chr_val_handle) {
        humidity_notify_status = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "humidity notifications %s", humidity_notify_status ? "enabled" : "disabled");
    }
}

/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void) {
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
