#include <rtconfig.h>
#include "ble_hid.h"

#if defined(BLUETOOTH)
#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "bf0_ble_gap.h"
#include "bf0_ble_common.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"
#define LOG_TAG "pomo_hid"
#include "log.h"

#define HID_REPORT_BITS 6
#define HID_REPORT_BYTES 1
#define HID_CMD_MSG_BASE 0x100
#define BASE_USB_HID_SPEC_VERSION 0x0101

SIBLES_ADVERTISING_CONTEXT_DECLAR(g_app_advertising_context);

uint8_t sibles_advertising_disc_mode_get(void)
{
    return GAPM_ADV_MODE_GEN_DISC;
}

enum {
    HIDS_REMOTE_WAKE = 1,
    HIDS_NORMALLY_CONNECTABLE = 2,
};

typedef struct {
    uint16_t version;
    uint8_t code;
    uint8_t flags;
} __packed hids_info_t;

typedef struct {
    uint8_t id;
    uint8_t type;
} __packed hids_report_t;

typedef struct {
    uint8_t is_power_on;
    uint8_t is_connected;
    uint8_t conn_idx;
    struct {
        sibles_hdl srv_handle;
        uint8_t is_config_on;
    } data;
    rt_mailbox_t mb_handle;
} ble_hid_env_t;

static ble_hid_env_t ble_env;
static uint8_t hid_consumer_state;
static uint16_t hid_report_ntf_cfg;
static uint8_t hid_ctrl_point;

static hids_info_t hid_info = {
    .version = BASE_USB_HID_SPEC_VERSION,
    .code = 0,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};
static hids_report_t hid_input_report = { .id = 0, .type = 1 };

/* HID Consumer Control Report Map for 6 media actions:
 * Bit 0: Volume Up (0x09, 0xE9)
 * Bit 1: Volume Down (0x09, 0xEA)
 * Bit 2: Mute (0x09, 0xE2)
 * Bit 3: Scan Previous Track (0x09, 0xB6)
 * Bit 4: Play/Pause (0x09, 0xCD)
 * Bit 5: Scan Next Track (0x09, 0xB5)
 * Bits 6-7: Constant padding (2 bits)
 * Total: 8 bits = 1 byte
 */
static const uint8_t hid_report_map[] = {
    0x05, 0x0C,                         /* Usage Page (Consumer) */
    0x09, 0x01,                         /* Usage (Consumer Control) */
    0xA1, 0x01,                         /* Collection (Application) */
    0x15, 0x00,                         /* Logical Minimum (0) */
    0x25, 0x01,                         /* Logical Maximum (1) */
    0x75, 0x01,                         /* Report Size (1) */
    0x95, 0x01,                         /* Report Count (1) */
    0x09, 0xE9, 0x81, 0x06,             /* Usage (Volume Up) -> Bit 0 */
    0x09, 0xEA, 0x81, 0x06,             /* Usage (Volume Down) -> Bit 1 */
    0x09, 0xE2, 0x81, 0x06,             /* Usage (Mute) -> Bit 2 */
    0x09, 0xB6, 0x81, 0x06,             /* Usage (Scan Previous Track) -> Bit 3 */
    0x09, 0xCD, 0x81, 0x06,             /* Usage (Play/Pause) -> Bit 4 */
    0x09, 0xB5, 0x81, 0x06,             /* Usage (Scan Next Track) -> Bit 5 */
    0x75, 0x02, 0x95, 0x01, 0x81, 0x03, /* Constant Padding (2 bits) -> Bits 6-7 */
    0xC0,                               /* End Collection */
};

enum {
    HIDS_IDX_SVC,
    HIDS_IDX_INFO_CHAR,
    HIDS_IDX_INFO_VAL,
    HIDS_IDX_REPORT_MAP,
    HIDS_IDX_REPORT_MAP_VAL,
    HIDS_IDX_REPORT,
    HIDS_IDX_REPORT_VAL,
    HIDS_IDX_REPORT_REF,
    HIDS_IDX_REPORT_NTF_CFG,
    HIDS_IDX_CTRL,
    HIDS_IDX_CTRL_VAL,
    HIDS_ATT_NB,
};

static struct attm_desc hid_att_db[] = {
    [HIDS_IDX_SVC] = { ATT_DECL_PRIMARY_SERVICE, PERM(RD, ENABLE), 0, 0 },
    [HIDS_IDX_INFO_CHAR] = { ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0 },
    [HIDS_IDX_INFO_VAL] = { ATT_CHAR_HID_INFO, PERM(RD, ENABLE), PERM(RI, ENABLE), sizeof(hid_info) },
    [HIDS_IDX_REPORT_MAP] = { ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0 },
    [HIDS_IDX_REPORT_MAP_VAL] = { ATT_CHAR_REPORT_MAP, PERM(RD, ENABLE), PERM(RI, ENABLE), sizeof(hid_report_map) },
    [HIDS_IDX_REPORT] = { ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0 },
    [HIDS_IDX_REPORT_VAL] = {
        ATT_CHAR_REPORT,
        PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(WRITE_COMMAND, ENABLE) |
        PERM(NTF, ENABLE) | PERM(WP, UNAUTH),
        PERM(UUID_LEN, UUID_16) | PERM(RI, ENABLE), HID_REPORT_BYTES
    },
    [HIDS_IDX_REPORT_NTF_CFG] = {
        ATT_DESC_CLIENT_CHAR_CFG, PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE) |
        PERM(WP, UNAUTH), PERM(RI, ENABLE), 2
    },
    [HIDS_IDX_REPORT_REF] = { ATT_DESC_REPORT_REF, PERM(RD, ENABLE), PERM(RI, ENABLE), 2 },
    [HIDS_IDX_CTRL] = { ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0 },
    [HIDS_IDX_CTRL_VAL] = { ATT_CHAR_HID_CTNL_PT, PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE), 0, 1 },
};

static uint8_t *hid_get_cb(uint8_t conn_idx, uint8_t idx, uint16_t *len)
{
    (void)conn_idx;
    *len = 0;
    switch (idx) {
    case HIDS_IDX_INFO_VAL:
        *len = sizeof(hid_info);
        return (uint8_t *)&hid_info;
    case HIDS_IDX_REPORT_MAP_VAL:
        *len = sizeof(hid_report_map);
        return (uint8_t *)hid_report_map;
    case HIDS_IDX_REPORT_VAL:
        *len = sizeof(hid_consumer_state);
        return &hid_consumer_state;
    case HIDS_IDX_REPORT_NTF_CFG:
        *len = sizeof(hid_report_ntf_cfg);
        return (uint8_t *)&hid_report_ntf_cfg;
    case HIDS_IDX_CTRL_VAL:
        *len = sizeof(hid_ctrl_point);
        return &hid_ctrl_point;
    case HIDS_IDX_REPORT_REF:
        *len = sizeof(hid_input_report);
        return (uint8_t *)&hid_input_report;
    default:
        return NULL;
    }
}

static uint8_t hid_set_cb(uint8_t conn_idx, sibles_set_cbk_t *param)
{
    (void)conn_idx;
    if (param->idx == HIDS_IDX_REPORT_NTF_CFG && param->len >= 1) {
        if (param->len >= 2) {
            memcpy(&hid_report_ntf_cfg, param->value, 2);
        } else {
            hid_report_ntf_cfg = *param->value;
        }
        ble_env.data.is_config_on = (uint8_t)(hid_report_ntf_cfg & 0x01);
    } else if (param->idx == HIDS_IDX_CTRL_VAL && param->len <= sizeof(hid_ctrl_point)) {
        memcpy(&hid_ctrl_point, param->value, param->len);
    }
    return 0;
}

static void ble_hid_service_init(void)
{
    sibles_register_svc_t service = {0};
    service.att_db = hid_att_db;
    service.num_entry = HIDS_ATT_NB;
    service.sec_lvl = PERM(SVC_AUTH, NO_AUTH) | PERM(SVC_UUID_LEN, UUID_16);
    service.uuid = ATT_SVC_HID;
    ble_env.data.srv_handle = sibles_register_svc(&service);
    rt_kprintf("[pomo_hid] service_init handle=%p\n", (void *)ble_env.data.srv_handle);
    if (ble_env.data.srv_handle)
        sibles_register_cbk(ble_env.data.srv_handle, hid_get_cb, hid_set_cb);
}

static int ble_hid_report_send(void)
{
    sibles_value_t value;
    if (!ble_env.is_connected || !ble_env.data.srv_handle) {
        rt_kprintf("[pomo_hid] report send skipped: conn=%d srv=%p\n",
                   ble_env.is_connected, (void *)ble_env.data.srv_handle);
        return -1;
    }
    value.hdl = ble_env.data.srv_handle;
    value.idx = HIDS_IDX_REPORT_VAL;
    value.len = HID_REPORT_BYTES;
    value.value = &hid_consumer_state;

    int ret = sibles_write_value(ble_env.conn_idx, &value);
    if (ret == 0) {
        for (int retry = 0; retry < 5; retry++) {
            rt_thread_mdelay(10);
            ret = sibles_write_value(ble_env.conn_idx, &value);
            if (ret > 0) break;
        }
    }
    rt_kprintf("[pomo_hid] report send state=0x%02x ret=%d\n", hid_consumer_state, ret);
    return ret;
}

static void ble_hid_key_set(ble_hid_command_t cmd)
{
    hid_consumer_state |= (uint8_t)(1u << cmd);
    int ret = ble_hid_report_send();
    rt_kprintf("[pomo_hid] key_set cmd=%d mask=0x%02x ret=%d\n", cmd, hid_consumer_state, ret);
}

static void ble_hid_key_clear(ble_hid_command_t cmd)
{
    hid_consumer_state &= (uint8_t)~(1u << cmd);
    int ret = ble_hid_report_send();
    rt_kprintf("[pomo_hid] key_clear cmd=%d mask=0x%02x ret=%d\n", cmd, hid_consumer_state, ret);
}

static uint8_t ble_hid_advertising_event(uint8_t event, void *context, void *data)
{
    (void)context;
    switch (event) {
    case SIBLES_ADV_EVT_ADV_STARTED: {
        sibles_adv_evt_startted_t *evt = (sibles_adv_evt_startted_t *)data;
        rt_kprintf("[pomo_hid] ADV started status=%d mode=%d\n",
                   evt ? evt->status : -1, evt ? evt->adv_mode : -1);
        break;
    }
    case SIBLES_ADV_EVT_ADV_STOPPED: {
        sibles_adv_evt_stopped_t *evt = (sibles_adv_evt_stopped_t *)data;
        rt_kprintf("[pomo_hid] ADV stopped reason=%d mode=%d\n",
                   evt ? evt->reason : -1, evt ? evt->adv_mode : -1);
        break;
    }
    default:
        rt_kprintf("[pomo_hid] ADV event=%u\n", event);
        break;
    }
    return 0;
}

static void ble_hid_advertising_start(void)
{
    sibles_advertising_para_t para = {0};
    uint8_t manufacturer_data[] = {0x20, 0xC4, 0x00, 0x91};
    char local_name[] = "POMO_MEMO_HID";
    uint16_t uuid_hid = ATT_SVC_HID;
    uint16_t appearance = 962;

    ble_gap_dev_name_t *dev_name = rt_malloc(sizeof(ble_gap_dev_name_t) + strlen(local_name));
    if (dev_name) {
        dev_name->len = strlen(local_name);
        memcpy(dev_name->name, local_name, dev_name->len);
        ble_gap_set_dev_name(dev_name);
        rt_free(dev_name);
    }

    para.own_addr_type = GAPM_STATIC_ADDR;
    para.config.adv_mode = SIBLES_ADV_CONNECT_MODE;
    para.config.mode_config.conn_config.duration = 0;
    para.config.mode_config.conn_config.interval = 0x30;
    para.config.max_tx_pwr = 0x7F;
    para.config.is_auto_restart = 1;
    para.rsp_data.completed_name = rt_malloc(strlen(local_name) + sizeof(sibles_adv_type_name_t));
    if (para.rsp_data.completed_name) {
        para.rsp_data.completed_name->name_len = strlen(local_name);
        memcpy(para.rsp_data.completed_name->name, local_name, strlen(local_name));
    }
    para.adv_data.completed_uuid = rt_malloc(sizeof(sibles_adv_type_srv_uuid_t) + 1 + sizeof(ATT_UUID_16_LEN));
    if (para.adv_data.completed_uuid) {
        para.adv_data.completed_uuid->count = 1;
        para.adv_data.completed_uuid->uuid_list[0].uuid_len = 2;
        memcpy(para.adv_data.completed_uuid->uuid_list[0].uuid.uuid_16, &uuid_hid, 2);
    }
    para.adv_data.appearance = rt_malloc(2);
    if (para.adv_data.appearance) {
        memcpy(para.adv_data.appearance, &appearance, 2);
    }
    para.adv_data.manufacturer_data = rt_malloc(sizeof(sibles_adv_type_manufacturer_data_t) + sizeof(manufacturer_data));
    if (para.adv_data.manufacturer_data) {
        para.adv_data.manufacturer_data->company_id = SIG_SIFLI_COMPANY_ID;
        para.adv_data.manufacturer_data->data_len = sizeof(manufacturer_data);
        memcpy(para.adv_data.manufacturer_data->additional_data, manufacturer_data, sizeof(manufacturer_data));
    }
    para.evt_handler = ble_hid_advertising_event;
    uint8_t result = sibles_advertising_init(g_app_advertising_context, &para);
    rt_kprintf("[pomo_hid] advertising init=%u\n", result);
    if (result == SIBLES_ADV_NO_ERR) {
        uint8_t start_res = sibles_advertising_start(g_app_advertising_context);
        rt_kprintf("[pomo_hid] advertising start result=%u (name=%s)\n", start_res, local_name);
    } else {
        rt_kprintf("[pomo_hid] advertising init failed error=%u\n", result);
    }
    rt_free(para.rsp_data.completed_name);
    rt_free(para.adv_data.completed_uuid);
    rt_free(para.adv_data.appearance);
    rt_free(para.adv_data.manufacturer_data);
}

static void ble_hid_thread_entry(void *parameter)
{
    rt_uint32_t value;
    (void)parameter;
    rt_kprintf("[pomo_hid] thread entry running\n");
    while (rt_mb_recv(ble_env.mb_handle, &value, RT_WAITING_FOREVER) == RT_EOK) {
        if (value == BLE_POWER_ON_IND) {
            if (ble_env.is_power_on) {
                rt_kprintf("[pomo_hid] BLE power on already initialized, ignoring\n");
                continue;
            }
            ble_env.is_power_on = 1;
            rt_kprintf("[pomo_hid] BLE power ready; configure Just Works bonding\n");
            /* Configure no-input/no-output, no-MITM Just Works with bonding */
            connection_manager_set_bond_ack(BOND_ACCEPT);
            connection_manager_set_bond_cnf_accept(true);
            connection_manager_set_bond_cnf_iocap(GAP_IO_CAP_NO_INPUT_NO_OUTPUT);
            connection_manager_set_bond_cnf_auth(GAP_AUTH_REQ_NO_MITM_BOND);
            connection_manager_set_bond_cnf_sec(GAP_SEC1_NOAUTH_PAIR_ENC);
            connection_manager_set_connected_auth(GAP_AUTH_REQ_NO_MITM_BOND);

            ble_hid_service_init();
            ble_hid_advertising_start();
        } else if (value >= HID_CMD_MSG_BASE && value <= HID_CMD_MSG_BASE + (rt_uint32_t)BLE_HID_NEXT) {
            ble_hid_command_t cmd = (ble_hid_command_t)(value - HID_CMD_MSG_BASE);
            if (ble_env.is_connected && ble_env.data.srv_handle) {
                rt_kprintf("[pomo_hid] dispatching command %d\n", cmd);
                ble_hid_key_set(cmd);
                rt_thread_mdelay(20);
                ble_hid_key_clear(cmd);
                rt_kprintf("[pomo_hid] command %d dispatch complete\n", cmd);
            } else {
                rt_kprintf("[pomo_hid] drop command %d (conn=%d srv=%p)\n",
                           cmd, ble_env.is_connected, (void *)ble_env.data.srv_handle);
            }
        }
    }
}

static int ble_hid_event_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    (void)len;
    (void)context;
    switch (event_id) {
    case BLE_POWER_ON_IND:
        rt_kprintf("[pomo_hid] event BLE_POWER_ON_IND received\n");
        if (ble_env.mb_handle) rt_mb_send(ble_env.mb_handle, BLE_POWER_ON_IND);
        break;
    case BLE_GAP_CONNECTED_IND: {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;
        ble_env.conn_idx = ind->conn_idx;
        ble_env.is_connected = 1;
        ble_env.data.is_config_on = 1;
        rt_kprintf("[pomo_hid] connected conn=%u peer=%02x:%02x:%02x:%02x:%02x:%02x intv=%u\n",
                   ble_env.conn_idx,
                   ind->peer_addr.addr[5], ind->peer_addr.addr[4], ind->peer_addr.addr[3],
                   ind->peer_addr.addr[2], ind->peer_addr.addr[1], ind->peer_addr.addr[0],
                   ind->con_interval);
        connection_manager_set_bond_cnf_auth(GAP_AUTH_REQ_NO_MITM_BOND);
        break;
    }
    case BLE_GAP_DISCONNECTED_IND: {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        rt_kprintf("[pomo_hid] disconnected conn=%u reason=0x%02x\n",
                   ind ? ind->conn_idx : ble_env.conn_idx,
                   ind ? ind->reason : 0);
        ble_env.is_connected = 0;
        hid_consumer_state = 0;
        break;
    }
    case BLE_GAP_BOND_IND: {
        ble_gap_bond_ind_t *ind = (ble_gap_bond_ind_t *)data;
        if (ind) {
            if (ind->info == GAPC_PAIRING_SUCCEED) {
                rt_kprintf("[pomo_hid] bond succeed auth=%u ltk=%d\n",
                           ind->data.auth.info, (int)ind->data.auth.ltk_present);
            } else if (ind->info == GAPC_PAIRING_FAILED) {
                rt_kprintf("[pomo_hid] bond failed reason=0x%02x\n", ind->data.reason);
            } else {
                rt_kprintf("[pomo_hid] bond info=%u\n", ind->info);
            }
        }
        break;
    }
    case BLE_GAP_ENCRYPT_IND:
        rt_kprintf("[pomo_hid] link encrypted\n");
        break;
    case CONNECTION_MANAGER_BOND_AUTH_INFOR: {
        connection_manager_bond_ack_infor_t *info = (connection_manager_bond_ack_infor_t *)data;
        rt_kprintf("[pomo_hid] bond auth infor request=%d\n", info ? info->request : -1);
        if (info) {
            if (info->request == GAPC_PAIRING_REQ) {
                connection_manager_bond_ack_reply(info->conn_idx, GAPC_PAIRING_REQ, true);
            } else if (info->request == GAPC_TK_EXCH) {
                connection_manager_bond_ack_reply(info->conn_idx, GAPC_TK_EXCH, true);
            } else if (info->request == GAPC_NC_EXCH) {
                connection_manager_bond_ack_reply(info->conn_idx, GAPC_NC_EXCH, true);
            }
        }
        break;
    }
    default:
        break;
    }
    return 0;
}
BLE_EVENT_REGISTER(ble_hid_event_handler, NULL);

void ble_hid_init(void)
{
    rt_kprintf("[pomo_hid] ble_hid_init started\n");
    if (ble_env.mb_handle) {
        rt_kprintf("[pomo_hid] already initialized\n");
        return;
    }
    rt_thread_t thread;
    ble_env.mb_handle = rt_mb_create("hid", 8, RT_IPC_FLAG_FIFO);
    if (!ble_env.mb_handle) {
        rt_kprintf("[pomo_hid] failed to create mailbox\n");
        return;
    }
    thread = rt_thread_create("pomo_hid", ble_hid_thread_entry, NULL, 4096, 20, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK) {
        rt_kprintf("[pomo_hid] failed to start thread\n");
        return;
    }
    rt_kprintf("[pomo_hid] calling sifli_ble_enable\n");
    sifli_ble_enable();
}

int ble_hid_send(ble_hid_command_t command)
{
    if (!ble_hid_connected()) {
        rt_kprintf("[pomo_hid] ble_hid_send not connected (cmd=%d)\n", command);
        return -1;
    }
    if (command < BLE_HID_VOLUME_UP || command > BLE_HID_NEXT) {
        rt_kprintf("[pomo_hid] ble_hid_send invalid command %d\n", command);
        return -2;
    }
    if (!ble_env.mb_handle) {
        rt_kprintf("[pomo_hid] ble_hid_send mailbox not initialized\n");
        return -3;
    }
    rt_uint32_t msg = HID_CMD_MSG_BASE + (rt_uint32_t)command;
    rt_err_t err = rt_mb_send(ble_env.mb_handle, msg);
    if (err != RT_EOK) {
        rt_kprintf("[pomo_hid] ble_hid_send mb_send err=%d\n", err);
        return -4;
    }
    rt_kprintf("[pomo_hid] ble_hid_send cmd=%d queued\n", command);
    return 0;
}
bool ble_hid_connected(void)
{
    return (ble_env.is_connected != 0);
}

#else

void ble_hid_init(void) {}
int ble_hid_send(ble_hid_command_t command) { (void)command; return -1; }
bool ble_hid_connected(void) { return false; }

#endif
