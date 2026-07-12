/* ui_session.c — ③ 会话页（单会话一页）
 *
 * 每个会话渲染成一张 466×466 页：来源色点 + 名 + 时间、最新回复、下一步 pill、
 * 历史消息。页内纵向可滚动翻看长内容；横向翻会话由外层 pager 负责。
 */
#include "ui_session.h"
#include "ui_common.h"
#include "session_store.h"
#include "bsp.h"
#include <time.h>

/* CJK 字体：优先 FreeType，失败则回退 montserrat（ASCII 可渲染）*/
static inline const lv_font_t *cjx(int sz) {
    lv_font_t *f = bsp_cjk_font(sz);
    return f ? f : (sz <= 14 ? &lv_font_montserrat_14 :
                    sz <= 16 ? &lv_font_montserrat_16 :
                    sz <= 24 ? &lv_font_montserrat_24 :
                               &lv_font_montserrat_24);
}
#define SESS_FONT_NAME  (cjx(32))
#define SESS_FONT_BODY  (cjx(24))
#define SESS_FONT_SMALL (cjx(20))
#define SESS_W          300   /* 内容宽度，居中落在圆内 */

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

lv_obj_t *ui_session_create_page(lv_obj_t *parent,
                                 const stored_session_t *session)
{
    const stored_session_t *ss = session;

    /* 页根：满屏、纯黑、不可滚（滚动交给内部 content）*/
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* 可滚动内容（上下滑翻页），落在安全圆内。
     * 只允许纵向滚动；横向不消费，靠 SCROLL_CHAIN_HOR（默认开）把横滑冒泡给
     * 外层 pager 翻会话——即使按在文字/标签上，也能横向翻页。
     * content 句柄存到 root 的 user_data，供 ui_session_scroll_top() 使用。 */
    lv_obj_t *content = lv_obj_create(root);
    lv_obj_set_user_data(root, content);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 340, SCREEN_SIZE - 2 * (EDGE_SAFE + 20));
    lv_obj_center(content);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, SP_SM, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    /* 顶部：来源色点 + 名 + 时间 */
    lv_obj_t *head = lv_obj_create(content);
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

    /* 时间戳转 HH:MM */
    char time_str[8];
    {
        time_t sec = ss->ts / 1000;
        struct tm tm;
        localtime_r(&sec, &tm);
        snprintf(time_str, sizeof(time_str), "%02d:%02d",
                 tm.tm_hour, tm.tm_min);
    }
    lv_obj_t *time = lv_label_create(head);
    lv_label_set_text(time, time_str);
    lv_obj_set_style_text_font(time, SESS_FONT_SMALL, 0);
    lv_obj_set_style_text_color(time, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(time, LV_OPA_40, 0);

    divider(content);

    /* 最新回复 */
    body_label(content, "Latest reply:", SESS_FONT_SMALL, COLOR_MIST);
    body_label(content, ss->last_reply[0] ? ss->last_reply : "—",
               SESS_FONT_BODY, COLOR_MIST);

    divider(content);

    /* 下一步 pill（mint）*/
    if (ss->next_step[0]) {
        lv_obj_t *pill = lv_obj_create(content);
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
        body_label(content, LV_SYMBOL_OK "  Waiting for your input", SESS_FONT_BODY, COLOR_MIST);
    }

    divider(content);

    /* 历史 */
    body_label(content, "History:", SESS_FONT_SMALL, COLOR_MIST);
    for (int i = 0; i < ss->history_len; i++) {
        const storable_msg_t *m = &ss->history[i];
        lv_color_t c = m->from_user ? COLOR_MIST : ui_source_color(ss->source);
        lv_obj_t *l = body_label(content, m->text, SESS_FONT_BODY, c);
        if (!m->from_user) lv_obj_set_style_text_opa(l, LV_OPA_90, 0);
        else               lv_obj_set_style_text_opa(l, LV_OPA_60, 0);
    }

    /* 底部固定指示：上滑返回（弧形白条贴合屏幕下缘 + 上箭头）。
     * 挂在 root 上（不是 content），所以纵向滚动内容时它固定不动、浮在最上层；
     * 关闭点击，避免挡住内容上下滑 / pager 左右滑手势。 */
    lv_obj_t *back_arc = lv_arc_create(root);
    lv_obj_remove_style_all(back_arc);
    lv_obj_set_size(back_arc, 430, 430);          /* 直径 → 半径≈212，贴近圆屏下缘 */
    lv_obj_center(back_arc);
    lv_arc_set_angles(back_arc, 55, 125);         /* 底部约 70° 弧，中点 90°(正下方) */
    lv_obj_clear_flag(back_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(back_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(back_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(back_arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(back_arc, LV_OPA_80, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(back_arc, 0, LV_PART_MAIN);     /* 隐藏背景轨道 */
    lv_obj_set_style_arc_opa(back_arc, LV_OPA_TRANSP, LV_PART_KNOB); /* 隐藏旋钮 */

    lv_obj_t *back_ico = lv_label_create(root);
    lv_label_set_text(back_ico, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(back_ico, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(back_ico, lv_color_white(), 0);
    lv_obj_set_style_text_opa(back_ico, LV_OPA_80, 0);
    lv_obj_clear_flag(back_ico, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(back_ico, LV_ALIGN_CENTER, 0, 196);  /* 弧中点正上方一点 */

    return root;
}

void ui_session_scroll_top(lv_obj_t *page)
{
    lv_obj_t *content = lv_obj_get_user_data(page);
    if (content)
        lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
}
