/* ui_pet.c — ① 主页桌宠视图（GIF 动画版） */
#include "ui_pet.h"
#include "ui_common.h"
#include "pet_states.h"
#include "clawd_assets.h"
#include "mock_data.h"

/* 桌宠基准缩放：GIF 素材 250px，整屏居中显示 ≈390px。
 * scale = 390*256/250 ≈ 399。 */
#define PET_SCALE_GIF    399
#define PET_SCALE_BREATH 411   /* 呼吸峰值 ≈ +3% */

/* disconnected 静态图缩放（150px → ~390px）*/
#define PET_SCALE_IMG    665
#define PET_Y_OFFSET     (-40)

static lv_obj_t *s_root;
static lv_obj_t *s_halo;
static lv_obj_t *s_gif;         /* lv_gif widget（11 个动画态）*/
static lv_obj_t *s_img;         /* lv_image widget（disconnected 回退）*/
static lv_obj_t *s_active;      /* 当前活跃的 pet widget（gif 或 img）*/
static lv_obj_t *s_bubble;
static lv_obj_t *s_bubble_title;
static lv_obj_t *s_bubble_body;
static lv_obj_t *s_status_lbl;

/* idle 呼吸动画（缩放）*/
static void breath_cb(void *pet, int32_t v)
{
    lv_image_set_scale((lv_obj_t *)pet, v);
}

/* 戳一下结束后把旋转归零 */
static void poke_done_cb(lv_anim_t *a)
{
    lv_image_set_rotation((lv_obj_t *)a->var, 0);
}

/* 戳一下：绕中心轻微旋转抖动 */
static void poke_anim(void)
{
    if (!s_active) return;
    lv_anim_delete(s_active, (lv_anim_exec_xcb_t)lv_image_set_rotation);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_active);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_image_set_rotation);
    lv_anim_set_values(&a, -20, 20);   /* ±2° */
    lv_anim_set_duration(&a, 80);
    lv_anim_set_playback_duration(&a, 80);
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

    s_halo = ui_halo_create(s_root);

    /* GIF widget（动画态用）*/
    s_gif = lv_gif_create(s_root);
    lv_obj_set_size(s_gif, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_align(s_gif, LV_ALIGN_CENTER, 0, PET_Y_OFFSET);
    lv_image_set_inner_align(s_gif, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_scale(s_gif, PET_SCALE_GIF);
    lv_image_set_rotation(s_gif, 0);
    /* 默认 loop_count=-1，按 GIF 文件自身循环（一般都是无限），无需显式设置 */

    /* 静态 image widget（disconnected 回退）*/
    s_img = lv_image_create(s_root);
    lv_obj_set_size(s_img, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_align(s_img, LV_ALIGN_CENTER, 0, PET_Y_OFFSET);
    lv_image_set_inner_align(s_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_scale(s_img, PET_SCALE_IMG);
    lv_image_set_rotation(s_img, 0);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    /* 呼吸动画：基准 → 峰值，1.5s 往返循环。挂在 s_gif 上；
     * disconnected 时 s_gif 隐藏，动画不影响。 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_gif);
    lv_anim_set_exec_cb(&a, breath_cb);
    lv_anim_set_values(&a, PET_SCALE_GIF, PET_SCALE_BREATH);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_playback_duration(&a, 1500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    /* 通知气泡 */
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

    s_status_lbl = ui_hint(s_root, "");

    ui_pet_refresh();
    return s_root;
}

void ui_pet_refresh(void)
{
    if (!s_root) return;
    pet_state_t st = pet_state_get();
    agent_source_t src = pet_source_get();

    const lv_image_dsc_t *dsc = clawd_asset_for(st);
    bool is_gif = clawd_is_gif(st);

    if (is_gif) {
        lv_gif_set_src(s_gif, dsc);
        lv_obj_clear_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        s_active = s_gif;
    } else {
        lv_image_set_src(s_img, dsc);
        lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        s_active = s_img;
    }

    ui_halo_apply(s_halo, st, src);

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
