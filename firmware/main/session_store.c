/* session_store.c — 动态会话存储实现 */
#include "session_store.h"
#include "mock_data.h"
#include <string.h>
#include <stdio.h>

/* ---- 会话数组（按 ts 降序排列）---- */
static stored_session_t s_sessions[SESSION_STORE_MAX_SESSIONS];
static int               s_count;

/* ---- 通知状态 ---- */
static char           s_notif_title[SESSION_STORE_NAME_LEN];
static char           s_notif_text[SESSION_STORE_NOTIF_LEN];
static agent_source_t s_notif_source;
static bool           s_has_notif;

/* ---- 会话存储 ---- */

int session_store_count(void)
{
    return s_count;
}

const stored_session_t *session_store_get(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return &s_sessions[index];
}

void session_store_upsert(const char *sid, const char *name,
                          agent_source_t src, pet_state_t state,
                          const char *last_reply, const char *next_step,
                          const storable_msg_t *history, int history_len,
                          int64_t ts)
{
    if (!sid) return;

    /* 1. 查找已有位置 */
    int pos = -1;
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_sessions[i].sid, sid, SESSION_STORE_SID_LEN - 1) == 0) {
            pos = i;
            break;
        }
    }

    /* 2. 如果不存在：确定插入位置 */
    if (pos < 0) {
        if (s_count < SESSION_STORE_MAX_SESSIONS) {
            pos = s_count;
            s_count++;
        } else {
            /* 数组满：覆盖最旧的（ts 最小 = 数组末尾）*/
            pos = s_count - 1;
        }
    }

    /* 3. 写入字段 */
    stored_session_t *s = &s_sessions[pos];
    strncpy(s->sid, sid, SESSION_STORE_SID_LEN - 1);
    s->sid[SESSION_STORE_SID_LEN - 1] = '\0';
    if (name && name[0]) {
        strncpy(s->name, name, SESSION_STORE_NAME_LEN - 1);
        s->name[SESSION_STORE_NAME_LEN - 1] = '\0';
    }
    /* name 空 / 空串时不覆盖已有的名 */
    s->source = src;
    s->state  = state;
    s->ts     = ts;

    if (last_reply && last_reply[0]) {
        strncpy(s->last_reply, last_reply, SESSION_STORE_REPLY_LEN - 1);
        s->last_reply[SESSION_STORE_REPLY_LEN - 1] = '\0';
    }
    /* lastReply 空时不覆盖已有的 */

    if (next_step && next_step[0]) {
        strncpy(s->next_step, next_step, SESSION_STORE_STEP_LEN - 1);
        s->next_step[SESSION_STORE_STEP_LEN - 1] = '\0';
    }
    /* nextStep 空时不覆盖已有的 */

    if (history && history_len > 0) {
        s->history_len = (history_len < SESSION_STORE_MAX_HISTORY)
                       ? history_len : SESSION_STORE_MAX_HISTORY;
        for (int i = 0; i < s->history_len; i++) {
            s->history[i].from_user = history[i].from_user;
            strncpy(s->history[i].text, history[i].text,
                    SESSION_STORE_MSG_TEXT_LEN - 1);
            s->history[i].text[SESSION_STORE_MSG_TEXT_LEN - 1] = '\0';
        }
    } else {
        s->history_len = 0;
    }

    /* 4. 按 ts 降序冒泡到正确位置 */
    for (int i = pos; i > 0 && s_sessions[i].ts > s_sessions[i-1].ts; i--) {
        stored_session_t tmp = s_sessions[i];
        s_sessions[i] = s_sessions[i-1];
        s_sessions[i-1] = tmp;
    }
}

bool session_store_update_state(const char *sid, pet_state_t state, int64_t ts)
{
    if (!sid) return false;

    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_sessions[i].sid, sid, SESSION_STORE_SID_LEN - 1) == 0) {
            s_sessions[i].state = state;
            s_sessions[i].ts    = ts;

            /* 重排 */
            for (int j = i; j > 0 && s_sessions[j].ts > s_sessions[j-1].ts; j--) {
                stored_session_t tmp = s_sessions[j];
                s_sessions[j] = s_sessions[j-1];
                s_sessions[j-1] = tmp;
            }
            return true;
        }
    }
    return false;
}

const stored_session_t *session_store_find_by_sid(const char *sid)
{
    if (!sid) return NULL;
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_sessions[i].sid, sid, SESSION_STORE_SID_LEN - 1) == 0)
            return &s_sessions[i];
    }
    return NULL;
}

/* ---- 通知弹窗状态 ---- */

void session_store_set_notification(const char *title, const char *text,
                                    agent_source_t src)
{
    s_notif_source = src;
    s_has_notif = true;

    if (title && title[0]) {
        strncpy(s_notif_title, title, SESSION_STORE_NAME_LEN - 1);
        s_notif_title[SESSION_STORE_NAME_LEN - 1] = '\0';
    } else {
        s_notif_title[0] = '\0';
    }

    if (text && text[0]) {
        strncpy(s_notif_text, text, SESSION_STORE_NOTIF_LEN - 1);
        s_notif_text[SESSION_STORE_NOTIF_LEN - 1] = '\0';
    } else {
        s_notif_text[0] = '\0';
    }
}

bool session_store_has_notification(void)
{
    return s_has_notif;
}

const char *session_store_notif_title(void)
{
    return s_notif_title[0] ? s_notif_title : NULL;
}

const char *session_store_notif_text(void)
{
    return s_notif_text[0] ? s_notif_text : NULL;
}

agent_source_t session_store_notif_source(void)
{
    return s_notif_source;
}

void session_store_clear_notification(void)
{
    s_has_notif = false;
}

void session_store_seed_mock(void)
{
    const mock_session_t *ms = mock_sessions();
    int n = mock_session_count();
    int64_t base_ts = 1718000000000;  /* 固定基准时间戳，按索引递减 */

    for (int i = 0; i < n && i < SESSION_STORE_MAX_SESSIONS; i++) {
        /* 生成假 sid：mock-0, mock-1, … */
        char sid[16];
        snprintf(sid, sizeof(sid), "mock-%d", i);

        /* 转换 mock_msg_t → storable_msg_t */
        storable_msg_t hist[SESSION_STORE_MAX_HISTORY];
        int hlen = ms[i].history_len;
        if (hlen > SESSION_STORE_MAX_HISTORY)
            hlen = SESSION_STORE_MAX_HISTORY;
        for (int j = 0; j < hlen; j++) {
            hist[j].from_user = ms[i].history[j].from_user;
            strncpy(hist[j].text, ms[i].history[j].text,
                    SESSION_STORE_MSG_TEXT_LEN - 1);
            hist[j].text[SESSION_STORE_MSG_TEXT_LEN - 1] = '\0';
        }

        session_store_upsert(sid, ms[i].name, ms[i].source,
                             ms[i].status,
                             ms[i].last_reply, ms[i].next_step,
                             hist, hlen,
                             base_ts - i * 60000);  /* 每条间隔 1 分钟 */
    }
}
