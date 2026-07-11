/* mock_data.h — demo 用的 mock 会话 / 硬件状态 / 通知数据
 *
 * 无任何通信，所有数据静态定义在 mock_data.c，模拟 bridge 下发的
 * SessionSummary / HardwareStatus / 通知。
 */
#ifndef MOCK_DATA_H
#define MOCK_DATA_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一条会话内的历史消息 */
typedef struct {
    bool        from_user;   /* true=用户，false=Agent */
    const char *text;
} mock_msg_t;

/* 单个会话摘要，对应 bridge SessionSummary */
typedef struct {
    const char       *name;        /* 会话名 */
    agent_source_t    source;      /* 来源 */
    pet_state_t       status;      /* 当前状态 */
    const char       *time;        /* 更新时间（已格式化）*/
    const char       *last_reply;  /* 最新回复片段 */
    const char       *next_step;   /* 下一步动作，NULL=等用户输入 */
    const mock_msg_t *history;     /* 历史消息数组 */
    int               history_len;
} mock_session_t;

/* 硬件状态，对应 bridge HardwareStatus（不含温度）*/
typedef struct {
    const char *device;
    int         battery_pct;
    bool        charging;
    const char *wifi;
    int         cpu_pct;
    const char *mem;
    int         laptop_battery_pct;
} mock_hardware_t;

/* 主页通知气泡 */
typedef struct {
    const char    *session_name;
    agent_source_t source;
    const char    *text;
} mock_notification_t;

/* 会话（按 updatedAt 倒序，最新在前）*/
const mock_session_t *mock_sessions(void);
int                   mock_session_count(void);

/* 硬件状态 */
const mock_hardware_t *mock_hardware(void);

/* 主页当前通知（demo 固定一条）*/
const mock_notification_t *mock_current_notification(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_DATA_H */
