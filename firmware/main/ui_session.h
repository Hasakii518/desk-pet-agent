/* ui_session.h — ③ 会话页（单会话一页）
 *
 * Route A 重构：会话不再是「一个视图内切换」，而是每个会话渲染成一张独立的
 * 466×466 页，交给 ui_nav 的横向 snap pager 平铺管理。本模块只负责「按
 * stored_session_t 数据建一页会话」，导航（左右翻会话 / 到边界回主页）全部
 * 由 pager 用原生滚动完成。
 */
#ifndef UI_SESSION_H
#define UI_SESSION_H

#include "lvgl.h"
#include "session_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 建一张会话页（使用 session_store 中的 stored_session_t 数据）。
 * 返回 466×466 根对象。 */
lv_obj_t *ui_session_create_page(lv_obj_t *parent,
                                 const stored_session_t *session);

/* 将指定会话页的滚动重置到顶部 */
void ui_session_scroll_top(lv_obj_t *page);

#ifdef __cplusplus
}
#endif

#endif /* UI_SESSION_H */
