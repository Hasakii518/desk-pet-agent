/* serial_protocol.h — USB-CDC 串口协议：下行帧解析 + 上行指令发送
 *
 * 在 LVGL 定时器中以 50ms 间隔轮询 USB Serial/JTAG 硬件 FIFO，
 * 逐行解析 JSON 帧（notify / session / heartbeat），驱动 pet 状态机、
 * 会话存储、通知弹窗。所有操作在 LVGL 任务内执行，无需锁。
 *
 * 断开检测：≥15s 无下行帧（含 heartbeat）→ 本地切 disconnected。
 */
#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include "ui_common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动串口轮询定时器。必须在 LVGL 锁内调用（会创建 lv_timer）。 */
void serial_protocol_init(void);

/* ---- 上行指令 ---- */

/* 发送纯命令帧，例如 {"cmd":"voice_start"} */
void serial_protocol_send_cmd(const char *cmd_name);

/* 发送带字符串参数的命令帧，例如 {"cmd":"voice_text","text":"hello"} */
void serial_protocol_send_cmd_kv(const char *cmd_name,
                                 const char *key, const char *value);

/* 发送带布尔参数的命令帧，例如 {"cmd":"mute_toggle","value":true} */
void serial_protocol_send_cmd_bool(const char *cmd_name,
                                   const char *key, bool value);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_PROTOCOL_H */
