/* ui_pet.h — ① 主页桌宠视图
 *
 * 圆形中心显示 Clawd 桌宠（真实素材），外缘 Source Halo，圆心上方通知气泡。
 * 每个视图暴露：create(parent) 建根对象、refresh() 按当前状态刷新、handle_tap()。
 */
#ifndef UI_PET_H
#define UI_PET_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建主页，返回根对象（全屏，父为某个 view 容器）*/
lv_obj_t *ui_pet_create(lv_obj_t *parent);

/* 按当前 pet_state 刷新桌宠图片 + Halo + 通知气泡 */
void ui_pet_refresh(void);

/* 点按桌宠：抖动反馈 + 切下一个状态（demo）*/
void ui_pet_poke(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_PET_H */
