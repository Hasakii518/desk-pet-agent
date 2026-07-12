/* ui_pet.c — ① 主页桌宠视图（GIF 动画版） */
#include "ui_pet.h"
#include "ui_common.h"
#include "pet_states.h"
#include "clawd_assets.h"
#include "session_store.h"
#include "bsp.h"

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
static lv_obj_t *s_modal;          /* 点击气泡放大的全屏浮层 */
static lv_obj_t *s_modal_title;
static lv_obj_t *s_modal_body;

/* 前置声明 */
static void bubble_click_cb(lv_event_t *e);
static void modal_click_cb(lv_event_t *e);

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
    lv_obj_set_style_text_font(s_bubble_title,
                    bsp_body_font(), 0);

    s_bubble_body = lv_label_create(s_bubble);
    /* CJK > emoji > montserrat 优先级 */
    lv_font_t *body_font = bsp_cjk_font(28);
    if (!body_font) body_font = bsp_emoji_font();
    if (!body_font) body_font = (lv_font_t *)&lv_font_montserrat_28;
    lv_obj_set_style_text_font(s_bubble_body, body_font, 0);
    lv_obj_set_style_text_color(s_bubble_body, COLOR_MIST, 0);
    lv_label_set_long_mode(s_bubble_body, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_bubble_body, 268);

    s_status_lbl = ui_hint(s_root, "");

    /* 点击气泡 → 放大到全屏居中展示完整通知内容 */
    lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bubble, bubble_click_cb, LV_EVENT_CLICKED, NULL);

    /* 全屏浮层（初始隐藏）：暗色遮罩 + 居中卡片 + 标题 + 正文 */
    s_modal = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_modal, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_modal, modal_click_cb, LV_EVENT_CLICKED, NULL);

    /* 卡片内容区域 */
    lv_obj_t *card = lv_obj_create(s_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 380, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, SP_MD, 0);
    lv_obj_set_style_bg_color(card, COLOR_STONE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, RAD_CARD_L, 0);
    lv_obj_set_style_pad_all(card, SP_MD, 0);

    s_modal_title = lv_label_create(card);
    lv_obj_set_style_text_font(s_modal_title,
                    bsp_body_font(), 0);
    lv_label_set_long_mode(s_modal_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_modal_title, 340);

    s_modal_body = lv_label_create(card);
    lv_font_t *modal_font = bsp_cjk_font(28);
    if (!modal_font) modal_font = bsp_emoji_font();
    if (!modal_font) modal_font = (lv_font_t *)&lv_font_montserrat_28;
    lv_obj_set_style_text_font(s_modal_body, modal_font, 0);
    lv_obj_set_style_text_color(s_modal_body, COLOR_MIST, 0);
    lv_label_set_long_mode(s_modal_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_modal_body, 340);

    /* 底部提示：点任意处关闭 */
    lv_obj_t *hint = lv_label_create(s_modal);
    lv_label_set_text(hint, LV_SYMBOL_UP " tap to close");
    lv_obj_set_style_text_font(hint, FONT_T4, 0);
    lv_obj_set_style_text_color(hint, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(hint, LV_OPA_40, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -EDGE_SAFE);

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

    /* 只要 notify 帧带了 title/text 就显示气泡，不限 PET_NOTIFICATION 态。
     * bridge 下发的 typing/building/thinking 等都携带弹窗内容。 */
    if (session_store_has_notification()) {
        const char *notif_title = session_store_notif_title();
        agent_source_t notif_src = session_store_notif_source();
        const char *notif_text = session_store_notif_text();

        lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_bubble_title, "%s  %s",
                              LV_SYMBOL_BELL,
                              notif_title ? notif_title : "Claude");
        lv_obj_set_style_text_color(s_bubble_title,
                                    ui_source_color(notif_src), 0);
        lv_label_set_text(s_bubble_body,
                          notif_text ? notif_text : "");
    } else {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_status_lbl, ui_state_label(st));
}

void ui_pet_poke(void)
{
    poke_anim();
    /* 不再循环 mock state——真实 state 由串口协议驱动 */
    ui_pet_refresh();
}

/* ---- 气泡点击放大 ---- */

static void bubble_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_modal) return;
    if (!session_store_has_notification()) return;

    const char *title = session_store_notif_title();
    agent_source_t src = session_store_notif_source();
    const char *text  = session_store_notif_text();

    lv_label_set_text_fmt(s_modal_title, "%s  %s",
                          LV_SYMBOL_BELL,
                          title ? title : "Claude");
    lv_obj_set_style_text_color(s_modal_title, ui_source_color(src), 0);

    lv_label_set_text(s_modal_body, text ? text : "");
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

static void modal_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_modal)
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}
