/* REQ-203：板端 BLE 文件传输服务（自建 128-bit GATT）。
 *
 * 收：cmd 特征（WRITE_REQ/WRITE_COMMAND）按字节累积成行，交给工作线程解析；
 * 发：data 特征（NTF）逐行回 V1 / F / OK / B / D / E / X。
 * 解析与读盘都不在 BLE 回调里做，回调只负责入队。
 */
#include <rtconfig.h>
#include "ble_file.h"

#if defined(BLUETOOTH)

#include <fcntl.h>
#include <string.h>

#include <rtthread.h>
#include "dfs_file.h"
#include "dfs_posix.h"

#include "bf0_ble_common.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"

#define BLE_FILE_TAG         "blefile"
#define BLE_FILE_PROTO       1
#define BLE_FILE_ROOT        "/"
#define BLE_FILE_LINE        64                          /* 单条命令行缓冲（含结尾 0） */
#define BLE_FILE_QUEUE       4                           /* 待解析命令行条数 */
#define BLE_FILE_CHUNK_MAX   192                         /* 单个 D 帧的原始字节上限 */
#define BLE_FILE_CHUNK_MIN   8
#define BLE_FILE_MTU_DEFAULT 23
#define BLE_FILE_NOTIFY_MAX  (3 + 2 * BLE_FILE_CHUNK_MAX + 1)
#define BLE_FILE_RETRY       20
#define BLE_FILE_RETRY_MS    10
#define BLE_FILE_PROGRESS    65536u

/* 需要在工作线程里做的事件（BLE 回调线程里注册服务/交换 MTU 会卡住上电流程）。 */
#define BLE_FILE_PENDING_SVC 0x01
#define BLE_FILE_PENDING_MTU 0x02

/* 128-bit UUID 按 BLE 规定以小端（LSB First）存放，与 canonical 文本相反：
 *   svc  c0de0001-5f1f-4b1e-9c3a-706f6d6f0001
 *   cmd  c0de0002-5f1f-4b1e-9c3a-706f6d6f0001
 *   data c0de0003-5f1f-4b1e-9c3a-706f6d6f0001
 */
#define BLE_FILE_UUID_SVC_BYTES  {                                          \
    0x01, 0x00, 0x6f, 0x6d, 0x6f, 0x70, 0x3a, 0x9c,                         \
    0x1e, 0x4b, 0x1f, 0x5f, 0x01, 0x00, 0xde, 0xc0 }
#define BLE_FILE_UUID_CMD_BYTES  {                                          \
    0x01, 0x00, 0x6f, 0x6d, 0x6f, 0x70, 0x3a, 0x9c,                         \
    0x1e, 0x4b, 0x1f, 0x5f, 0x02, 0x00, 0xde, 0xc0 }
#define BLE_FILE_UUID_DATA_BYTES {                                          \
    0x01, 0x00, 0x6f, 0x6d, 0x6f, 0x70, 0x3a, 0x9c,                         \
    0x1e, 0x4b, 0x1f, 0x5f, 0x03, 0x00, 0xde, 0xc0 }

/* 16-bit UUID 在 128-bit 表里仍然按 LSB First 填 2 字节。 */
#define BLE_FILE_UUID_16(x) {((uint8_t)((x) & 0xff)), ((uint8_t)((x) >> 8))}

static uint8_t ble_file_uuid_svc[ATT_UUID_128_LEN] = BLE_FILE_UUID_SVC_BYTES;

enum {
    IDX_SVC,
    IDX_CMD_CHAR,
    IDX_CMD_VAL,
    IDX_DATA_CHAR,
    IDX_DATA_VAL,
    IDX_DATA_CCCD,
    IDX_ATT_NB,
};

static struct attm_desc_128 ble_file_att_db[IDX_ATT_NB] = {
    [IDX_SVC] = { BLE_FILE_UUID_16(ATT_DECL_PRIMARY_SERVICE), PERM(RD, ENABLE), 0, 0 },
    [IDX_CMD_CHAR] = { BLE_FILE_UUID_16(ATT_DECL_CHARACTERISTIC), PERM(RD, ENABLE), 0, 0 },
    [IDX_CMD_VAL] = {
        BLE_FILE_UUID_CMD_BYTES,
        PERM(WRITE_REQ, ENABLE) | PERM(WRITE_COMMAND, ENABLE),
        PERM(UUID_LEN, UUID_128) | PERM(RI, ENABLE), BLE_FILE_LINE
    },
    [IDX_DATA_CHAR] = { BLE_FILE_UUID_16(ATT_DECL_CHARACTERISTIC), PERM(RD, ENABLE), 0, 0 },
    [IDX_DATA_VAL] = {
        BLE_FILE_UUID_DATA_BYTES,
        PERM(NTF, ENABLE),
        PERM(UUID_LEN, UUID_128) | PERM(RI, ENABLE), BLE_FILE_NOTIFY_MAX
    },
    [IDX_DATA_CCCD] = {
        BLE_FILE_UUID_16(ATT_DESC_CLIENT_CHAR_CFG),
        PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
        PERM(RI, ENABLE), 2
    },
};

typedef struct {
    sibles_hdl srv_handle;
    uint8_t conn_idx;
    uint8_t connected;
    uint8_t enabled;
    uint8_t ntf_on;
    uint16_t mtu;
    volatile uint8_t transfer_active;
    bool (*busy)(void);
} ble_file_env_t;

static ble_file_env_t ble_file_env = { .mtu = BLE_FILE_MTU_DEFAULT };

static uint16_t ble_file_ntf_cfg;
static uint8_t ble_file_ntf_warned;
static volatile uint8_t ble_file_pending;
static uint8_t ble_file_chunk_buf[BLE_FILE_CHUNK_MAX];
static uint8_t ble_file_notify_buf[BLE_FILE_NOTIFY_MAX];
static uint8_t ble_file_probe_buf[64];

static char ble_file_cmd_line[BLE_FILE_LINE];
static uint16_t ble_file_cmd_used;
static char ble_file_cmd_queue[BLE_FILE_QUEUE][BLE_FILE_LINE];
static volatile uint8_t ble_file_cmd_head;
static volatile uint8_t ble_file_cmd_tail;
static rt_sem_t ble_file_cmd_sem;

static void ble_file_service_init(void);

static uint16_t ble_file_chunk_bytes(void)
{
    uint16_t mtu = ble_file_env.mtu ? ble_file_env.mtu : BLE_FILE_MTU_DEFAULT;
    uint16_t chunk = mtu > 7 ? (uint16_t)((mtu - 7) / 2) : 0;   /* 3 + 2n + 1 <= mtu - 3 */
    if (chunk < BLE_FILE_CHUNK_MIN) chunk = BLE_FILE_CHUNK_MIN;
    if (chunk > BLE_FILE_CHUNK_MAX) chunk = BLE_FILE_CHUNK_MAX;
    return chunk;
}

/* 只接受 memoNNNN.wav（12 字符），防止目录穿越与越界读取。 */
static bool ble_file_name_ok(const char *name)
{
    size_t n;
    int i;
    if (!name) return false;
    n = strlen(name);
    if (n != 12 || strncmp(name, "memo", 4) != 0) return false;
    for (i = 4; i < 8; ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    return strcmp(name + 8, ".wav") == 0;
}

static int ble_file_size_of(const char *name, uint32_t *size)
{
    struct stat st;
    struct dfs_fd fd;
    char path[16];
    uint32_t total = 0;
    int r;

    if (!ble_file_name_ok(name) || !size) return -1;
    rt_snprintf(path, sizeof path, BLE_FILE_ROOT "%s", name);
    if (dfs_file_stat(path, &st) == 0) {
        *size = (uint32_t)st.st_size;
        return 0;
    }
    /* stat 不可用时退化为数一遍字节数。 */
    if (dfs_file_open(&fd, path, O_RDONLY) != 0) return -1;
    while ((r = dfs_file_read(&fd, ble_file_probe_buf, sizeof ble_file_probe_buf)) > 0)
        total += (uint32_t)r;
    dfs_file_close(&fd);
    if (r < 0) return -1;
    *size = total;
    return 0;
}

static unsigned ble_file_count(void)
{
    DIR *dir;
    struct dirent *entry;
    unsigned count = 0;

    dir = opendir(BLE_FILE_ROOT);
    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL)
        if (ble_file_name_ok(entry->d_name)) count++;
    closedir(dir);
    return count;
}

static int ble_file_notify(const uint8_t *data, uint16_t len)
{
    sibles_value_t value;
    int ret;
    int i;

    if (!ble_file_env.connected || !ble_file_env.srv_handle) return -1;
    if (!ble_file_env.ntf_on) {
        if (!ble_file_ntf_warned) {
            ble_file_ntf_warned = 1;
            rt_kprintf("[%s] host has not enabled notify\n", BLE_FILE_TAG);
        }
        return -1;
    }
    value.hdl = ble_file_env.srv_handle;
    value.idx = IDX_DATA_VAL;
    value.len = len;
    value.value = (uint8_t *)data;

    ret = sibles_write_value(ble_file_env.conn_idx, &value);
    for (i = 0; i < BLE_FILE_RETRY && ret == 0 && ble_file_env.connected; ++i)
    {
        rt_thread_mdelay(BLE_FILE_RETRY_MS);
        ret = sibles_write_value(ble_file_env.conn_idx, &value);
    }
    return ret;
}

static bool ble_file_send(const uint8_t *data, uint16_t len)
{
    if (!data || !len) return false;
    return ble_file_notify(data, len) == (int)len;
}

static bool ble_file_send_text(const char *text)
{
    if (!text) return false;
    return ble_file_send((const uint8_t *)text, (uint16_t)strlen(text));
}

static void ble_file_send_error(int code)
{
    char line[8];
    rt_snprintf(line, sizeof line, "X %d\n", code);
    ble_file_send_text(line);
}

static uint16_t ble_file_hex_line(uint8_t *out, const uint8_t *data, uint16_t len)
{
    static const char hex[] = "0123456789abcdef";
    uint16_t pos = 0;
    uint16_t i;

    out[pos++] = 'D';
    out[pos++] = ' ';
    for (i = 0; i < len; ++i)
    {
        out[pos++] = (uint8_t)hex[data[i] >> 4];
        out[pos++] = (uint8_t)hex[data[i] & 0x0f];
    }
    out[pos++] = '\n';
    return pos;
}

static void ble_file_send_version(void)
{
    char line[24];
    rt_snprintf(line, sizeof line, "V%d %u\n", BLE_FILE_PROTO, ble_file_count());
    ble_file_send_text(line);
}

static void ble_file_send_list(void)
{
    DIR *dir;
    struct dirent *entry;
    char line[BLE_FILE_LINE];

    dir = opendir(BLE_FILE_ROOT);
    if (!dir) {
        rt_kprintf("[%s] list: no filesystem\n", BLE_FILE_TAG);
        ble_file_send_error(2);
        return;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        uint32_t size = 0;
        int len;
        if (!ble_file_name_ok(entry->d_name)) continue;
        if (ble_file_size_of(entry->d_name, &size) != 0) continue;
        len = rt_snprintf(line, sizeof line, "F %s %u\n", entry->d_name, (unsigned)size);
        if (len <= 0 || !ble_file_send_text(line)) break;
    }
    closedir(dir);
    ble_file_send_text("OK\n");
}

static void ble_file_send_file(const char *name)
{
    struct dfs_fd fd;
    char path[16];
    char line[BLE_FILE_LINE];
    uint32_t size = 0;
    uint32_t sent = 0;
    uint32_t next_log = BLE_FILE_PROGRESS;
    uint32_t start_ms;
    uint16_t chunk;
    int len;

    if (!ble_file_name_ok(name)) {
        rt_kprintf("[%s] bad name\n", BLE_FILE_TAG);
        ble_file_send_error(3);
        return;
    }
    if (ble_file_env.transfer_active ||
        (ble_file_env.busy && ble_file_env.busy())) {
        rt_kprintf("[%s] busy, reject %s\n", BLE_FILE_TAG, name);
        ble_file_send_error(9);
        return;
    }
    if (!ble_file_env.connected) return;
    if (ble_file_size_of(name, &size) != 0) {
        rt_kprintf("[%s] no such file %s\n", BLE_FILE_TAG, name);
        ble_file_send_error(1);
        return;
    }
    rt_snprintf(path, sizeof path, BLE_FILE_ROOT "%s", name);
    if (dfs_file_open(&fd, path, O_RDONLY) != 0) {
        rt_kprintf("[%s] open failed %s\n", BLE_FILE_TAG, name);
        ble_file_send_error(1);
        return;
    }

    ble_file_env.transfer_active = 1;
    len = rt_snprintf(line, sizeof line, "B %s %u\n", name, (unsigned)size);
    if (len <= 0 || !ble_file_send_text(line)) goto out;

    chunk = ble_file_chunk_bytes();
    start_ms = rt_tick_get_millisecond();
    rt_kprintf("[%s] send %s %u B chunk=%u\n", BLE_FILE_TAG, name, (unsigned)size, chunk);
    while (sent < size)
    {
        uint32_t left = size - sent;
        uint16_t want = (uint16_t)(left > chunk ? chunk : left);
        int r;

        if (!ble_file_env.connected) break;
        r = dfs_file_read(&fd, ble_file_chunk_buf, want);
        if (r <= 0) {
            rt_kprintf("[%s] read failed at %u/%u\n", BLE_FILE_TAG, (unsigned)sent, (unsigned)size);
            ble_file_send_error(2);
            goto out;
        }
        if (!ble_file_send(ble_file_notify_buf,
                           ble_file_hex_line(ble_file_notify_buf, ble_file_chunk_buf, (uint16_t)r))) {
            rt_kprintf("[%s] notify failed at %u/%u\n", BLE_FILE_TAG, (unsigned)sent, (unsigned)size);
            goto out;
        }
        sent += (uint32_t)r;
        if (sent >= next_log) {
            rt_kprintf("[%s] progress %u/%u\n", BLE_FILE_TAG, (unsigned)sent, (unsigned)size);
            next_log += BLE_FILE_PROGRESS;
        }
    }
    if (sent == size) {
        ble_file_send_text("E\n");
        rt_kprintf("[%s] sent %s %u B in %u ms\n", BLE_FILE_TAG, name, (unsigned)sent,
                   (unsigned)(rt_tick_get_millisecond() - start_ms));
    }
out:
    dfs_file_close(&fd);
    ble_file_env.transfer_active = 0;
}

static void ble_file_dispatch(const char *line)
{
    if (strcmp(line, "V") == 0) {
        ble_file_send_version();
    } else if (strcmp(line, "L") == 0) {
        ble_file_send_list();
    } else if (strncmp(line, "G ", 2) == 0) {
        ble_file_send_file(line + 2);
    } else {
        rt_kprintf("[%s] unknown cmd '%s'\n", BLE_FILE_TAG, line);
        ble_file_send_error(3);
    }
}

static void ble_file_enqueue(const char *line)
{
    uint8_t next = (uint8_t)((ble_file_cmd_head + 1) % BLE_FILE_QUEUE);

    if (next == ble_file_cmd_tail) {
        rt_kprintf("[%s] cmd queue full, drop '%s'\n", BLE_FILE_TAG, line);
        return;
    }
    rt_snprintf(ble_file_cmd_queue[ble_file_cmd_head], BLE_FILE_LINE, "%s", line);
    ble_file_cmd_head = next;
    if (ble_file_cmd_sem) rt_sem_release(ble_file_cmd_sem);
}

static void ble_file_worker_entry(void *parameter)
{
    (void)parameter;
    while (1)
    {
        char line[BLE_FILE_LINE];
        uint8_t pending;
        if (rt_sem_take(ble_file_cmd_sem, RT_WAITING_FOREVER) != RT_EOK) continue;

        pending = ble_file_pending;
        if (pending) {
            ble_file_pending = (uint8_t)(ble_file_pending & (uint8_t)~pending);
            if (pending & BLE_FILE_PENDING_SVC) ble_file_service_init();
            if (pending & BLE_FILE_PENDING_MTU) sibles_exchange_mtu(ble_file_env.conn_idx);
        }
        while (ble_file_cmd_tail != ble_file_cmd_head)
        {
            rt_snprintf(line, sizeof line, "%s", ble_file_cmd_queue[ble_file_cmd_tail]);
            ble_file_cmd_tail = (uint8_t)((ble_file_cmd_tail + 1) % BLE_FILE_QUEUE);
            ble_file_dispatch(line);
        }
    }
}

static uint8_t *ble_file_get_cb(uint8_t conn_idx, uint8_t idx, uint16_t *len)
{
    (void)conn_idx;
    *len = 0;
    if (idx == IDX_DATA_CCCD) {
        *len = sizeof ble_file_ntf_cfg;
        return (uint8_t *)&ble_file_ntf_cfg;
    }
    return NULL;
}

/* 只做累积与入队，读盘和回包全部在工作线程里完成。 */
static uint8_t ble_file_set_cb(uint8_t conn_idx, sibles_set_cbk_t *param)
{
    uint16_t i;

    (void)conn_idx;
    if (!param || !param->value) return 0;
    if (param->idx == IDX_DATA_CCCD && param->len >= 1) {
        ble_file_ntf_cfg = param->len >= 2
                           ? (uint16_t)(param->value[0] | (param->value[1] << 8))
                           : param->value[0];
        ble_file_env.ntf_on = (uint8_t)(ble_file_ntf_cfg & 0x0001);
        if (ble_file_env.ntf_on) ble_file_ntf_warned = 0;
        rt_kprintf("[%s] notify %s\n", BLE_FILE_TAG, ble_file_env.ntf_on ? "on" : "off");
        return 0;
    }
    if (param->idx != IDX_CMD_VAL) return 0;

    for (i = 0; i < param->len; ++i)
    {
        char ch = (char)param->value[i];
        if (ch == '\n' || ch == '\r') {
            if (ble_file_cmd_used) {
                ble_file_cmd_line[ble_file_cmd_used] = 0;
                ble_file_enqueue(ble_file_cmd_line);
            }
            ble_file_cmd_used = 0;
        } else if (ble_file_cmd_used < BLE_FILE_LINE - 1) {
            ble_file_cmd_line[ble_file_cmd_used++] = ch;
        } else {
            ble_file_cmd_used = 0;          /* 超长行丢弃 */
        }
    }
    return 0;
}

static void ble_file_service_init(void)
{
    sibles_register_svc_128_t svc = {0};

    if (ble_file_env.srv_handle) return;
    rt_kprintf("[%s] registering service...\n", BLE_FILE_TAG);
    svc.att_db = ble_file_att_db;
    svc.num_entry = IDX_ATT_NB;
    svc.sec_lvl = PERM(SVC_AUTH, NO_AUTH) | PERM(SVC_UUID_LEN, UUID_128);
    svc.uuid = ble_file_uuid_svc;
    ble_file_env.srv_handle = sibles_register_svc_128(&svc);
    rt_kprintf("[%s] service handle=%p\n", BLE_FILE_TAG, (void *)ble_file_env.srv_handle);
    if (ble_file_env.srv_handle) {
        sibles_register_cbk(ble_file_env.srv_handle, ble_file_get_cb, ble_file_set_cb);
        rt_kprintf("[%s] svc ready (svc=c0de0001 cmd=c0de0002 data=c0de0003)\n",
                   BLE_FILE_TAG);
    }
}

static int ble_file_event_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    (void)len;
    (void)context;
    if (!ble_file_env.enabled) return 0;

    switch (event_id)
    {
    case BLE_POWER_ON_IND:
        /* 服务注册必须离开 BLE 回调线程，否则会卡住上电与 HID 初始化（SDK file_transfer 例程同做法）。 */
        ble_file_pending |= BLE_FILE_PENDING_SVC;
        if (ble_file_cmd_sem) rt_sem_release(ble_file_cmd_sem);
        break;
    case BLE_GAP_CONNECTED_IND: {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;
        if (!ind) break;
        ble_file_env.conn_idx = ind->conn_idx;
        ble_file_env.connected = 1;
        ble_file_env.ntf_on = 0;
        ble_file_ntf_warned = 0;
        ble_file_env.mtu = BLE_FILE_MTU_DEFAULT;
        ble_file_cmd_used = 0;
        ble_file_cmd_head = ble_file_cmd_tail = 0;
        rt_kprintf("[%s] connected conn=%u\n", BLE_FILE_TAG, ind->conn_idx);
        ble_file_pending |= BLE_FILE_PENDING_MTU;
        if (ble_file_cmd_sem) rt_sem_release(ble_file_cmd_sem);
        break;
    }
    case SIBLES_MTU_EXCHANGE_IND: {
        sibles_mtu_exchange_ind_t *ind = (sibles_mtu_exchange_ind_t *)data;
        if (!ind) break;
        ble_file_env.mtu = ind->mtu;
        rt_kprintf("[%s] mtu=%u chunk=%u\n", BLE_FILE_TAG, ind->mtu, ble_file_chunk_bytes());
        break;
    }
    case BLE_GAP_DISCONNECTED_IND:
        rt_kprintf("[%s] disconnected\n", BLE_FILE_TAG);
        ble_file_env.connected = 0;
        ble_file_env.ntf_on = 0;
        ble_file_cmd_used = 0;
        ble_file_cmd_head = ble_file_cmd_tail = 0;
        break;
    default:
        break;
    }
    return 0;
}
BLE_EVENT_REGISTER(ble_file_event_handler, NULL);

void ble_file_init(bool (*busy)(void))
{
    rt_thread_t thread;

    if (ble_file_env.enabled) {
        rt_kprintf("[%s] already initialized\n", BLE_FILE_TAG);
        return;
    }
    ble_file_env.busy = busy;
    /* 先打开事件处理，避免 ble_hid_init() 之后 BLE 上电事件早于本函数完成。 */
    ble_file_env.enabled = 1;
    if (!ble_file_cmd_sem) {
        ble_file_cmd_sem = rt_sem_create("blefile", 0, RT_IPC_FLAG_FIFO);
        if (!ble_file_cmd_sem) {
            ble_file_env.enabled = 0;
            rt_kprintf("[%s] sem create failed\n", BLE_FILE_TAG);
            return;
        }
    }
    thread = rt_thread_create("memo_ble", ble_file_worker_entry, NULL, 4096, 20, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK) {
        ble_file_env.enabled = 0;
        rt_kprintf("[%s] thread start failed\n", BLE_FILE_TAG);
        return;
    }
    rt_kprintf("[%s] init done, waiting BLE power on\n", BLE_FILE_TAG);
}

bool ble_file_connected(void) { return ble_file_env.connected != 0; }

bool ble_file_transfer_active(void) { return ble_file_env.transfer_active != 0; }

#else

void ble_file_init(bool (*busy)(void)) { (void)busy; }
bool ble_file_connected(void) { return false; }
bool ble_file_transfer_active(void) { return false; }

#endif /* BLUETOOTH */
