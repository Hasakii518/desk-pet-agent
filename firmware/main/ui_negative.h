/* ui_negative.h — ② 左滑负一屏：语音按钮 + 硬件状态卡 + 预留 slot */
#ifndef UI_NEGATIVE_H
#define UI_NEGATIVE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_negative_create(lv_obj_t *parent);

/* 点按语音按钮：demo 走 空闲→录音→识别→已发送 的视觉流程 */
void ui_negative_voice_tap(void);

/* 重置滚动到顶部（每次进入负一屏时调用）*/
void ui_negative_scroll_top(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_NEGATIVE_H */
