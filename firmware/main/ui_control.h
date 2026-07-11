/* ui_control.h — ④ 控制中心：下拉覆盖层，3x2 tile 网格 */
#ifndef UI_CONTROL_H
#define UI_CONTROL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_control_create(lv_obj_t *parent);

/* 当前是否有二级菜单浮窗打开。手势分发器应据此跳过导航，避免拖滑块时被误判
 * 为全局滑动手势而返回主页。 */
bool ui_control_has_submenu(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CONTROL_H */
