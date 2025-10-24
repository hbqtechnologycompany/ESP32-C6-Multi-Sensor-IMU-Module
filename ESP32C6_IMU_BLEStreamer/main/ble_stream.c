#include "ble_stream.h"
#include "imu_ble.h"
#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include <string.h>
#include "led_status.h"

static const char *TAG = "BLE_STREAM";

static esp_gatt_if_t s_gatts_if = 0;
static uint16_t s_conn_id = 0xFFFF;
static uint16_t s_service_handle = 0;
static uint16_t s_char_handle = 0;
static bool s_notify_enabled = false;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t notify_ccc[2] = {0x00, 0x00};

#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
static const esp_ble_adv_params_t s_legacy_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
#else
static const uint8_t s_ext_adv_handle = 0;
static const esp_ble_gap_ext_adv_params_t s_ext_adv_params = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
    .interval_min = 0x20,
    .interval_max = 0x40,
    .channel_map = ADV_CHNL_ALL,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .tx_power = 0,
    .primary_phy = ESP_BLE_GAP_PRI_PHY_1M,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_1M,
    .sid = 0,
    .scan_req_notif = false,
};
static const esp_ble_gap_ext_adv_t s_ext_adv_start = {
    .instance = 0,
    .duration = 0,
    .max_events = 0,
};
#endif

static esp_gatts_attr_db_t gatt_db[] = {
    // Service Declaration
    [0] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                                  sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&(uint16_t){BLE_STREAM_SERVICE_UUID}}},

    // Characteristic Declaration
    [1] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                                  1, 1, (uint8_t *)&char_prop_notify}},

    // Characteristic Value (Notify)
    [2] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){BLE_STREAM_CHAR_DATA_UUID}, ESP_GATT_PERM_READ,
                                  244, 0, NULL}},

    // CCC Descriptor
    [3] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                  sizeof(notify_ccc), sizeof(notify_ccc), (uint8_t *)notify_ccc}},
};

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising((esp_ble_adv_params_t *)&s_legacy_adv_params);
        break;
#else
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:
        ESP_LOGI(TAG, "Ext adv params set: status=%d", param->ext_adv_set_params.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Ext adv data set: status=%d", param->ext_adv_data_set.status);
        if (param->ext_adv_data_set.status == ESP_BT_STATUS_SUCCESS) {
            esp_err_t r = esp_ble_gap_ext_adv_start(1, &s_ext_adv_start);
            if (r != ESP_OK) {
                ESP_LOGE(TAG, "ext adv start failed: %s", esp_err_to_name(r));
            }
        }
        break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "Ext adv start complete: status=%d", param->ext_adv_start.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Ext adv stop complete: status=%d", param->ext_adv_stop.status);
        break;
#endif
    case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
        ESP_LOGI(TAG, "PHY updated: status=%d tx=%d rx=%d",
                 param->phy_update.status,
                 param->phy_update.tx_phy,
                 param->phy_update.rx_phy);
        break;
    default:
        break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name(BLE_STREAM_DEVICE_NAME);
        // Set raw advertising data (flags + name)
        {
            uint8_t adv[31] = {0x02, 0x01, 0x06};
            const char *name = BLE_STREAM_DEVICE_NAME;
            size_t nlen = strlen(name);
            if (nlen > 26) nlen = 26;
            adv[3] = (uint8_t)(nlen + 1);
            adv[4] = 0x09; // Complete Local Name
            memcpy(&adv[5], name, nlen);
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
            esp_ble_gap_config_adv_data_raw(adv, 5 + nlen);
#else
            esp_err_t r = esp_ble_gap_ext_adv_set_params(s_ext_adv_handle, &s_ext_adv_params);
            if (r != ESP_OK) {
                ESP_LOGE(TAG, "ext adv set params failed: %s", esp_err_to_name(r));
            } else {
                r = esp_ble_gap_config_ext_adv_data_raw(s_ext_adv_handle, 5 + nlen, adv);
                if (r != ESP_OK) {
                    ESP_LOGE(TAG, "ext adv set data failed: %s", esp_err_to_name(r));
                }
            }
#endif
        }
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, 4, 0);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK) {
            s_service_handle = param->add_attr_tab.handles[0];
            s_char_handle = param->add_attr_tab.handles[2];
            esp_ble_gatts_start_service(s_service_handle);
        }
        break;
    case ESP_GATTS_CONNECT_EVT:
        s_conn_id = param->connect.conn_id;
        imu_ble_on_ble_connect();
        esp_ble_conn_update_params_t conn_params = {
            .latency = 0,
            .max_int = 6,   // ~7.5 ms
            .min_int = 6,
            .timeout = 400,
            .bda = {0},
        };
        memcpy(conn_params.bda, param->connect.remote_bda, ESP_BD_ADDR_LEN);
        esp_ble_gap_update_conn_params(&conn_params);
        // Request 2M PHY
        esp_ble_gap_set_preferred_phy(param->connect.remote_bda,
                                      0,
                                      ESP_BLE_GAP_PHY_2M_PREF_MASK,
                                      ESP_BLE_GAP_PHY_2M_PREF_MASK,
                                      ESP_BLE_GAP_PHY_OPTIONS_NO_PREF);
        // MTU request
        esp_ble_gatt_set_local_mtu(247);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        s_conn_id = 0xFFFF;
        s_notify_enabled = false;
        imu_ble_on_ble_disconnect();
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
        esp_ble_gap_start_advertising((esp_ble_adv_params_t *)&s_legacy_adv_params);
#else
        {
            esp_err_t r = esp_ble_gap_ext_adv_start(1, &s_ext_adv_start);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "ext adv restart failed: %s", esp_err_to_name(r));
            }
        }
#endif
        break;
    case ESP_GATTS_CONF_EVT:
        break;
    case ESP_GATTS_WRITE_EVT:
        // CCC written?
        if (param->write.handle == s_char_handle + 1 && param->write.len >= 2) {
            s_notify_enabled = (param->write.value[0] & 0x01);
            ESP_LOGI(TAG, "Notify %s", s_notify_enabled ? "EN" : "DIS");
            imu_ble_on_notifications_changed(s_notify_enabled);
        }
        break;
    default:
        break;
    }
}

esp_err_t ble_stream_init(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_cb));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0x42));

    // Prefer 2M PHY by default
    esp_ble_gap_set_preferred_default_phy(ESP_BLE_GAP_PHY_2M_PREF_MASK,
                                          ESP_BLE_GAP_PHY_2M_PREF_MASK);
    return ESP_OK;
}

esp_err_t ble_stream_start(void)
{
    // Advertising started in GAP callback after adv data set
    return ESP_OK;
}

esp_err_t ble_stream_notify(const uint8_t *data, uint16_t len)
{
    if (!s_notify_enabled || s_conn_id == 0xFFFF || s_char_handle == 0) return ESP_ERR_INVALID_STATE;
    esp_err_t r = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_char_handle, len, (uint8_t *)data, false);
    return r;
}
