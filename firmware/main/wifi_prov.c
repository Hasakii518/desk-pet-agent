/* wifi_prov.c — WiFi STA 配网实现 */
#include "wifi_prov.h"
#include "bt_stack.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";

#define NVS_NS       "wifiprov"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define MAX_RETRY    3

typedef enum { ST_IDLE, ST_CONNECTING, ST_OK, ST_FAIL } prov_state_t;

static volatile prov_state_t s_state = ST_IDLE;
static char     s_ssid[33];
static char     s_pass[65];
static char     s_ip[16] = "--";
static int      s_rssi;
static int      s_err;
static int      s_retry;
static bool     s_got_creds;      /* 本次启动收到过完整凭据 */

/* ---- 状态切换 + BLE 上报请求 ---- */
static void set_state(prov_state_t st)
{
    if (s_state == st) return;
    s_state = st;
    ESP_LOGI(TAG, "state -> %s", wifi_prov_state_str());
    bt_stack_prov_status_request();
}

/* ---- NVS ---- */
static void nvs_save_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, s_ssid);
    nvs_set_str(h, NVS_KEY_PASS, s_pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "credentials saved to NVS");
}

static bool nvs_load_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l1 = sizeof(s_ssid), l2 = sizeof(s_pass);
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, s_ssid, &l1);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, s_pass, &l2);
    nvs_close(h);
    return e1 == ESP_OK && s_ssid[0] != '\0' && e2 == ESP_OK;
}

/* ---- 连接 ---- */
static void start_connect(void)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    /* 开放网络：password 为空时 threshold 保持 OPEN */
    if (s_pass[0] != '\0') {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    }
    s_retry = 0;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
    set_state(ST_CONNECTING);
}

/* ---- 事件 ---- */
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_STA_START) {
        if (s_got_creds) esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        s_err = d ? d->reason : 0;
        strlcpy(s_ip, "--", sizeof(s_ip));
        if (s_state == ST_OK) {
            /* 已连接后掉线：报 fail，等下次 commit 或重启重连 */
            ESP_LOGW(TAG, "lost connection (reason=%d)", s_err);
            set_state(ST_FAIL);
        } else if (s_state == ST_CONNECTING) {
            if (s_retry < MAX_RETRY) {
                s_retry++;
                ESP_LOGW(TAG, "disconnected (reason=%d), retry %d/%d",
                         s_err, s_retry, MAX_RETRY);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "connect failed (reason=%d)", s_err);
                set_state(ST_FAIL);
            }
        }
        bt_stack_prov_status_request();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
        ESP_LOGI(TAG, "got ip %s rssi=%d", s_ip, s_rssi);
        nvs_save_creds();
        set_state(ST_OK);
    }
}

/* ============================ 对外接口 ============================ */

void wifi_prov_init(void)
{
    /* NVS：BLE/WiFi 凭据存储；首次或分区变更时 erase 重来 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s — wifi disabled", esp_err_to_name(err));
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed");
        return;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* NVS 有凭据 → 自动重连；没有则等 BLE 配网 */
    if (nvs_load_creds()) {
        s_got_creds = true;
        esp_wifi_start();   /* STA_START 事件里 esp_wifi_connect */
        ESP_LOGI(TAG, "auto-connect to saved SSID %s", s_ssid);
    } else {
        esp_wifi_start();
        ESP_LOGI(TAG, "no saved credentials, waiting for BLE provisioning");
    }
}

void wifi_prov_set_ssid(const char *ssid)
{
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
}

void wifi_prov_set_password(const char *pass)
{
    strlcpy(s_pass, pass, sizeof(s_pass));
}

void wifi_prov_commit(void)
{
    if (s_ssid[0] == '\0') {
        ESP_LOGW(TAG, "commit with empty ssid, ignored");
        return;
    }
    s_got_creds = true;
    ESP_LOGI(TAG, "commit: connecting to %s", s_ssid);
    esp_wifi_disconnect();   /* 若正连着旧 AP 先断开 */
    start_connect();
}

bool wifi_prov_connected(void) { return s_state == ST_OK; }

const char *wifi_prov_state_str(void)
{
    switch (s_state) {
    case ST_CONNECTING: return "connecting";
    case ST_OK:         return "ok";
    case ST_FAIL:       return "fail";
    default:            return "idle";
    }
}

const char *wifi_prov_ssid(void)
{
    return s_ssid[0] ? s_ssid : "--";
}

int wifi_prov_rssi(void)
{
    if (s_state == ST_OK) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
    }
    return s_rssi;
}

void wifi_prov_ip_str(char *buf, size_t len)
{
    if (len > 0) strlcpy(buf, s_ip, len);
}

int wifi_prov_last_err(void) { return s_err; }

void wifi_prov_status_json(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    switch (s_state) {
    case ST_OK:
        snprintf(buf, len, "{\"s\":\"ok\",\"ip\":\"%s\",\"rssi\":%d}",
                 s_ip, wifi_prov_rssi());
        break;
    case ST_FAIL:
        snprintf(buf, len, "{\"s\":\"fail\",\"err\":%d}", s_err);
        break;
    case ST_CONNECTING:
        snprintf(buf, len, "{\"s\":\"connecting\"}");
        break;
    default:
        snprintf(buf, len, "{\"s\":\"idle\"}");
        break;
    }
}
