/* ui_session.h — ③ 会话页：单会话全屏 + 右滑/左滑切会话 + 上下滑翻内容 */
#ifndef UI_SESSION_H
#define UI_SESSION_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_session_create(lv_obj_t *parent);

/* 切到下一个 / 上一个会话（循环）。返回切换后的索引；
 * prev 在第 0 个会话再左滑时返回 -1（表示应回主页）。 */
int ui_session_next(void);
int ui_session_prev(void);

/* 定位到「最近会话」（索引 0）并刷新 */
void ui_session_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SESSION_H */
