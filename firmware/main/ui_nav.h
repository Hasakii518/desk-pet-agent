/* ui_nav.h — 横向 snap 翻页导航（Route A：跟手翻页）
 *
 * 把主页、负一屏、各会话平铺成一条横向可滚动的页序列，用 LVGL 原生滚动 +
 * snap 吸附实现「跟手拖拽 + 惯性 + 吸附」。控制中心是 scr 顶层覆盖层，不进页序列。
 *
 * 页序列（左→右），使方向符合规格（原生滚动：右滑露出左侧页，左滑露出右侧页）：
 *   [负一屏, 主页, 会话0(最新) … 会话N-1(最旧)]
 *   - 主页右滑 → 负一屏。
 *   - 主页左滑 → 会话0(最新)；会话左滑 → 更旧；最旧再左滑到边界停住（不循环）。
 */
#ifndef UI_NAV_H
#define UI_NAV_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 在 scr 上构建整套导航（pager + 控制中心覆盖层）。落在活动屏上。 */
void ui_nav_create(lv_obj_t *scr);

/* 滚动回主页（带动画）。用于底边上滑「全局回家」。 */
void ui_nav_go_home(void);

/* 当前是否停在主页（用于门控戳一下 / 下拉控制中心）。 */
bool ui_nav_at_home(void);

/* 重建会话页（session store 数量变化时调用）。
 * 保留负一屏(index 0)和主页(index 1)，只重建 index 2+ 的会话页。 */
void ui_nav_rebuild_sessions(void);

/* 控制中心覆盖层 */
void ui_nav_control_show(void);
void ui_nav_control_hide(void);
bool ui_nav_control_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_NAV_H */
