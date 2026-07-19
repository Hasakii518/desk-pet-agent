/* ui_connect.c — 连接详情页实现 */
#include "ui_connect.h"
#include "ui_common.h"
#include "bt_stack.h"
#include "wifi_prov.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_overlay;   /* 全屏遮罩，NULL=未开 */
static lv_timer_t *s_timer;

/* 行标签句柄：BLE 状态 / WiFi 状态 / IP / 信号 */
static lv_obj_t *s_lbl_ble;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_lbl_rssi;

static const char *wifi_state_cn(void)
{
    const char *s = wifi_prov_state_str();
    if (strcmp(s, "ok") == 0)         return "已连接";
    if (strcmp(s, "connecting") == 0) return "连接中…";
    if (strcmp(s, "fail") == 0)       return "失败";
    return "未配置";
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_overlay) return;

    char buf[64];
    char name[16];
    bt_stack_device_name(name, sizeof(name));
    snprintf(buf, sizeof(buf), "%s  %s",
             name, bt_stack_connected() ? "已连接" : "广播中…");
    lv_label_set_text(s_lbl_ble, buf);

    snprintf(buf, sizeof(buf), "%s  %s", wifi_state_cn(), wifi_prov_ssid());
    lv_label_set_text(s_lbl_wifi, buf);

    char ip[16];
    wifi_prov_ip_str(ip, sizeof(ip));
    if (wifi_prov_connected()) {
        snprintf(buf, sizeof(buf), "IP  %s", ip);
        lv_label_set_text(s_lbl_ip, buf);
        snprintf(buf, sizeof(buf), "信号  %d dBm", wifi_prov_rssi());
        lv_label_set_text(s_lbl_rssi, buf);
    } else if (strcmp(wifi_prov_state_str(), "fail") == 0) {
        snprintf(buf, sizeof(buf), "错误码  %d", wifi_prov_last_err());
        lv_label_set_text(s_lbl_ip, buf);
        lv_label_set_text(s_lbl_rssi, "检查密码 / 信号后重试");
    } else {
        lv_label_set_text(s_lbl_ip, "IP  --");
        lv_label_set_text(s_lbl_rssi, "信号  --");
    }
}

/* 点遮罩空白关闭（点卡片上不关）*/
static void mask_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED &&
        lv_event_get_target(e) == s_overlay) {
        ui_connect_close();
    }
}

/* 造一行：图标 + 标题 + 值标签（返回值标签句柄）*/
static lv_obj_t *make_row(lv_obj_t *card, const char *icon, const char *title,
                          const char *value, lv_color_t icon_color)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, SP_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ic, icon_color, 0);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tt = lv_label_create(col);
    lv_label_set_text(tt, title);
    lv_obj_set_style_text_font(tt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tt, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(tt, LV_OPA_60, 0);

    lv_obj_t *vv = lv_label_create(col);
    lv_label_set_text(vv, value);
    lv_obj_set_style_text_font(vv, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vv, COLOR_MIST, 0);
    return vv;
}

void ui_connect_open(lv_obj_t *parent)
{
    ui_connect_close();

    s_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_overlay, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, mask_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, 340);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COLOR_STONE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, RAD_CARD_L, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, SP_LG, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, SP_MD, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Connection");
    lv_obj_set_style_text_font(title, FONT_T4, 0);
    lv_obj_set_style_text_color(title, COLOR_MIST, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, LV_PCT(100));

    s_lbl_ble  = make_row(card, LV_SYMBOL_BLUETOOTH, "Bluetooth", "--", COLOR_WORKBLUE);
    s_lbl_wifi = make_row(card, LV_SYMBOL_WIFI,      "WiFi",      "--", COLOR_WORKBLUE);
    s_lbl_ip   = make_row(card, LV_SYMBOL_LIST,      "网络",       "--", COLOR_MINT);
    s_lbl_rssi = make_row(card, LV_SYMBOL_EYE_OPEN,  "信号",       "--", COLOR_MINT);

    ui_hint(card, "Tap outside to close");

    s_timer = lv_timer_create(refresh_cb, 1000, NULL);
    refresh_cb(NULL);
}

bool ui_connect_visible(void)
{
    return s_overlay != NULL;
}

void ui_connect_close(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
}
