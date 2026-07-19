/* bt_stack.h — BLE 协议栈（NimBLE host + GATT server）
 *
 * 设备作 GATT server，广播名 ClawdPet-XXXX（XXXX=MAC 后 4 位十六进制）。
 * GATT 布局（与 bridge internal/transport/ble.go 的 UUID 一一对应）：
 *
 *   Nordic UART Service (NUS)  6E400001-…
 *     RX 6E400002  write/write-no-rsp   bridge→设备：Agent 数据帧分片
 *     TX 6E400003  notify               设备→bridge：上行 Command 行
 *
 *   WiFi 配网 Service          4A1A0000-…
 *     SSID     4A1A0001  write          UTF-8 SSID（≤32B）
 *     Password 4A1A0002  write          密码（≤64B）
 *     Commit   4A1A0003  write          任意值触发连接
 *     Status   4A1A0004  read/notify    JSON {"s":…,"ip":…,"rssi":…,"err":…}
 *
 * 线程模型：GATT 回调跑在 NimBLE host 任务，只把 RX 字节塞进环形缓冲；
 * 行重组 + 帧解析由 ble_proto lv_timer 在 LVGL 任务里完成（frame_parse）。
 * 上行 notify 经 ble_proto 定时器发送，不在 GATT/WiFi 事件上下文直发。
 */
#ifndef BT_STACK_H
#define BT_STACK_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 NimBLE host + GATT + 广播。必须在 LVGL 锁内调用（会创建 lv_timer）。
 * 协议栈初始化失败只记日志不致命——串口通道仍可工作。 */
void bt_stack_init(void);

/* 当前是否有中心（bridge）连着。 */
bool bt_stack_connected(void);

/* 拷贝设备广播名到 buf（"ClawdPet-XXXX"）。 */
void bt_stack_device_name(char *buf, size_t len);

/* 请求发送最近一次 WiFi 配网状态（由 ble_proto 定时器实际 notify）。 */
void bt_stack_prov_status_request(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_STACK_H */
