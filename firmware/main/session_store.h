/* session_store.h — 动态会话存储 + 通知状态（替代 mock_data）
 *
 * session 帧按 sid 整条覆盖；notify 帧只刷实时 state + 当前弹窗。
 * 存储为定长数组（最多 SESSION_STORE_MAX_SESSIONS 条），按 timestamp 降序排列。
 * 所有操作在 LVGL 任务内执行，无需锁。
 */
#ifndef SESSION_STORE_H
#define SESSION_STORE_H

#include "ui_common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SESSION_STORE_MAX_SESSIONS  5
#define SESSION_STORE_MAX_HISTORY   6
#define SESSION_STORE_SID_LEN       32
#define SESSION_STORE_NAME_LEN      64
#define SESSION_STORE_REPLY_LEN     256
#define SESSION_STORE_STEP_LEN      256
#define SESSION_STORE_MSG_TEXT_LEN  128
#define SESSION_STORE_NOTIF_LEN     256

/* 一条会话历史消息 */
typedef struct {
    bool  from_user;
    char  text[SESSION_STORE_MSG_TEXT_LEN];
} storable_msg_t;

/* 单条会话快照（设备内存记录）*/
typedef struct {
    char            sid[SESSION_STORE_SID_LEN];
    char            name[SESSION_STORE_NAME_LEN];
    agent_source_t  source;
    pet_state_t     state;
    char            last_reply[SESSION_STORE_REPLY_LEN];
    char            next_step[SESSION_STORE_STEP_LEN];
    storable_msg_t  history[SESSION_STORE_MAX_HISTORY];
    int             history_len;
    int64_t         ts;          /* 毫秒时间戳（上次更新时间）*/
} stored_session_t;

/* ---- 会话存储 ---- */

/* 已存储会话数量（按 ts 降序排列，0=最新）*/
int session_store_count(void);

/* 按展示索引获取会话（0=最新）。index 越界返回 NULL。 */
const stored_session_t *session_store_get(int index);

/* 插入或覆盖一条会话（键=sid）。覆盖后按 ts 重排。 */
void session_store_upsert(const char *sid, const char *name,
                          agent_source_t src, pet_state_t state,
                          const char *last_reply, const char *next_step,
                          const storable_msg_t *history, int history_len,
                          int64_t ts);

/* 仅更新已有会话的 state 和 ts。sid 不存在时返回 false。 */
bool session_store_update_state(const char *sid, pet_state_t state, int64_t ts);

/* 按 sid 查找会话，返回 NULL 表示不存在。 */
const stored_session_t *session_store_find_by_sid(const char *sid);

/* ---- 通知弹窗状态 ---- */

/* 设置当前通知（notify 帧带 title/text 时调用）*/
void session_store_set_notification(const char *title, const char *text,
                                    agent_source_t src);

/* 是否有通知可显示 */
bool session_store_has_notification(void);

/* 当前通知标题（可能为 NULL）*/
const char *session_store_notif_title(void);

/* 当前通知正文（可能为 NULL）*/
const char *session_store_notif_text(void);

/* 当前通知来源 */
agent_source_t session_store_notif_source(void);

/* 清除通知（状态离开 PET_NOTIFICATION 时调用）*/
void session_store_clear_notification(void);

/* 用 mock_data 预填充会话存储（bridge 未连接时的初始展示）。
 * 后续 bridge session 帧到达后自动覆盖对应 sid。 */
void session_store_seed_mock(void);

#ifdef __cplusplus
}
#endif

#endif /* SESSION_STORE_H */
