/* app_main.c — 入口 + BSP 初始化 + 视图切换 + 手势分发
 *
 * 四个视图是同一圆形画布上的不同层，靠滑动切换：
 *   主页 ─左滑→ 负一屏      主页 ─右滑→ 会话页      主页 ─下滑→ 控制中心
 *   会话页 右滑=下一个 / 左滑=上一个（首个再左滑回主页）
 *   任意屏：底边上滑 = 回主页
 *
 * 手势：用一个 LVGL 定时器轮询触摸 indev 的按下/抬起，交给 gesture.c 分类。
 * 竖直滑动交给 LVGL 原生滚动（负一屏/会话页内容），本分发器只处理导航。
 * 无任何通信，全部走 mock。
 */
#include "esp_log.h"

#include "bsp.h"
#include "ui_common.h"
#include "gesture.h"
#include "pet_states.h"
#include "ui_pet.h"
#include "ui_negative.h"
#include "ui_session.h"
#include "ui_control.h"

static const char *TAG = "app";

static lv_obj_t *s_view[4];      /* VIEW_PET / NEGATIVE / SESSION / CONTROL */
static view_id_t s_current = VIEW_PET;

/* 只显示 target 视图，其余隐藏 */
static void show_view(view_id_t target)
{
    for (int i = 0; i < 4; i++) {
        if (i == target) lv_obj_clear_flag(s_view[i], LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_view[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_current = target;

    if (target == VIEW_PET)     ui_pet_refresh();
    if (target == VIEW_SESSION) ui_session_reset();
}

/* 处理一次已分类的手势 */
static void dispatch(gesture_t g)
{
    /* 底边上滑：全局回主页（控制中心除外，它的上滑即返回）*/
    if (g == GESTURE_HOME) {
        show_view(VIEW_PET);
        return;
    }

    switch (s_current) {
    case VIEW_PET:
        switch (g) {
        case GESTURE_LEFT:  show_view(VIEW_NEGATIVE); break;
        case GESTURE_RIGHT: show_view(VIEW_SESSION);  break;
        case GESTURE_DOWN:  show_view(VIEW_CONTROL);  break;
        case GESTURE_TAP:   ui_pet_poke();            break;
        default: break;
        }
        break;

    case VIEW_NEGATIVE:
        /* 右滑回主页；上下滑交给 LVGL 原生滚动 */
        if (g == GESTURE_RIGHT) show_view(VIEW_PET);
        break;

    case VIEW_SESSION:
        if (g == GESTURE_RIGHT) {
            ui_session_next();
        } else if (g == GESTURE_LEFT) {
            if (ui_session_prev() < 0) show_view(VIEW_PET);  /* 首个再左滑回主页 */
        }
        /* 上下滑：LVGL 原生滚动内容 */
        break;

    case VIEW_CONTROL:
        /* 上滑返回主页 */
        if (g == GESTURE_UP || g == GESTURE_HOME) show_view(VIEW_PET);
        break;
    }
}

/* -------- 手势轮询：跟踪按下→抬起 -------- */
static bool s_was_pressed;

static void gesture_poll_cb(lv_timer_t *t)
{
    (void)t;
    lv_indev_t *indev = bsp_touch_indev();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_indev_state_t st = lv_indev_get_state(indev);

    if (st == LV_INDEV_STATE_PRESSED && !s_was_pressed) {
        s_was_pressed = true;
        gesture_press((int16_t)p.x, (int16_t)p.y);
    } else if (st == LV_INDEV_STATE_RELEASED && s_was_pressed) {
        s_was_pressed = false;
        gesture_t g = gesture_release((int16_t)p.x, (int16_t)p.y);
        if (g != GESTURE_NONE) dispatch(g);
    }
}

/* 语音按钮点击回调在 ui_negative.c 内部自挂，这里不再委托 */

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_VOID, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_view[VIEW_PET]      = ui_pet_create(scr);
    s_view[VIEW_NEGATIVE] = ui_negative_create(scr);
    s_view[VIEW_SESSION]  = ui_session_create(scr);
    s_view[VIEW_CONTROL]  = ui_control_create(scr);

    show_view(VIEW_PET);

    /* 手势轮询 30ms */
    lv_timer_create(gesture_poll_cb, 30, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "DeskPet Agent firmware demo starting");
    pet_state_set(PET_IDLE, SRC_CLAUDE_CODE);

    bsp_init();

    if (bsp_lvgl_lock(-1)) {
        build_ui();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "UI ready");
}
