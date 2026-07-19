/* frame_parse.h — 下行帧解析（传输无关，串口 / BLE 共用）
 *
 * 帧格式见 shared/protocol.md：每行一条 JSON（\n 分隔）。本模块负责：
 *   - 字节流按行重组（任意分片边界，BLE GATT 分片 / USB FIFO 读取都适用）
 *   - 行 JSON 解析并分发（notify / session / heartbeat）
 *   - 链路活性跟踪：≥15s 无下行帧 → 本地切 disconnected
 *   - 上行指令扇出：同一条 Command 行同时写串口和 BLE（谁连着谁收到）
 *
 * 所有函数必须在 LVGL 任务上下文调用（内部直接刷 UI，无锁）。
 * BLE RX 在 NimBLE host 任务回调里先把字节塞进环形缓冲（bt_stack.c），
 * 由 ble_proto 的 lv_timer 在本模块 feed，保证单线程语义。
 */
#ifndef FRAME_PARSE_H
#define FRAME_PARSE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：重置链路时间戳 + 启动断连检测定时器（100ms）。
 * 幂等，重复调用安全。必须在 LVGL 锁内调用（会创建 lv_timer）。 */
void frame_parse_init(void);

/* 喂入一段下行字节流（任意边界），内部按 \n 重组并解析完整行。 */
void frame_parse_feed_bytes(const uint8_t *data, int len);

/* ---- 上行 ---- */

/* 上行发送槽：把一行 JSON（不含 \n）写到物理通道。 */
typedef void (*frame_uplink_sink_t)(const char *line);

/* 注册一个上行槽（串口 / BLE 各一个），返回 false 表示槽位已满。 */
bool frame_parse_register_uplink(frame_uplink_sink_t sink);

/* 把一行上行 JSON 扇出到所有已注册槽。 */
void frame_parse_send_line(const char *line);

/* 上行指令构造 + 扇出（等价于原 serial_protocol_send_cmd*）。 */
void frame_parse_send_cmd(const char *cmd_name);
void frame_parse_send_cmd_kv(const char *cmd_name, const char *key, const char *value);
void frame_parse_send_cmd_bool(const char *cmd_name, const char *key, bool value);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_PARSE_H */
