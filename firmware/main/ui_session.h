/* ui_session.h — ③ 会话页（单会话一页）
 *
 * Route A 重构：会话不再是「一个视图内切换」，而是每个会话渲染成一张独立的
 * 466×466 页，交给 ui_nav 的横向 snap pager 平铺管理。本模块只负责「按索引
 * 建一页会话」，导航（左右翻会话 / 到边界回主页）全部由 pager 用原生滚动完成。
 */
#ifndef UI_SESSION_H
#define UI_SESSION_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 建一张会话页（第 index 个会话，0 = 最近）。父为 pager 的某个 page 容器。
 * 返回 466×466 根对象。 */
lv_obj_t *ui_session_create_page(lv_obj_t *parent, int index);

/* 将指定会话页的滚动重置到顶部 */
void ui_session_scroll_top(lv_obj_t *page);

#ifdef __cplusplus
}
#endif

#endif /* UI_SESSION_H */
