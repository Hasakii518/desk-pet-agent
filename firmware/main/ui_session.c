/* ui_session.c — ③ 会话页 */
#include "ui_session.h"
#include "ui_common.h"
#include "mock_data.h"

/* 会话页专用字号：比全局 T 字号小一档，保证一屏能放下更多历史，
 * 且落在圆形安全区内（全局 T 已整体放大，这里不复用以免溢出圆屏）。 */
#define SESS_FONT_NAME  (&lv_font_montserrat_24)
#define SESS_FONT_BODY  (&lv_font_montserrat_16)
#define SESS_FONT_SMALL (&lv_font_montserrat_14)
#define SESS_W          300   /* 内容宽度，居中落在圆内 */

static lv_obj_t *s_root;
static lv_obj_t *s_content;   /* 可滚动内容容器 */
static int       s_index;     /* 当前会话索引 */

static lv_obj_t *divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, SESS_W, 1);
    lv_obj_set_style_bg_color(d, COLOR_STONE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

static lv_obj_t *body_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, SESS_W);
    return l;
}

/* 依据 s_index 重建内容 */
static void rebuild(void)
{
    const mock_session_t *ss = &mock_sessions()[s_index];
    lv_obj_clean(s_content);

    /* 顶部：来源色点 + 名 + 时间 */
    lv_obj_t *head = lv_obj_create(s_content);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, SESS_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, SP_SM, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(head);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, ui_source_color(ss->source), 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    lv_obj_t *name = lv_label_create(head);
    lv_label_set_text(name, ss->name);
    lv_obj_set_style_text_font(name, SESS_FONT_NAME, 0);
    lv_obj_set_style_text_color(name, COLOR_MIST, 0);

    lv_obj_t *time = lv_label_create(head);
    lv_label_set_text(time, ss->time);
    lv_obj_set_style_text_font(time, SESS_FONT_SMALL, 0);
    lv_obj_set_style_text_color(time, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(time, LV_OPA_40, 0);

    divider(s_content);

    /* 最新回复 */
    body_label(s_content, "Latest reply:", SESS_FONT_SMALL, COLOR_MIST);
    body_label(s_content, ss->last_reply, SESS_FONT_BODY, COLOR_MIST);

    divider(s_content);

    /* 下一步 pill（mint）*/
    if (ss->next_step) {
        lv_obj_t *pill = lv_obj_create(s_content);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pill, COLOR_MINT, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_set_style_pad_hor(pill, SP_MD, 0);
        lv_obj_set_style_pad_ver(pill, SP_SM, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *pl = lv_label_create(pill);
        lv_label_set_text_fmt(pl, LV_SYMBOL_PLAY "  %s", ss->next_step);
        lv_obj_set_style_text_font(pl, SESS_FONT_BODY, 0);
        lv_obj_set_style_text_color(pl, COLOR_VOID, 0);
    } else {
        body_label(s_content, LV_SYMBOL_OK "  Waiting for your input", SESS_FONT_BODY, COLOR_MIST);
    }

    divider(s_content);

    /* 历史 */
    body_label(s_content, "History:", SESS_FONT_SMALL, COLOR_MIST);
    for (int i = 0; i < ss->history_len; i++) {
        const mock_msg_t *m = &ss->history[i];
        lv_color_t c = m->from_user ? COLOR_MIST : ui_source_color(ss->source);
        lv_obj_t *l = body_label(s_content, m->text, SESS_FONT_BODY, c);
        if (!m->from_user) lv_obj_set_style_text_opa(l, LV_OPA_90, 0);
        else               lv_obj_set_style_text_opa(l, LV_OPA_60, 0);
    }

    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
}

lv_obj_t *ui_session_create(lv_obj_t *parent)
{
    s_index = 0;

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 会话页不显示外缘 Halo（仅主页显示）*/

    /* 可滚动内容（上下滑翻页），落在安全圆内 */
    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, 340, SCREEN_SIZE - 2 * (EDGE_SAFE + 20));
    lv_obj_center(s_content);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_content, SP_SM, 0);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);

    ui_hint(s_root, "Right: next  Left: prev");

    rebuild();
    return s_root;
}

int ui_session_next(void)
{
    /* 到最后一个会话就停住，不循环 */
    if (s_index >= mock_session_count() - 1) return s_index;
    s_index++;
    rebuild();
    return s_index;
}

int ui_session_prev(void)
{
    if (s_index == 0) return -1;   /* 首个再左滑 → 回主页 */
    s_index = (s_index - 1 + mock_session_count()) % mock_session_count();
    rebuild();
    return s_index;
}

void ui_session_reset(void)
{
    s_index = 0;
    if (s_content) rebuild();
}
