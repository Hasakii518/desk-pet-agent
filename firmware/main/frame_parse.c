/* frame_parse.c — 下行帧解析实现（从 serial_protocol.c 抽出，传输无关） */
#include "frame_parse.h"
#include "session_store.h"
#include "pet_states.h"
#include "ui_pet.h"
#include "ui_nav.h"

#include "lvgl.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "frame";

#define LINE_BUF_SIZE          1536   /* protocol max session frame ~1.5KB */
#define DISCONNECT_TIMEOUT_MS  15000
#define MAX_UPLINK_SINKS       2      /* 串口 + BLE */

static char    s_line_buf[LINE_BUF_SIZE];
static int     s_line_len;
static int64_t s_last_frame_ms;
static bool    s_disconnected;
static bool    s_inited;

static frame_uplink_sink_t s_sinks[MAX_UPLINK_SINKS];
static int                 s_sink_count;

/* ============================ 行解析 + 帧分发 ============================ */

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

    cJSON *ts_json = cJSON_GetObjectItem(root, "ts");
    int64_t ts = (ts_json && cJSON_IsNumber(ts_json))
               ? (int64_t)ts_json->valuedouble : 0;

    if (strcmp(t, "heartbeat") == 0) {
        /* 链路活性用本地时钟：ts 是 bridge 墙钟，与 esp_timer 不同域 */
        s_last_frame_ms = esp_timer_get_time() / 1000;
    }
    else if (strcmp(t, "notify") == 0) {
        s_last_frame_ms = esp_timer_get_time() / 1000;
        app_reset_idle();

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

        if (title || text) {
            session_store_set_notification(title ? title : "",
                                           text  ? text  : "", src);
        } else {
            session_store_clear_notification();
        }

        if (sid) {
            session_store_update_state(sid, st, ts);
        }

        ESP_LOGI(TAG, "notify state=%s → pet=%d title=%s text=%.40s",
                 state_str ? state_str : "?", (int)st,
                 title ? title : "-", text ? text : "-");
        pet_state_set(st, src);
        ui_pet_refresh();
    }
    else if (strcmp(t, "session") == 0) {
        s_last_frame_ms = esp_timer_get_time() / 1000;
        app_reset_idle();

        cJSON *sid_json        = cJSON_GetObjectItem(root, "sid");
        cJSON *name_json       = cJSON_GetObjectItem(root, "name");
        cJSON *src_json        = cJSON_GetObjectItem(root, "src");
        cJSON *state_json      = cJSON_GetObjectItem(root, "state");
        cJSON *last_reply_json = cJSON_GetObjectItem(root, "lastReply");
        cJSON *next_step_json  = cJSON_GetObjectItem(root, "nextStep");
        cJSON *history_json    = cJSON_GetObjectItem(root, "history");

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
        ui_nav_rebuild_sessions();
    }
    /* 未知 t 值静默忽略 */

    cJSON_Delete(root);
}

void frame_parse_feed_bytes(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        if (data[i] == '\n') {
            if (s_line_len > 0 && s_line_len < (int)sizeof(s_line_buf)) {
                s_line_buf[s_line_len] = '\0';
                process_line(s_line_buf);
            }
            s_line_len = 0;
        } else if (data[i] == '\r') {
            /* 忽略 */
        } else if (s_line_len < (int)sizeof(s_line_buf) - 1) {
            s_line_buf[s_line_len++] = (char)data[i];
        } else {
            ESP_LOGW(TAG, "Line buffer overflow, discarding");
            s_line_len = 0;
        }
    }
}

/* ============================ 断连检测 ============================ */

static void link_check_cb(lv_timer_t *t)
{
    (void)t;
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
            app_reset_idle();
            ESP_LOGI(TAG, "Host reconnected");
        }
    }
}

void frame_parse_init(void)
{
    if (s_inited) return;
    s_inited = true;
    s_last_frame_ms = esp_timer_get_time() / 1000;
    s_disconnected = false;
    s_line_len = 0;
    lv_timer_create(link_check_cb, 100, NULL);
    ESP_LOGI(TAG, "Frame parser initialized (disconnect %ds)",
             DISCONNECT_TIMEOUT_MS / 1000);
}

/* ============================ 上行扇出 ============================ */

bool frame_parse_register_uplink(frame_uplink_sink_t sink)
{
    if (s_sink_count >= MAX_UPLINK_SINKS) return false;
    s_sinks[s_sink_count++] = sink;
    return true;
}

void frame_parse_send_line(const char *line)
{
    for (int i = 0; i < s_sink_count; i++) {
        if (s_sinks[i]) s_sinks[i](line);
    }
}

/* 用 cJSON 序列化后发送并释放。cJSON 自动转义字符串，避免 value 含
 * " / \ / 换行时产出非法 JSON 被 bridge 丢弃。失败静默（上行 best-effort）。*/
static void send_cmd_obj(cJSON *root)
{
    if (!root) return;
    char *line = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!line) return;
    frame_parse_send_line(line);
    cJSON_free(line);
}

void frame_parse_send_cmd(const char *cmd_name)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    send_cmd_obj(root);
}

void frame_parse_send_cmd_kv(const char *cmd_name,
                             const char *key, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    cJSON_AddStringToObject(root, key, value);
    send_cmd_obj(root);
}

void frame_parse_send_cmd_bool(const char *cmd_name,
                               const char *key, bool value)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd_name);
    cJSON_AddBoolToObject(root, key, value);
    send_cmd_obj(root);
}
