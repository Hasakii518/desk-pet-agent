/* bt_stack.c — NimBLE host + GATT server（NUS 数据通道 + WiFi 配网） */
#include "bt_stack.h"
#include "frame_parse.h"
#include "wifi_prov.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "bt";

/* 前置声明：广播与 GAP 事件互相引用（断连后重启广播）*/
static int gap_event(struct ble_gap_event *event, void *arg);

/* ---- UUID（NimBLE BLE_UUID128_INIT 字节序为 little-endian，即倒序）---- */
/* 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
#define UUID128_NUS_SVC  BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x6E)
#define UUID128_NUS_RX   BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x6E)
#define UUID128_NUS_TX   BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x03,0x00,0x40,0x6E)
/* 4A1A00xx-B5A3-F393-E0A9-E50E24DCCA9E */
#define UUID128_PROV_SVC BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x00,0x00,0x1A,0x4A)
#define UUID128_PROV_SSID BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x1A,0x4A)
#define UUID128_PROV_PASS BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x1A,0x4A)
#define UUID128_PROV_COMMIT BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x03,0x00,0x1A,0x4A)
#define UUID128_PROV_STATUS BLE_UUID128_INIT(0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x04,0x00,0x1A,0x4A)

static const ble_uuid128_t uuid_nus_svc  = UUID128_NUS_SVC;
static const ble_uuid128_t uuid_nus_rx   = UUID128_NUS_RX;
static const ble_uuid128_t uuid_nus_tx   = UUID128_NUS_TX;
static const ble_uuid128_t uuid_prov_svc = UUID128_PROV_SVC;
static const ble_uuid128_t uuid_prov_ssid = UUID128_PROV_SSID;
static const ble_uuid128_t uuid_prov_pass = UUID128_PROV_PASS;
static const ble_uuid128_t uuid_prov_commit = UUID128_PROV_COMMIT;
static const ble_uuid128_t uuid_prov_status = UUID128_PROV_STATUS;

/* ---- 状态 ---- */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_nus_tx_handle;        /* TX notify 属性句柄 */
static uint16_t s_prov_status_handle;
static bool     s_tx_notify_on;
static bool     s_prov_notify_on;
static uint16_t s_mtu = 23;             /* 连接后协商，上行分片用 */
static char     s_dev_name[16];         /* "ClawdPet-XXXX" */
static volatile bool s_prov_status_pending;  /* wifi_prov 请求上报状态（跨任务）*/

/* ---- RX 环形缓冲：GATT 回调（host 任务）写入，ble_proto（LVGL 任务）读出 ---- */
#define RX_RING_SIZE 4096
static uint8_t      s_rx_ring[RX_RING_SIZE];
static volatile int s_rx_head;   /* 写指针（host 任务）*/
static volatile int s_rx_tail;   /* 读指针（LVGL 任务）*/

static void rx_ring_write(const uint8_t *data, int len)
{
    int head = s_rx_head;
    for (int i = 0; i < len; i++) {
        int next = (head + 1) % RX_RING_SIZE;
        if (next == s_rx_tail) {
            /* 满：丢弃余下字节。行尾 \n 可能丢失 → 该行作废，
             * 设备按协议丢弃解析失败的行，后续帧不受影响。*/
            ESP_LOGW(TAG, "rx ring full, dropped %d bytes", len - i);
            break;
        }
        s_rx_ring[head] = data[i];
        head = next;
    }
    s_rx_head = head;
}

/* ============================ GATT 访问回调 ============================ */

/* NUS RX：bridge 下行帧分片 → 环形缓冲 */
static int gatt_nus_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[244];
    int len = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), NULL);
    if (len > 0) rx_ring_write(buf, len);
    return 0;
}

/* 配网特征读写。ssid/pass/commit 是写；status 读时回当前 JSON。 */
static int gatt_prov_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)arg;
    const ble_uuid_t *u = ctxt->chr ? ctxt->chr->uuid : NULL;
    if (!u) return BLE_ATT_ERR_UNLIKELY;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[65];
        int cap = (ble_uuid_cmp(u, &uuid_prov_pass.u) == 0) ? 64 : 32;
        int len = ble_hs_mbuf_to_flat(ctxt->om, buf, cap, NULL);
        buf[len] = '\0';
        if (ble_uuid_cmp(u, &uuid_prov_ssid.u) == 0) {
            wifi_prov_set_ssid(buf);
        } else if (ble_uuid_cmp(u, &uuid_prov_pass.u) == 0) {
            wifi_prov_set_password(buf);
        } else if (ble_uuid_cmp(u, &uuid_prov_commit.u) == 0) {
            wifi_prov_commit();
        }
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(u, &uuid_prov_status.u) == 0) {
            char json[96];
            wifi_prov_status_json(json, sizeof(json));
            int rc = os_mbuf_append(ctxt->om, json, strlen(json));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- GATT 服务表 ---- */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_nus_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &uuid_nus_rx.u,
                .access_cb = gatt_nus_rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &uuid_nus_tx.u,
                .access_cb = NULL,              /* notify-only，无读写 */
                .val_handle = &s_nus_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_prov_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &uuid_prov_ssid.u,  .access_cb = gatt_prov_access,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &uuid_prov_pass.u,  .access_cb = gatt_prov_access,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &uuid_prov_commit.u,.access_cb = gatt_prov_access,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &uuid_prov_status.u,.access_cb = gatt_prov_access,
              .val_handle = &s_prov_status_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            { 0 }
        },
    },
    { 0 }
};

/* ============================ 广播 ============================ */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;

    /* adv：flags + 128-bit NUS UUID；名字放 scan response（31B 上限）*/
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t[]){ uuid_nus_svc };
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&f) != 0) {
        ESP_LOGE(TAG, "adv set fields failed");
        return;
    }
    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (uint8_t *)s_dev_name;
    rsp.name_len = strlen(s_dev_name);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "adv start failed: %d", rc);
    else ESP_LOGI(TAG, "advertising as %s", s_dev_name);
}

/* ============================ GAP 事件 ============================ */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "central connected (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed: %d", event->connect.status);
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "central disconnected (reason=0x%x)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_tx_notify_on = false;
        s_prov_notify_on = false;
        s_mtu = 23;
        start_advertising();
        break;
    case BLE_GAP_EVENT_MTU:
        s_mtu = event->mtu.value;
        ESP_LOGI(TAG, "mtu updated: %d", s_mtu);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_nus_tx_handle) {
            s_tx_notify_on = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "NUS TX notify %s", s_tx_notify_on ? "on" : "off");
        } else if (event->subscribe.attr_handle == s_prov_status_handle) {
            s_prov_notify_on = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "prov status notify %s", s_prov_notify_on ? "on" : "off");
            if (s_prov_notify_on) s_prov_status_pending = true;  /* 订阅即报一次 */
        }
        break;
    default:
        break;
    }
    return 0;
}

/* ============================ NimBLE 初始化 ============================ */

static void on_sync(void)
{
    /* 确保有 public 地址（ESP32-S3 efuse 烧录），失败退回 random static */
    int rc = ble_hs_id_infer_auto(0, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    snprintf(s_dev_name, sizeof(s_dev_name), "ClawdPet-%02X%02X", addr[1], addr[0]);
    ble_svc_gap_device_name_set(s_dev_name);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();        /* host 事件循环，直到 nimble_port_stop */
    nimble_port_freertos_deinit();
}

/* ============================ ble_proto：RX 排空 + 上行 ============================ */

/* 上行槽：一行 JSON → NUS TX notify，按 (mtu-3) 分片 + \n 结尾 */
static void ble_uplink(const char *line)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_notify_on) return;
    int chunk = s_mtu - 3;
    if (chunk < 20) chunk = 20;
    size_t len = strlen(line);
    const char *p = line;
    while (len > 0) {
        size_t n = len > (size_t)chunk ? (size_t)chunk : len;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(p, n);
        if (!om) return;
        if (ble_gatts_notify_custom(s_conn_handle, s_nus_tx_handle, om) != 0) return;
        p += n;
        len -= n;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat("\n", 1);
    if (om) ble_gatts_notify_custom(s_conn_handle, s_nus_tx_handle, om);
}

/* LVGL 定时器：排空 RX 环形缓冲喂给 frame_parse；补发待上报的配网状态 */
static void ble_proto_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t buf[256];
    int tail = s_rx_tail;
    int head = s_rx_head;
    int n = 0;
    while (tail != head && n < (int)sizeof(buf)) {
        buf[n++] = s_rx_ring[tail];
        tail = (tail + 1) % RX_RING_SIZE;
    }
    s_rx_tail = tail;
    if (n > 0) frame_parse_feed_bytes(buf, n);

    if (s_prov_status_pending && s_prov_notify_on &&
        s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        s_prov_status_pending = false;
        char json[96];
        wifi_prov_status_json(json, sizeof(json));
        struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
        if (om) ble_gatts_notify_custom(s_conn_handle, s_prov_status_handle, om);
    }
}

/* ============================ 对外接口 ============================ */

void bt_stack_init(void)
{
    strlcpy(s_dev_name, "ClawdPet-0000", sizeof(s_dev_name));

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d — BLE disabled", rc);
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt init failed: %d — BLE disabled", rc);
        return;
    }

    frame_parse_init();
    frame_parse_register_uplink(ble_uplink);
    lv_timer_create(ble_proto_cb, 10, NULL);

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE stack initialized");
}

bool bt_stack_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void bt_stack_device_name(char *buf, size_t len)
{
    if (len > 0) strlcpy(buf, s_dev_name, len);
}

void bt_stack_prov_status_request(void)
{
    s_prov_status_pending = true;
}
