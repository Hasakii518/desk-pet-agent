/* bsp.h — 板级支持：466x466 圆形 AMOLED (SH8601 QSPI) + FT5x06 触摸 + LVGL 移植
 *
 * 引脚参照 Waveshare ESP32-S3-Touch-AMOLED-1.43C。
 * bsp_init() 完成显示 + 触摸 + lvgl_port 初始化后，即可在默认屏上建 UI。
 */
#ifndef BSP_H
#define BSP_H

#include "lvgl.h"
#include "esp_lcd_touch.h"

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

/* 触摸句柄，给手势轮询直读触控 IC（绕过 LVGL indev 采样周期）。 */
esp_lcd_touch_handle_t bsp_touch_handle(void);

/* 读 CST816S 手势寄存器 0x01。返回值：0=无, 1=上, 2=下, 3=左, 4=右, 5=点击, -1=错误。
 * 极快滑时触控 IC 不报坐标点，只报手势码——从这里兜底取方向。 */
int bsp_touch_read_gesture(void);

/* 开/关显示（BOOT 键息屏用）。on=false 关屏省电，on=true 恢复。 */
void bsp_display_set_on(bool on);

/* 设置面板亮度（SH8601 0x51 命令，0~255）。注意别传 0，否则 AMOLED 熄灭。 */
void bsp_set_brightness(uint8_t level);

/* FreeType emoji fallback font（Noto Emoji Regular, 24px bitmap）。 */
lv_font_t *bsp_emoji_font(void);

/* CJK 字体：从 Flash 中的 cjk.ttf 用 FreeType 渲染指定 size。
 * 返回 NULL 表示该尺寸未加载（cjk.ttf 不存在或 FreeType 失败）。
 * 上层调用者自行 fallback 到内置蒙纳字体。 */
lv_font_t *bsp_cjk_font(int size);

/* 通知正文用字体：优先 CJK 28px，不存在时回退 montserrat 28。 */
lv_font_t *bsp_body_font(void);

/* 设置 LVGL 显示刷新周期（ms）。16≈60Hz，33≈30Hz。 */
void bsp_set_refr_period(uint32_t period_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
