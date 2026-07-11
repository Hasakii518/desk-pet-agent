/* ui_pet.c — ① 主页桌宠视图 */
#include "ui_pet.h"
#include "ui_common.h"
#include "pet_states.h"
#include "clawd_assets.h"
#include "mock_data.h"

static lv_obj_t *s_root;
static lv_obj_t *s_halo;
static lv_obj_t *s_pet;        /* 桌宠图片 */
static lv_obj_t *s_bubble;     /* 通知气泡 */
static lv_obj_t *s_bubble_title;
static lv_obj_t *s_bubble_body;
static lv_obj_t *s_status_lbl; /* 底部状态英文名（调试友好）*/

/* 桌宠基准缩放：素材 150px。整屏图片对象 + 居中内对齐，缩放绕图片中心。
 * 目标显示 ≈390px，scale = 390*256/150 ≈ 665。 */
#define PET_SCALE_BASE   665
#define PET_SCALE_BREATH 685   /* 呼吸峰值 ≈ +3% */
/* 原始素材里 Clawd 在 150px 画框中偏下，整体上移补偿，使其视觉居中偏上 */
#define PET_Y_OFFSET     (-40)

/* idle 呼吸动画（缩放）*/
static void breath_cb(void *pet, int32_t v)
{
    lv_image_set_scale((lv_obj_t *)pet, v);  /* 256 = 1.0x */
}

/* 戳一下结束后把旋转归零，避免残留倾斜 */
static void poke_done_cb(lv_anim_t *a)
{
    lv_image_set_rotation((lv_obj_t *)a->var, 0);
}

/* 戳一下：绕中心轻微旋转抖动（pivot 已居中；结束归零）*/
static void poke_anim(void)
{
    lv_anim_delete(s_pet, (lv_anim_exec_xcb_t)lv_image_set_rotation);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pet);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_image_set_rotation);
    lv_anim_set_values(&a, -60, 60);   /* ±6° (0.1° 单位) */
    lv_anim_set_duration(&a, 100);
    lv_anim_set_playback_duration(&a, 100);
    lv_anim_set_repeat_count(&a, 2);
    lv_anim_set_completed_cb(&a, poke_done_cb);
    lv_anim_start(&a);
}

lv_obj_t *ui_pet_create(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* Source Halo */
    s_halo = ui_halo_create(s_root);

    /* 桌宠图片：整屏对象 + 居中内对齐。inner_align=CENTER 时 LVGL 默认
     * pivot 就是图片中心，缩放绕图片中心生长并保持居中，无需再设 pivot
     * （注意 lv_image_set_pivot 的坐标是图片自身 150px 空间，误传屏幕坐标会飞出屏）。 */
    s_pet = lv_image_create(s_root);
    lv_image_set_src(s_pet, clawd_image_for(pet_state_get()));
    lv_obj_set_size(s_pet, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_align(s_pet, LV_ALIGN_CENTER, 0, PET_Y_OFFSET);
    lv_image_set_inner_align(s_pet, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_scale(s_pet, PET_SCALE_BASE);
    lv_image_set_rotation(s_pet, 0);

    /* idle 呼吸：基准 → 峰值，1.5s 往返循环 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pet);
    lv_anim_set_exec_cb(&a, breath_cb);
    lv_anim_set_values(&a, PET_SCALE_BASE, PET_SCALE_BREATH);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_playback_duration(&a, 1500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    /* 通知气泡：圆心上方，胶囊卡片 */
    s_bubble = ui_card(s_root, 300, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(s_bubble, 20, 0);
    lv_obj_align(s_bubble, LV_ALIGN_TOP_MID, 0, EDGE_SAFE + 18);
    lv_obj_set_flex_flow(s_bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_bubble, SP_XS, 0);

    s_bubble_title = lv_label_create(s_bubble);
    lv_obj_set_style_text_font(s_bubble_title, FONT_T4, 0);

    s_bubble_body = lv_label_create(s_bubble);
    lv_obj_set_style_text_font(s_bubble_body, FONT_T3, 0);
    lv_obj_set_style_text_color(s_bubble_body, COLOR_MIST, 0);
    lv_label_set_long_mode(s_bubble_body, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_bubble_body, 268);

    /* 底部：状态名提示 */
    s_status_lbl = ui_hint(s_root, "");

    ui_pet_refresh();
    return s_root;
}

void ui_pet_refresh(void)
{
    if (!s_root) return;
    pet_state_t st = pet_state_get();
    agent_source_t src = pet_source_get();

    lv_image_set_src(s_pet, clawd_image_for(st));
    ui_halo_apply(s_halo, st, src);

    /* 通知气泡只在 notification 态显示 */
    if (st == PET_NOTIFICATION) {
        const mock_notification_t *n = mock_current_notification();
        lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_bubble_title, "%s  %s",
                              LV_SYMBOL_BELL, n->session_name);
        lv_obj_set_style_text_color(s_bubble_title, ui_source_color(n->source), 0);
        lv_label_set_text(s_bubble_body, n->text);
    } else {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_status_lbl, ui_state_label(st));
}

void ui_pet_poke(void)
{
    poke_anim();
    pet_state_cycle();
    ui_pet_refresh();
}
