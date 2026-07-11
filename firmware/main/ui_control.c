/* ui_control.c — ④ 控制中心（下拉覆盖层）
 *
 * 3 列 × 2 行 tile：WiFi / 蓝牙 / 设置 / 亮度 / 勿扰 / 传输。
 * 点击 tile 切换本地状态（demo 无实际硬件动作）。
 */
#include "ui_control.h"
#include "ui_common.h"

/* 每个 tile 的状态类型 */
typedef enum { TILE_TOGGLE, TILE_ACTION, TILE_CYCLE } tile_kind_t;

typedef struct {
    const char *icon;
    const char *label;
    tile_kind_t kind;
    bool        on;        /* TOGGLE 用 */
    lv_color_t  on_color;  /* 开启态填充色 */
    int         level;     /* CYCLE 用（亮度/传输档位）*/
    const char *levels[3]; /* CYCLE 档位名 */
    lv_obj_t   *obj;
    lv_obj_t   *icon_lbl;
    lv_obj_t   *dot;       /* 右上角指示点 */
    lv_obj_t   *sub;       /* 副标题（SSID/档位）*/
} tile_t;

static tile_t s_tiles[6];

static void tile_render(tile_t *t)
{
    bool active = (t->kind == TILE_TOGGLE && t->on) ||
                  (t->kind == TILE_CYCLE);  /* cycle 始终视为激活着色到副标题 */

    if (t->kind == TILE_TOGGLE && t->on) {
        lv_obj_set_style_bg_color(t->obj, t->on_color, 0);
        lv_obj_set_style_text_color(t->icon_lbl, COLOR_VOID, 0);
    } else {
        lv_obj_set_style_bg_color(t->obj, COLOR_STONE, 0);
        lv_obj_set_style_text_color(t->icon_lbl, COLOR_MIST, 0);
        if (t->kind == TILE_TOGGLE)
            lv_obj_set_style_text_opa(t->icon_lbl, LV_OPA_40, 0);
        else
            lv_obj_set_style_text_opa(t->icon_lbl, LV_OPA_COVER, 0);
    }

    /* 指示点：仅 TOGGLE 开启态显示 */
    if (t->kind == TILE_TOGGLE && t->on)
        lv_obj_clear_flag(t->dot, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(t->dot, LV_OBJ_FLAG_HIDDEN);

    /* 副标题 */
    if (t->sub) {
        if (t->kind == TILE_CYCLE)
            lv_label_set_text(t->sub, t->levels[t->level]);
    }
    (void)active;
}

static void tile_event_cb(lv_event_t *e)
{
    tile_t *t = (tile_t *)lv_event_get_user_data(e);
    switch (t->kind) {
    case TILE_TOGGLE: t->on = !t->on; break;
    case TILE_CYCLE:  t->level = (t->level + 1) % 3; break;
    case TILE_ACTION: break;  /* demo：设置屏留空 */
    }
    tile_render(t);
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

    t->icon_lbl = lv_label_create(t->obj);
    lv_label_set_text(t->icon_lbl, t->icon);
    /* tile 内字体用本地较小字号（全局 T 字号已整体放大，这里回滚以适配紧凑 tile）*/
    lv_obj_set_style_text_font(t->icon_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(t->icon_lbl, LV_ALIGN_TOP_LEFT, 2, 2);

    lv_obj_t *lbl = lv_label_create(t->obj);
    lv_label_set_text(lbl, t->label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, COLOR_MIST, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    /* CYCLE 类型的副标题（档位）*/
    if (t->kind == TILE_CYCLE) {
        t->sub = lv_label_create(t->obj);
        lv_obj_set_style_text_font(t->sub, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t->sub, COLOR_MINT, 0);
        lv_obj_align(t->sub, LV_ALIGN_LEFT_MID, 2, 6);
    }

    /* 右上角指示点 */
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
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(root, COLOR_VOID, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

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

    /* 3x2 网格，居中 */
    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 3 * 108 + 2 * SP_SM, 2 * 108 + SP_SM);
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
    s_tiles[3] = (tile_t){ .icon = LV_SYMBOL_EYE_OPEN,  .label = "Brightness", .kind = TILE_CYCLE,  .level = 1, .levels = {"Low","Mid","High"} };
    s_tiles[4] = (tile_t){ .icon = LV_SYMBOL_MUTE,      .label = "Do Not Dist",.kind = TILE_TOGGLE, .on = false, .on_color = COLOR_AMBER };
    s_tiles[5] = (tile_t){ .icon = LV_SYMBOL_USB,       .label = "Transport",  .kind = TILE_CYCLE,  .level = 0, .levels = {"Serial","BLE","WiFi"} };

    for (int i = 0; i < 6; i++)
        make_tile(grid, &s_tiles[i]);

    ui_hint(root, "Tap toggle  Swipe up: home");
    return root;
}
