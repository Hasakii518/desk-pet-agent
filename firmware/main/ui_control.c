/* ui_control.c — ④ 控制中心（下拉覆盖层）
 *
 * 3 列 × 3 行 tile：WiFi / 蓝牙 / 设置 / 亮度 / 勿扰 / 传输 / 刷新率。
 * 点击 tile 切换本地状态；长按 tile 进入二级菜单（浮窗），点空白（遮罩）返回上级。
 * 亮度二级菜单含竖向亮度条，实际调节面板亮度（SH8601 0x51），最低不熄灭。
 * 刷新率 Auto/30Hz/15Hz 切换 LVGL 显示刷新周期。
 */
#include "ui_control.h"
#include "ui_common.h"
#include "bsp.h"
#include "bt_stack.h"
#include "wifi_prov.h"
#include "ui_connect.h"
#include <string.h>

/* 每个 tile 的状态类型 */
typedef enum { TILE_TOGGLE, TILE_ACTION, TILE_CYCLE, TILE_BRIGHTNESS } tile_kind_t;

typedef struct {
    const char *icon;
    const char *label;
    tile_kind_t kind;
    bool        on;        /* TOGGLE 用 */
    lv_color_t  on_color;  /* 开启态填充色 */
    int         level;     /* CYCLE 用（传输档位）*/
    const char *levels[3]; /* CYCLE 档位名 */
    lv_obj_t   *obj;
    lv_obj_t   *icon_lbl;
    lv_obj_t   *dot;       /* 右上角指示点 */
    lv_obj_t   *sub;       /* 副标题（档位）*/
    bool        lp;        /* long-press 已触发，抑制随后的 CLICKED */
} tile_t;

static tile_t s_tiles[7];
static lv_obj_t *s_root;    /* 控制中心根，二级菜单浮窗挂这里 */
static lv_obj_t *s_submenu; /* 当前二级菜单遮罩，NULL=未开 */

/* ---- 亮度（lv_slider 竖向）---- */
#define BRIGHT_MIN     25      /* 不低于此，避免完全熄灭 */
#define BRIGHT_MAX     255
static int       s_bright = BRIGHT_MAX;  /* 当前亮度值 */
static lv_obj_t *s_bright_slider;

/* 二级菜单里 CYCLE（传输）选项的选中态刷新 */
static tile_t   *s_cycle_tile;
static lv_obj_t *s_cycle_rows[3];

/* ============================ 渲染 ============================ */
static void tile_render(tile_t *t)
{
    if (t->kind == TILE_TOGGLE && t->on) {
        lv_obj_set_style_bg_color(t->obj, t->on_color, 0);
        lv_obj_set_style_text_color(t->icon_lbl, COLOR_VOID, 0);
        lv_obj_set_style_text_opa(t->icon_lbl, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(t->obj, COLOR_STONE, 0);
        lv_obj_set_style_text_color(t->icon_lbl, COLOR_MIST, 0);
        lv_obj_set_style_text_opa(t->icon_lbl, LV_OPA_COVER, 0);
    }

    if (t->kind == TILE_TOGGLE && t->on)
        lv_obj_clear_flag(t->dot, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(t->dot, LV_OBJ_FLAG_HIDDEN);

    if (t->sub && t->kind == TILE_CYCLE)
        lv_label_set_text(t->sub, t->levels[t->level]);
}

/* ============================ 亮度 ============================ */
static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    s_bright = (int)val;
    bsp_set_brightness((uint8_t)s_bright);
}

/* ============================ 二级菜单（浮窗）============================ */
static void close_submenu(void)
{
    if (s_submenu) {
        lv_obj_delete(s_submenu);
        s_submenu = NULL;
    }
}

static void submenu_mask_event_cb(lv_event_t *e)
{
    /* 直接点在遮罩（空白）上才返回上级；点到卡片/子控件冒泡来时不关 */
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && lv_event_get_target(e) == s_submenu)
        close_submenu();
}

/* 创建浮窗：全屏半透明遮罩 + 居中卡片，返回卡片供填充。 */
static lv_obj_t *submenu_open(const char *title_icon, const char *title_text)
{
    close_submenu();
    s_bright_slider = NULL;  /* 非亮度菜单使用；亮度菜单内由 build 赋真实 slider */

    s_submenu = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_submenu);
    lv_obj_set_size(s_submenu, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_submenu, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(s_submenu, LV_OPA_70, 0);
    lv_obj_clear_flag(s_submenu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_submenu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_submenu, submenu_mask_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(s_submenu);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 280, 360);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COLOR_STONE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, RAD_CARD_L, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, SP_LG, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);   /* 接住卡片空白点击，避免冒泡关菜单 */
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, SP_MD, 0);

    /* 标题：图标 + 文字 */
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, SP_SM, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = lv_label_create(hdr);
    lv_label_set_text(ic, title_icon);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ic, COLOR_MIST, 0);
    lv_obj_t *tt = lv_label_create(hdr);
    lv_label_set_text(tt, title_text);
    lv_obj_set_style_text_font(tt, FONT_T4, 0);
    lv_obj_set_style_text_color(tt, COLOR_MIST, 0);

    return card;
}

/* ---- TOGGLE 类（WiFi/蓝牙/勿扰）：大开关 ---- */
static void toggle_submenu_cb(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
    t->on = !t->on;
    tile_render(t);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, t->on ? t->on_color : COLOR_VOID, 0);
    lv_obj_t *bl = lv_obj_get_child(btn, 0);
    lv_label_set_text(bl, t->on ? "ON" : "OFF");
    lv_obj_set_style_text_color(bl, t->on ? COLOR_VOID : COLOR_MIST, 0);
}

static void build_toggle_submenu(tile_t *t)
{
    lv_obj_t *card = submenu_open(t->icon, t->label);

    lv_obj_t *btn = lv_obj_create(card);
    lv_obj_set_size(btn, 200, 64);
    lv_obj_set_style_radius(btn, RAD_TILE, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, t->on ? t->on_color : COLOR_VOID, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, t->on ? "ON" : "OFF");
    lv_obj_set_style_text_font(bl, FONT_T4, 0);
    lv_obj_set_style_text_color(bl, t->on ? COLOR_VOID : COLOR_MIST, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, toggle_submenu_cb, LV_EVENT_CLICKED, t);

    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, t->on ? "Enabled" : "Disabled");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(desc, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(desc, LV_OPA_60, 0);
}

/* ---- CYCLE 类（传输）：档位列表 ---- */
static void cycle_submenu_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    int i = -1;
    for (int j = 0; j < 3; j++) if (s_cycle_rows[j] == row) { i = j; break; }
    if (i < 0) return;
    s_cycle_tile->level = i;
    tile_render(s_cycle_tile);
    for (int j = 0; j < 3; j++) {
        bool sel = (j == i);
        lv_obj_set_style_bg_color(s_cycle_rows[j], sel ? COLOR_MINT : COLOR_VOID, 0);
        lv_obj_t *rl = lv_obj_get_child(s_cycle_rows[j], 0);
        lv_obj_set_style_text_color(rl, sel ? COLOR_VOID : COLOR_MIST, 0);
    }
}

static void build_cycle_submenu(tile_t *t)
{
    lv_obj_t *card = submenu_open(t->icon, t->label);
    s_cycle_tile = t;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, 240, 48);
        lv_obj_set_style_radius(row, RAD_TILE, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, (i == t->level) ? COLOR_MINT : COLOR_VOID, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *rl = lv_label_create(row);
        lv_label_set_text(rl, t->levels[i]);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rl, (i == t->level) ? COLOR_VOID : COLOR_MIST, 0);
        lv_obj_center(rl);
        lv_obj_add_event_cb(row, cycle_submenu_cb, LV_EVENT_CLICKED, NULL);
        s_cycle_rows[i] = row;
    }
}

/* ---- ACTION 类（设置）：占位 ---- */
static void build_action_submenu(tile_t *t)
{
    lv_obj_t *card = submenu_open(t->icon, t->label);
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, "No options in this demo.");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(desc, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(desc, LV_OPA_60, 0);
}

/* ---- BRIGHTNESS：竖向亮度条（lv_slider）---- */
static void build_brightness_submenu(tile_t *t)
{
    (void)t;
    lv_obj_t *card = submenu_open(LV_SYMBOL_EYE_OPEN, "Brightness");

    lv_obj_t *zone = lv_obj_create(card);
    lv_obj_remove_style_all(zone);
    lv_obj_set_size(zone, 200, 280);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);

    /* 竖向滑块，原生支持拖拽/点击，不走手动 hit-test */
    s_bright_slider = lv_slider_create(zone);
    lv_obj_set_size(s_bright_slider, 40, 200);
    lv_obj_center(s_bright_slider);
    lv_bar_set_orientation(s_bright_slider, LV_BAR_ORIENTATION_VERTICAL);
    lv_slider_set_range(s_bright_slider, BRIGHT_MIN, BRIGHT_MAX);

    /* 背景轨（COLOR_VOID 内陷，MIST 细描边）*/
    lv_obj_set_style_bg_color(s_bright_slider, COLOR_VOID, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bright_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bright_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bright_slider, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bright_slider, COLOR_MIST, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_bright_slider, LV_OPA_40, LV_PART_MAIN);

    /* 指示条（MINT 填充）*/
    lv_obj_set_style_bg_color(s_bright_slider, COLOR_MINT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bright_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bright_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    /* 旋钮（MIST 小圆）*/
    lv_obj_set_style_bg_color(s_bright_slider, COLOR_MIST, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_bright_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(s_bright_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    /* min 提示（slider 正下方）*/
    lv_obj_t *minhint = lv_label_create(zone);
    lv_label_set_text(minhint, "min");
    lv_obj_set_style_text_font(minhint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(minhint, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(minhint, LV_OPA_40, 0);
    lv_obj_align_to(minhint, s_bright_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, SP_SM);

    lv_obj_add_event_cb(s_bright_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 设初始值（触发 VALUE_CHANGED → 同步 % 标签 + 实际面板亮度）*/
    lv_slider_set_value(s_bright_slider, s_bright, LV_ANIM_OFF);
}

static void open_tile_submenu(tile_t *t)
{
    switch (t->kind) {
    case TILE_TOGGLE:     build_toggle_submenu(t); break;
    case TILE_CYCLE:      build_cycle_submenu(t); break;
    case TILE_ACTION:     build_action_submenu(t); break;
    case TILE_BRIGHTNESS: build_brightness_submenu(t); break;
    }
}

bool ui_control_has_submenu(void)
{
    return s_submenu != NULL || ui_connect_visible();
}

/* ============================ 真实状态同步 ============================
 * WiFi tile 亮 = 已连上 AP；蓝牙 tile 亮 = bridge 已连上 BLE。 */
static void status_sync_cb(lv_timer_t *t)
{
    (void)t;
    bool wifi_on = wifi_prov_connected();
    bool ble_on  = bt_stack_connected();
    if (s_tiles[0].on != wifi_on) {
        s_tiles[0].on = wifi_on;
        tile_render(&s_tiles[0]);
    }
    if (s_tiles[1].on != ble_on) {
        s_tiles[1].on = ble_on;
        tile_render(&s_tiles[1]);
    }
}

/* WiFi / 蓝牙 tile：点按或长按都打开连接详情页 */
static bool is_connect_tile(const tile_t *t)
{
    return t == &s_tiles[0] || t == &s_tiles[1];
}

/* ============================ tile 事件 ============================ */
static void tile_event_cb(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_LONG_PRESSED) {
        t->lp = true;
        if (is_connect_tile(t)) {
            ui_connect_open(s_root);
        } else {
            open_tile_submenu(t);
        }
        return;
    }
    if (code == LV_EVENT_CLICKED) {
        if (t->lp) { t->lp = false; return; }   /* 长按已开菜单，忽略本次 CLICKED */
        if (is_connect_tile(t)) {
            ui_connect_open(s_root);
            return;
        }
        switch (t->kind) {
        case TILE_TOGGLE: t->on = !t->on; break;
        case TILE_CYCLE: {
            int n = 0; while (n < 3 && t->levels[n]) n++;
            t->level = (t->level + 1) % n;
            /* Refresh tile: 手动设置 LVGL 刷新率 */
            if (t->levels[0] && strcmp(t->levels[0], "Auto") == 0) {
                if (t->level == 0)      bsp_set_refr_period(33);  /* Auto → 30Hz 起步 */
                else if (t->level == 1) bsp_set_refr_period(33);  /* 30Hz */
                else                    bsp_set_refr_period(67);  /* 15Hz */
            }
            break;
        }
        case TILE_BRIGHTNESS: break;
        case TILE_ACTION: break;
        }
        tile_render(t);
    }
}

static void make_tile(lv_obj_t *grid, tile_t *t)
{
    t->obj = lv_obj_create(grid);
    lv_obj_set_size(t->obj, 108, 108);
    lv_obj_set_style_radius(t->obj, RAD_TILE, 0);
    lv_obj_set_style_border_width(t->obj, 0, 0);
    lv_obj_clear_flag(t->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t->obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t->obj, tile_event_cb, LV_EVENT_CLICKED, t);
    lv_obj_add_event_cb(t->obj, tile_event_cb, LV_EVENT_LONG_PRESSED, t);

    t->icon_lbl = lv_label_create(t->obj);
    lv_label_set_text(t->icon_lbl, t->icon);
    lv_obj_set_style_text_font(t->icon_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(t->icon_lbl, LV_ALIGN_TOP_LEFT, 2, 2);

    lv_obj_t *lbl = lv_label_create(t->obj);
    lv_label_set_text(lbl, t->label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, COLOR_MIST, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    if (t->kind == TILE_CYCLE) {
        t->sub = lv_label_create(t->obj);
        lv_obj_set_style_text_font(t->sub, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t->sub, COLOR_MINT, 0);
        lv_obj_align(t->sub, LV_ALIGN_LEFT_MID, 2, 6);
    }

    t->dot = lv_obj_create(t->obj);
    lv_obj_set_size(t->dot, 8, 8);
    lv_obj_set_style_radius(t->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(t->dot, COLOR_MINT, 0);
    lv_obj_set_style_border_width(t->dot, 0, 0);
    lv_obj_align(t->dot, LV_ALIGN_TOP_RIGHT, -2, 2);

    tile_render(t);
}

lv_obj_t *ui_control_create(lv_obj_t *parent)
{
    s_root = NULL;
    s_submenu = NULL;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s_root = root;

    /* 顶部拖拽手柄 + 标题 */
    lv_obj_t *handle = lv_obj_create(root);
    lv_obj_set_size(handle, 44, 5);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(handle, COLOR_MIST, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_40, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, EDGE_SAFE + 8);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Control Center");
    lv_obj_set_style_text_font(title, FONT_T4, 0);
    lv_obj_set_style_text_color(title, COLOR_MIST, 0);
    lv_obj_set_style_text_opa(title, LV_OPA_60, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, EDGE_SAFE + 20);

    /* 3x3 网格（7 tile），居中 */
    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 3 * 108 + 2 * SP_SM, 3 * 108 + 2 * SP_SM);
    lv_obj_center(grid);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, SP_SM, 0);
    lv_obj_set_style_pad_column(grid, SP_SM, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    /* 六个 tile：上排 WiFi/蓝牙/设置，下排 亮度/勿扰/传输 */
    s_tiles[0] = (tile_t){ .icon = LV_SYMBOL_WIFI,      .label = "WiFi",       .kind = TILE_TOGGLE, .on = true,  .on_color = COLOR_WORKBLUE };
    s_tiles[1] = (tile_t){ .icon = LV_SYMBOL_BLUETOOTH, .label = "Bluetooth",  .kind = TILE_TOGGLE, .on = true,  .on_color = COLOR_WORKBLUE };
    s_tiles[2] = (tile_t){ .icon = LV_SYMBOL_SETTINGS,  .label = "Settings",   .kind = TILE_ACTION };
    s_tiles[3] = (tile_t){ .icon = LV_SYMBOL_EYE_OPEN,  .label = "Brightness", .kind = TILE_BRIGHTNESS };
    s_tiles[4] = (tile_t){ .icon = LV_SYMBOL_MUTE,      .label = "Do Not Dist",.kind = TILE_TOGGLE, .on = false, .on_color = COLOR_AMBER };
    s_tiles[5] = (tile_t){ .icon = LV_SYMBOL_USB,       .label = "Transport",  .kind = TILE_CYCLE,  .level = 0, .levels = {"Serial","BLE","WiFi"} };
    s_tiles[6] = (tile_t){ .icon = LV_SYMBOL_REFRESH,   .label = "Refresh",    .kind = TILE_CYCLE,  .level = 0, .levels = {"Auto","30Hz","15Hz"} };

    for (int i = 0; i < 7; i++)
        make_tile(grid, &s_tiles[i]);

    /* WiFi/蓝牙 tile 状态接真实链路（1s 轮询）*/
    lv_timer_create(status_sync_cb, 1000, NULL);

    ui_hint(root, "Tap toggle  Long-press: menu  Swipe up: home");
    return root;
}
