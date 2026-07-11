/* ui_common.c — 通用工具实现 */
#include "ui_common.h"

lv_color_t ui_source_color(agent_source_t src)
{
    return (src == SRC_CLAUDE_CODE) ? COLOR_CLAUDEPURPLE : COLOR_WORKBLUE;
}

lv_color_t ui_state_halo_color(pet_state_t st, agent_source_t src)
{
    switch (st) {
    case PET_DISCONNECTED: return COLOR_CORAL;
    case PET_ERROR:        return COLOR_CORAL;
    case PET_PERMISSION:   return COLOR_AMBER;
    case PET_NOTIFICATION: return ui_source_color(src);
    case PET_HAPPY:        return COLOR_MINT;
    case PET_SLEEPING:     return COLOR_STONE;
    default:               return ui_source_color(src);
    }
}

const char *ui_state_label(pet_state_t st)
{
    switch (st) {
    case PET_DISCONNECTED: return "Offline";
    case PET_ERROR:        return "Error";
    case PET_PERMISSION:   return "Permission";
    case PET_NOTIFICATION: return "Message";
    case PET_BUILDING:     return "Building";
    case PET_TYPING:       return "Typing";
    case PET_THINKING:     return "Thinking";
    case PET_SPEAKING:     return "Speaking";
    case PET_WAITING:      return "Waiting";
    case PET_HAPPY:        return "Happy";
    case PET_IDLE:         return "Idle";
    case PET_SLEEPING:     return "Sleeping";
    default:               return "?";
    }
}

lv_obj_t *ui_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w > 0 ? w : LV_SIZE_CONTENT, h > 0 ? h : LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COLOR_STONE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, RAD_CARD_L, 0);
    lv_obj_set_style_pad_all(card, SP_MD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_slot_placeholder(lv_obj_t *parent, const char *slot_name)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_set_size(slot, 300, 56);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(slot, COLOR_STONE, 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    lv_obj_set_style_border_opa(slot, LV_OPA_80, 0);
    /* 虚线感：用圆角 + 半透明边框近似（LVGL 无原生虚线边框）*/
    lv_obj_set_style_radius(slot, RAD_TILE, 0);
    lv_obj_set_style_pad_all(slot, SP_SM, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(slot);
    lv_label_set_text(title, slot_name);
    lv_obj_set_style_text_color(title, COLOR_MIST, 0);
    lv_obj_set_style_text_font(title, FONT_T4, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -8);

    lv_obj_t *state = lv_label_create(slot);
    lv_label_set_text(state, "disabled");
    lv_obj_set_style_text_color(state, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(state, LV_OPA_40, 0);
    lv_obj_set_style_text_font(state, FONT_T4, 0);
    lv_obj_align(state, LV_ALIGN_LEFT_MID, 0, 10);
    return slot;
}

lv_obj_t *ui_hint(lv_obj_t *parent, const char *text)
{
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, text);
    lv_obj_set_style_text_color(hint, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(hint, LV_OPA_40, 0);
    lv_obj_set_style_text_font(hint, FONT_T4, 0);
    /* 底部安全圆内 */
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -(EDGE_SAFE + 12));
    return hint;
}

/* ---------------- Source Halo ---------------- */

lv_obj_t *ui_halo_create(lv_obj_t *parent)
{
    lv_obj_t *halo = lv_arc_create(parent);
    lv_obj_set_size(halo, SCREEN_SIZE - 6, SCREEN_SIZE - 6);
    /* Halo 是悬浮外缘环，绝不参与父容器的 flex/flow 布局，
     * 否则会把负一屏等纵向堆叠内容挤出屏幕。用 IGNORE_LAYOUT + 居中对齐。 */
    lv_obj_add_flag(halo, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_style(halo, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(halo, 270);
    lv_arc_set_bg_angles(halo, 0, 360);

    /* 背景弧几乎不可见 */
    lv_obj_set_style_arc_color(halo, COLOR_STONE, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(halo, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(halo, 3, LV_PART_MAIN);

    /* 前景弧 = 情绪灯 */
    lv_obj_set_style_arc_width(halo, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(halo, true, LV_PART_INDICATOR);
    return halo;
}

/* Halo 旋转动画回调：整环旋转，让固定长度的指示弧沿外缘扫过 */
static void halo_sweep_cb(void *arc, int32_t v)
{
    lv_arc_set_rotation((lv_obj_t *)arc, v);
}

void ui_halo_apply(lv_obj_t *halo, pet_state_t st, agent_source_t src)
{
    if (!halo) return;
    lv_color_t c = ui_state_halo_color(st, src);
    lv_obj_set_style_arc_color(halo, c, LV_PART_INDICATOR);

    /* 清掉旧动画后按状态重建 */
    lv_anim_delete(halo, halo_sweep_cb);

    switch (st) {
    case PET_DISCONNECTED:
    case PET_ERROR:
        /* 常亮整环（快速三连闪略过，静态即可表意）*/
        lv_arc_set_angles(halo, 0, 360);
        lv_obj_set_style_arc_opa(halo, LV_OPA_COVER, LV_PART_INDICATOR);
        break;
    case PET_HAPPY:
    case PET_SLEEPING:
        /* 稳定微光整环 */
        lv_arc_set_angles(halo, 0, 360);
        lv_obj_set_style_arc_opa(halo, LV_OPA_60, LV_PART_INDICATOR);
        break;
    default: {
        /* 一段固定弧沿外缘匀速扫过（2s 周期），通过旋转整个 arc 实现 */
        lv_obj_set_style_arc_opa(halo, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_arc_set_angles(halo, 0, 90);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, halo);
        lv_anim_set_exec_cb(&a, halo_sweep_cb);
        lv_anim_set_duration(&a, 2000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        /* Claude Code 逆时针，WorkBuddy 顺时针 */
        if (src == SRC_CLAUDE_CODE)
            lv_anim_set_values(&a, 360, 0);
        else
            lv_anim_set_values(&a, 0, 360);
        lv_anim_start(&a);
        break;
    }
    }
}
