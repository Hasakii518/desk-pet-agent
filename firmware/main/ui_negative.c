/* ui_negative.c — ② 负一屏 */
#include "ui_negative.h"
#include "ui_common.h"
#include "mock_data.h"

typedef enum { VOICE_IDLE, VOICE_RECORDING, VOICE_RECOGNIZING, VOICE_SENT } voice_state_t;

static lv_obj_t *s_root;
static lv_obj_t *s_voice_btn;
static lv_obj_t *s_voice_icon;
static lv_obj_t *s_voice_lbl;
static voice_state_t s_voice = VOICE_IDLE;

static void voice_render(void)
{
    switch (s_voice) {
    case VOICE_IDLE:
        lv_obj_set_style_bg_color(s_voice_btn, COLOR_STONE, 0);
        lv_obj_set_style_text_color(s_voice_icon, COLOR_MIST, 0);
        lv_label_set_text(s_voice_lbl, "Idle");
        lv_obj_set_style_text_color(s_voice_lbl, COLOR_MIST, 0);
        break;
    case VOICE_RECORDING:
        lv_obj_set_style_bg_color(s_voice_btn, COLOR_CORAL, 0);
        lv_obj_set_style_text_color(s_voice_icon, COLOR_VOID, 0);
        lv_label_set_text(s_voice_lbl, "Listening...");
        lv_obj_set_style_text_color(s_voice_lbl, COLOR_CORAL, 0);
        break;
    case VOICE_RECOGNIZING:
        lv_obj_set_style_bg_color(s_voice_btn, COLOR_WORKBLUE, 0);
        lv_obj_set_style_text_color(s_voice_icon, COLOR_VOID, 0);
        lv_label_set_text(s_voice_lbl, "Recognizing...");
        lv_obj_set_style_text_color(s_voice_lbl, COLOR_WORKBLUE, 0);
        break;
    case VOICE_SENT:
        lv_obj_set_style_bg_color(s_voice_btn, COLOR_MINT, 0);
        lv_obj_set_style_text_color(s_voice_icon, COLOR_VOID, 0);
        lv_label_set_text(s_voice_lbl, "Sent");
        lv_obj_set_style_text_color(s_voice_lbl, COLOR_MINT, 0);
        break;
    }
}

/* demo：定时推进语音状态机 */
static void voice_advance_cb(lv_timer_t *t)
{
    switch (s_voice) {
    case VOICE_RECORDING:    s_voice = VOICE_RECOGNIZING; break;
    case VOICE_RECOGNIZING:  s_voice = VOICE_SENT;        break;
    case VOICE_SENT:         s_voice = VOICE_IDLE;        break;
    default:                 s_voice = VOICE_IDLE;        break;
    }
    voice_render();
    if (s_voice == VOICE_IDLE) lv_timer_delete(t);
}

void ui_negative_voice_tap(void)
{
    if (s_voice != VOICE_IDLE) return;
    s_voice = VOICE_RECORDING;
    voice_render();
    lv_timer_create(voice_advance_cb, 900, NULL);
}

/* 语音按钮点击回调 */
static void voice_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_negative_voice_tap();
}

/* 一张 label 行：左标题 + 右值 */
static void kv_row(lv_obj_t *card, const char *k, const char *v)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kl = lv_label_create(row);
    lv_label_set_text(kl, k);
    lv_obj_set_style_text_font(kl, FONT_T4, 0);
    lv_obj_set_style_text_color(kl, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(kl, LV_OPA_60, 0);

    lv_obj_t *vl = lv_label_create(row);
    lv_label_set_text(vl, v);
    lv_obj_set_style_text_font(vl, FONT_T3, 0);
    lv_obj_set_style_text_color(vl, COLOR_MIST, 0);
}

/* 一张带标题的信息卡 */
static lv_obj_t *titled_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = ui_card(parent, 320, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, SP_XS, 0);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, FONT_T4, 0);
    lv_obj_set_style_text_color(t, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(t, LV_OPA_40, 0);
    return card;
}

lv_obj_t *ui_negative_create(lv_obj_t *parent)
{
    const mock_hardware_t *hw = mock_hardware();

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    /* 纵向居中堆叠，可上下滑滚动 */
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, SP_MD, 0);
    lv_obj_set_style_pad_top(s_root, EDGE_SAFE + 20, 0);
    lv_obj_set_style_pad_bottom(s_root, EDGE_SAFE + 20, 0);
    lv_obj_set_scroll_dir(s_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_root, LV_SCROLLBAR_MODE_OFF);

    /* 负一屏不显示外缘 Halo（仅主页显示）*/

    /* 语音按钮：圆形 48px（放大到 64 更易点），上方居中 */
    s_voice_btn = lv_obj_create(s_root);
    lv_obj_set_size(s_voice_btn, 64, 64);
    lv_obj_set_style_radius(s_voice_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_voice_btn, 0, 0);
    lv_obj_clear_flag(s_voice_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_voice_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_voice_btn, voice_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_voice_icon = lv_label_create(s_voice_btn);
    lv_label_set_text(s_voice_icon, LV_SYMBOL_AUDIO);
    lv_obj_center(s_voice_icon);

    s_voice_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_voice_lbl, FONT_T4, 0);
    voice_render();

    /* 设备卡 */
    lv_obj_t *dev = titled_card(s_root, "DEVICE");
    kv_row(dev, hw->device, "");
    {
        static char batt[32];
        lv_snprintf(batt, sizeof(batt), "%d%% %s", hw->battery_pct,
                    hw->charging ? LV_SYMBOL_CHARGE : "");
        kv_row(dev, "Battery", batt);
    }
    kv_row(dev, "WiFi", hw->wifi);

    /* 系统卡 */
    lv_obj_t *sys = titled_card(s_root, "SYSTEM");
    {
        static char cpu[16];
        lv_snprintf(cpu, sizeof(cpu), "%d%%", hw->cpu_pct);
        kv_row(sys, "CPU", cpu);
    }
    kv_row(sys, "Memory", hw->mem);
    {
        static char lap[16];
        lv_snprintf(lap, sizeof(lap), "%d%%", hw->laptop_battery_pct);
        kv_row(sys, "Laptop", lap);
    }

    /* 预留 slot */
    ui_slot_placeholder(s_root, "slot:weather");
    ui_slot_placeholder(s_root, "slot:shortcuts");

    ui_hint(s_root, "Swipe right: back home");
    return s_root;
}
