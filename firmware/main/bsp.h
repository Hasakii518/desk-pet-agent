/* bsp.h — 板级支持：466x466 圆形 AMOLED (SH8601 QSPI) + FT5x06 触摸 + LVGL 移植
 *
 * 引脚参照 Waveshare ESP32-S3-Touch-AMOLED-1.43C。
 * bsp_init() 完成显示 + 触摸 + lvgl_port 初始化后，即可在默认屏上建 UI。
 */
#ifndef BSP_H
#define BSP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化显示、触摸与 LVGL。成功后 lv_display_get_default() 可用。 */
void bsp_init(void);

/* 加/解 LVGL 互斥锁（lvgl_port 在独立任务里跑）。UI 操作需包裹在锁内。 */
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);

/* 触摸输入设备句柄（用于全局手势识别）。bsp_init 后可用。 */
lv_indev_t *bsp_touch_indev(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
