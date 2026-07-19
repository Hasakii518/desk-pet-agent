/* ui_connect.h — 连接详情页（控制中心 WiFi/蓝牙 tile 点入）
 *
 * 全屏浮窗展示 BLE / WiFi 实时状态：BLE 广播名、连接状态；WiFi 状态、
 * SSID、IP、信号强度。1s 定时刷新，点遮罩空白关闭。
 */
#ifndef UI_CONNECT_H
#define UI_CONNECT_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 在 parent（控制中心根）上打开连接详情浮窗。重复调用先关再开。 */
void ui_connect_open(lv_obj_t *parent);

/* 当前详情页是否打开（手势分发器据此屏蔽全局手势）。 */
bool ui_connect_visible(void);

/* 关闭详情页（控制中心收起时调用）。 */
void ui_connect_close(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CONNECT_H */
