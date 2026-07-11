/* ui_nav.c — 横向 snap 翻页导航 + 控制中心覆盖层 */
#include "ui_nav.h"
#include "ui_common.h"
#include "pet_states.h"
#include "mock_data.h"
#include "ui_pet.h"
#include "ui_negative.h"
#include "ui_session.h"
#include "ui_control.h"

#define PAGE_W SCREEN_SIZE

static lv_obj_t *s_pager;
static lv_obj_t *s_control;      /* 控制中心覆盖层 */
static int       s_home_index;   /* 主页在页序列中的索引 */
static int       s_page_count;
static bool      s_control_visible;

/* 由滚动位置算出当前最接近的页索引 */
static int current_page(void)
{
    int sx = lv_obj_get_scroll_x(s_pager);
    int idx = (sx + PAGE_W / 2) / PAGE_W;
    if (idx < 0) idx = 0;
    if (idx >= s_page_count) idx = s_page_count - 1;
    return idx;
}

bool ui_nav_at_home(void)
{
    return current_page() == s_home_index;
}

/* 停止滚动后：刷新目标页，并重置滚动到顶部 */
static void pager_scroll_end_cb(lv_event_t *e)
{
    (void)e;
    int idx = current_page();
    lv_obj_t *page = lv_obj_get_child(s_pager, idx);

    if (idx == 0) {
        /* 负一屏 */
        ui_negative_scroll_top();
    } else if (idx == s_home_index) {
        /* 主页 */
        ui_pet_refresh();
    } else if (idx > s_home_index && page) {
        /* 会话页 */
        ui_session_scroll_top(page);
    }
}

void ui_nav_go_home(void)
{
    lv_obj_scroll_to_x(s_pager, s_home_index * PAGE_W, LV_ANIM_ON);
}

/* -------- 控制中心覆盖层：从顶部滑入 / 滑出 -------- */

static void control_hide_done(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

void ui_nav_control_show(void)
{
    if (s_control_visible) return;
    s_control_visible = true;
    lv_obj_clear_flag(s_control, LV_OBJ_FLAG_HIDDEN);
    /* 实测：y 增大 = 物理向下。从 y=-SCREEN_SIZE（屏上方外侧）滑到 y=0
     * 即为"向下拉出"；反向即为"向上收起"。 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_control);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, -SCREEN_SIZE, 0);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_nav_control_hide(void)
{
    if (!s_control_visible) return;
    s_control_visible = false;
    /* 从 y=0 滑回 y=-SCREEN_SIZE（物理向上收起），动画结束后隐藏 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_control);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, 0, -SCREEN_SIZE);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, control_hide_done);
    lv_anim_start(&a);
}

bool ui_nav_control_visible(void) { return s_control_visible; }

/* -------- 构建 -------- */

void ui_nav_create(lv_obj_t *scr)
{
    int n = mock_session_count();
    s_home_index = 1;                 /* 负一屏(0), 主页(1), 会话(2..n+1) */
    s_page_count = n + 2;

    /* 横向 pager：满屏、flex row、snap 居中、一次一页 */
    s_pager = lv_obj_create(scr);
    lv_obj_remove_style_all(s_pager);
    lv_obj_set_size(s_pager, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_pager, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_pager, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(s_pager, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_pager, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(s_pager, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(s_pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(s_pager, 0, 0);
    lv_obj_set_style_pad_column(s_pager, 0, 0);

    /* 页序列（左→右）：负一屏, 主页, 会话0(最新) … 会话N-1(最旧)
     * 原生滚动：右滑露出左侧页 → 主页右滑到负一屏；左滑露出右侧页 → 主页左滑到最新会话，
     * 继续左滑翻更旧会话，最旧到边界停住（不循环）。 */
    ui_negative_create(s_pager);              /* index 0 */
    ui_pet_create(s_pager);                   /* index 1 = 主页 */
    for (int i = 0; i < n; i++)               /* 最新在前，逐渐更旧 */
        ui_session_create_page(s_pager, i);

    lv_obj_add_event_cb(s_pager, pager_scroll_end_cb, LV_EVENT_SCROLL_END, NULL);

    /* 初始定位到主页（无动画）*/
    lv_obj_update_layout(s_pager);
    lv_obj_scroll_to_x(s_pager, s_home_index * PAGE_W, LV_ANIM_OFF);

    /* 控制中心覆盖层：scr 顶层，初始藏在屏幕上方外 */
    s_control = ui_control_create(scr);
    lv_obj_set_pos(s_control, 0, -SCREEN_SIZE);  /* 初始停在屏上方外侧 */
    lv_obj_add_flag(s_control, LV_OBJ_FLAG_HIDDEN);
}
