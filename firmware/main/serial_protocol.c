/* serial_protocol.c — USB-CDC 串口协议实现 */
#include "serial_protocol.h"
#include "session_store.h"
#include "pet_states.h"
#include "ui_pet.h"
#include "ui_nav.h"

#include "driver/usb_serial_jtag.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "serial";

#define LINE_BUF_SIZE          1536    /* protocol max session frame ~1.5KB */
#define POLL_INTERVAL_MS       10    /* 10ms→100Hz，避免 USB FIFO(64B) 溢出 */
#define DISCONNECT_TIMEOUT_MS  15000

static char       s_line_buf[LINE_BUF_SIZE];
static int        s_line_len;
static int64_t    s_last_frame_ms;    /* 最近一次收到 frame 的毫秒时间 */
static bool       s_disconnected;     /* 当前是否处于断连态 */
static lv_timer_t *s_poll_timer;

/* ---- 前置声明 ---- */
static void process_line(const char *line);

/* ============================ 行缓冲 + 轮询 ============================ */

static void serial_poll_cb(lv_timer_t *t)
{
    (void)t;

    /* 从 USB-CDC 硬件 FIFO 读取可用字节（非阻塞）*/
    uint8_t buf[512];
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
    static int zero_count;
    if (n > 0) {
        if (zero_count > 0) {
            ESP_LOGI(TAG, "serial resumed (%d bytes after %d polls of 0)", n, zero_count);
            zero_count = 0;
        }
    } else {
        zero_count++;
        if (zero_count == 1 || zero_count % 200 == 0)  /* 首次或每 10s 报一次 */
            ESP_LOGW(TAG, "serial read 0 bytes (x%d)", zero_count);
    }
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            /* 完整一行：null 终止后处理 */
            if (s_line_len > 0 && s_line_len < (int)sizeof(s_line_buf)) {
                s_line_buf[s_line_len] = '\0';
                process_line(s_line_buf);
            }
            s_line_len = 0;
        } else if (s_line_len < (int)sizeof(s_line_buf) - 1) {
            s_line_buf[s_line_len++] = (char)buf[i];
        } else {
            /* 行过长，丢弃到下一个 \n（防御：bridge 不应发超过 1.5KB 的行）*/
            ESP_LOGW(TAG, "Line buffer overflow, discarding");
            s_line_len = 0;
        }
    }

    /* ---- 断开检测 ---- */
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - s_last_frame_ms > DISCONNECT_TIMEOUT_MS) {
        if (!s_disconnected) {
            s_disconnected = true;
            session_store_clear_notification();
            pet_state_set_disconnected(true);
            ui_pet_refresh();
            ESP_LOGW(TAG, "Host disconnected (no frame for %lld ms)",
                     now_ms - s_last_frame_ms);
        }
    } else {
        if (s_disconnected) {
            s_disconnected = false;
            pet_state_set_disconnected(false);
            ui_pet_refresh();
            ESP_LOGI(TAG, "Host reconnected");
        }
    }
}

/* ============================ 帧分发 ============================ */

static void process_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGW(TAG, "JSON parse fail (len=%d err_at=%d): %s",
                 (int)strlen(line),
                 err ? (int)(err - line) : -1,
                 line);
        return;
    }

    cJSON *t_json = cJSON_GetObjectItem(root, "t");
    if (!t_json || !cJSON_IsString(t_json)) {
        cJSON_Delete(root);
        return;
    }
    const char *t = t_json->valuestring;

    /* 提取 ts（所有帧类型通用）*/
    cJSON *ts_json = cJSON_GetObjectItem(root, "ts");
    int64_t ts = (ts_json && cJSON_IsNumber(ts_json))
               ? (int64_t)ts_json->valuedouble : 0;

    if (strcmp(t, "heartbeat") == 0) {
        /* heartbeat 仅刷新链路时间，不唤醒屏幕、不改变 pet 状态 */
        /* 链路活性用本地时钟：ts 是 bridge 墙钟（仅用于展示），
         * 与 esp_timer_get_time() 不同域，不能混进断连判定。*/
        s_last_frame_ms = esp_timer_get_time() / 1000;
    }
    else if (strcmp(t, "notify") == 0) {
        /* 链路活性用本地时钟：ts 是 bridge 墙钟（仅用于展示），
         * 与 esp_timer_get_time() 不同域，不能混进断连判定。*/
        s_last_frame_ms = esp_timer_get_time() / 1000;
        app_reset_idle();

        /* 提取字段 */
        cJSON *sid_json   = cJSON_GetObjectItem(root, "sid");
        cJSON *state_json = cJSON_GetObjectItem(root, "state");
        cJSON *src_json   = cJSON_GetObjectItem(root, "src");
        cJSON *title_json = cJSON_GetObjectItem(root, "title");
        cJSON *text_json  = cJSON_GetObjectItem(root, "text");

        const char *sid       = (sid_json   && cJSON_IsString(sid_json))
                              ? sid_json->valuestring : NULL;
        const char *state_str = (state_json && cJSON_IsString(state_json))
                              ? state_json->valuestring : NULL;
        const char *src_str   = (src_json   && cJSON_IsString(src_json))
                              ? src_json->valuestring : NULL;
        const char *title     = (title_json && cJSON_IsString(title_json))
                              ? title_json->valuestring : NULL;
        const char *text      = (text_json  && cJSON_IsString(text_json))
                              ? text_json->valuestring : NULL;

        pet_state_t    st  = pet_state_from_string(state_str);
        agent_source_t src = pet_source_from_string(src_str);

        /* 通知弹窗：title + text 齐备时显示；缺任一则清空。
         * bridge 的 encoder 保证每个 notify 帧同时带 title 和 text。 */
        if (title || text) {
            session_store_set_notification(title ? title : "",
                                           text  ? text  : "", src);
        } else {
            session_store_clear_notification();
        }

        /* 更新对应会话的 state */
        if (sid) {
            session_store_update_state(sid, st, ts);
        }

        /* 驱宠表情 */
        ESP_LOGI(TAG, "notify state=%s → pet=%d title=%s text=%.40s",
                 state_str ? state_str : "?", (int)st,
                 title ? title : "-", text ? text : "-");
        pet_state_set(st, src);
        ui_pet_refresh();
    }
    else if (strcmp(t, "session") == 0) {
        /* 链路活性用本地时钟：ts 是 bridge 墙钟（仅用于展示），
         * 与 esp_timer_get_time() 不同域，不能混进断连判定。*/
        s_last_frame_ms = esp_timer_get_time() / 1000;
        app_reset_idle();

        /* 提取字段 */
        cJSON *sid_json       = cJSON_GetObjectItem(root, "sid");
        cJSON *name_json      = cJSON_GetObjectItem(root, "name");
        cJSON *src_json       = cJSON_GetObjectItem(root, "src");
        cJSON *state_json     = cJSON_GetObjectItem(root, "state");
        cJSON *last_reply_json = cJSON_GetObjectItem(root, "lastReply");
        cJSON *next_step_json = cJSON_GetObjectItem(root, "nextStep");
        cJSON *history_json   = cJSON_GetObjectItem(root, "history");

        const char *sid        = (sid_json && cJSON_IsString(sid_json))
                               ? sid_json->valuestring : NULL;
        const char *name       = (name_json && cJSON_IsString(name_json))
                               ? name_json->valuestring : NULL;
        const char *src_str    = (src_json && cJSON_IsString(src_json))
                               ? src_json->valuestring : NULL;
        const char *state_str  = (state_json && cJSON_IsString(state_json))
                               ? state_json->valuestring : NULL;
        const char *last_reply = (last_reply_json && cJSON_IsString(last_reply_json))
                               ? last_reply_json->valuestring : NULL;
        const char *next_step  = (next_step_json && cJSON_IsString(next_step_json))
                               ? next_step_json->valuestring : NULL;

        pet_state_t    st  = pet_state_from_string(state_str);
        agent_source_t src = pet_source_from_string(src_str);

        /* 提取 history 数组 */
        storable_msg_t history_msgs[SESSION_STORE_MAX_HISTORY];
        int history_count = 0;
        if (history_json && cJSON_IsArray(history_json)) {
            cJSON *item;
            cJSON_ArrayForEach(item, history_json) {
                if (history_count >= SESSION_STORE_MAX_HISTORY) break;
                cJSON *u = cJSON_GetObjectItem(item, "u");
                cJSON *x = cJSON_GetObjectItem(item, "x");
                if (x && cJSON_IsString(x)) {
                    history_msgs[history_count].from_user = (u && cJSON_IsTrue(u));
                    strncpy(history_msgs[history_count].text,
                            x->valuestring, SESSION_STORE_MSG_TEXT_LEN - 1);
                    history_msgs[history_count]
                        .text[SESSION_STORE_MSG_TEXT_LEN - 1] = '\0';
                    history_count++;
                }
            }
        }

        session_store_upsert(sid, name, src, st,
                             last_reply, next_step,
                             history_msgs, history_count, ts);

        /* 会话页内容随 session 帧刷新：数量变化（新增/淘汰）或已存在会话
         * 内容更新（recap/nextStep/history 刷新）都要重建对应页，否则 LVGL
         * 标签停留在建页时的旧值。session 帧低频，整页重建可接受。*/
        ui_nav_rebuild_sessions();
    }
    /* 未知 t 值静默忽略 */

    cJSON_Delete(root);
}

/* ============================ 初始化 ============================ */

void serial_protocol_init(void)
{
    /* 默认时区：设备无 RTC，会话时间用 bridge 下发的墙钟 ms 经 localtime_r
     * 格式化；不设 TZ 则显示 UTC。这里设为 CST-8（本项目默认地域），如需
     * 其他时区改此处的 TZ 字符串即可。*/
    setenv("TZ", "CST-8", 1);
    tzset();

    /* 安装 USB Serial/JTAG 驱动，否则 usb_serial_jtag_read_bytes() 会访问
     * 未初始化的驱动上下文（NULL ring buffer）导致 LoadProhibited panic。
     * 仅用 TX/RX FIFO（非 VFS / 中断），默认配置即可。
     * ESP_ERR_INVALID_STATE 表示已被控制台安装——可复用，read_bytes 照常工作。*/
    usb_serial_jtag_driver_config_t usj_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&usj_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s — serial disabled",
                 esp_err_to_name(err));
        return;  /* 不启动轮询，否则 read_bytes 仍会 panic */
    }

    s_last_frame_ms = esp_timer_get_time() / 1000;
    s_disconnected = false;
    s_line_len = 0;

    s_poll_timer = lv_timer_create(serial_poll_cb, POLL_INTERVAL_MS, NULL);

    ESP_LOGI(TAG, "Serial protocol initialized (poll %dms, disconnect %ds)",
             POLL_INTERVAL_MS, DISCONNECT_TIMEOUT_MS / 1000);
}

/* ============================ 上行指令 ============================ */

static void write_line(const char *line)
{
    int len = strlen(line);
    usb_serial_jtag_write_bytes((const uint8_t *)line, len,
                                pdMS_TO_TICKS(100));
}

/* 用 cJSON 序列化后发送并释放。cJSON 自动转义字符串，避免 value 含
 * " / \ / 换行时产出非法 JSON 被 bridge 丢弃。失败静默（上行 best-effort）。*/
static void send_cmd_obj(cJSON *root)
{
    if (!root) return;
    char *line = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!line) return;
    write_line(line);
    cJSON_free(line);
}

void serial_protocol_send_cmd(const char *cmd_name)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    send_cmd_obj(root);
}

void serial_protocol_send_cmd_kv(const char *cmd_name,
                                 const char *key, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    cJSON_AddStringToObject(root, key, value);
    send_cmd_obj(root);
}

void serial_protocol_send_cmd_bool(const char *cmd_name,
                                   const char *key, bool value)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    cJSON_AddBoolToObject(root, key, value);
    send_cmd_obj(root);
}
