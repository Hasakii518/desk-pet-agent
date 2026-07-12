/* app_main.c — 入口 + BSP 初始化 + 导航装配 + 手势/按键分发
 *
 * Route A（跟手翻页）：
 *   横向翻页交给 ui_nav 的原生 snap pager（拖拽 + 惯性 + 吸附），
 *   页序列 [会话… , 主页, 负一屏]；本文件的手势分发只处理「非横向」动作：
 *     - TAP（停在主页时）        → 戳一下桌宠
 *     - DOWN（停在主页时）       → 拉出控制中心覆盖层
 *     - UP / 控制中心可见时      → 收起控制中心
 *     - HOME（底边上滑）         → 滚回主页 / 先收控制中心
 *   横向 LEFT/RIGHT 不在这里处理——由 pager 用原生滚动完成。
 *
 * BOOT(GPIO0) 键：按一下息屏、再按一下亮屏，循环切换。
 * 无任何通信，全部走 mock。
 */
#include "esp_log.h"
#include "driver/gpio.h"

#include "bsp.h"
#include "ui_common.h"
#include "gesture.h"
#include "pet_states.h"
#include "ui_pet.h"
#include "ui_nav.h"
#include "ui_control.h"
#include "serial_protocol.h"
#include "session_store.h"

static const char *TAG = "app";

#define BOOT_GPIO  GPIO_NUM_0

/* 处理一次已分类的手势（横向翻页除外，那由 pager 负责）*/
static void dispatch(gesture_t g)
{
    /* 二级菜单（浮窗）打开时屏蔽全局手势，避免拖滑块 / 点选项被误判成滑动导航 */
    if (ui_control_has_submenu())
        return;

    /* 控制中心可见时：下滑或回家手势收起它（MADCTL 180° 旋转后，LVGL DOWN=物理上滑） */
    if (ui_nav_control_visible()) {
        if (g == GESTURE_DOWN || g == GESTURE_HOME)
            ui_nav_control_hide();
        return;
    }

    switch (g) {
    case GESTURE_HOME:                 /* 底边上滑：全局回主页（已在家则跳过）*/
        if (!ui_nav_at_home())
            ui_nav_go_home();
        break;
    case GESTURE_UP:                   /* 仅主页下滑拉出控制中心（旋转后 LVGL UP = 物理下滑） */
        if (ui_nav_at_home())
            ui_nav_control_show();
        break;
    case GESTURE_TAP:                  /* 仅主页戳桌宠 */
        if (ui_nav_at_home())
            ui_pet_poke();
        break;
    default:
        /* LEFT/RIGHT/DOWN：横向/纵向交给 pager 和页内滚动 */
        break;
    }
}

/* -------- 手势轮询：读取 LVGL indev 缓存的触控状态，不再直读 I2C --------
 * CST816 的坐标由 LVGL indev（INT 驱动 + EVENT 模式）读取。本定时器只
 * 从 indev 缓存取最新坐标做手势分类，消除冗余的 I2C 读取。
 *
 * 注意：LVGL indev 的 get_coordinates 已应用 tp_cfg.mirror_y=1（y = y_max - y），
 * 坐标与之前手动镜像一致，无需额外翻转。 */
static bool              s_was_pressed;
static int16_t           s_last_x, s_last_y;
static int               s_last_raw_gesture;   /* 防 CST820 手势寄存器读后不清零 */

void app_reset_idle(void);
static void dyn_fps_cb(lv_timer_t *t);

static void gesture_poll_cb(lv_timer_t *t)
{
    (void)t;
    lv_indev_t *indev = bsp_touch_indev();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_indev_state_t state = lv_indev_get_state(indev);

    if (state == LV_INDEV_STATE_PRESSED) {
        app_reset_idle();
        if (!s_was_pressed) {
            s_was_pressed = true;
            gesture_press(pt.x, pt.y);
        }
        s_last_x = pt.x;
        s_last_y = pt.y;
    } else if (s_was_pressed) {   /* 刚抬起 */
        s_was_pressed = false;
        gesture_t g = gesture_release(s_last_x, s_last_y);
        if (g != GESTURE_NONE) dispatch(g);
    }

    /* 极快滑时 CST816S 可能不报坐标点，直接把方向写入手势寄存器 0x01。
     * 在没有正在跟踪的触摸时，读手势码兜底。
     * CST820 寄存器读后不一定清零，用 last_raw 过滤同一手势码的重复上报。 */
    if (!s_was_pressed) {
        int raw = bsp_touch_read_gesture();
        if (raw > 0 && raw != s_last_raw_gesture) app_reset_idle();
        s_last_raw_gesture = raw;
        gesture_t g = GESTURE_NONE;
        switch (raw) {
        case 0x01: g = GESTURE_UP;    break;   /* IC 上报「上滑」*/
        case 0x02: g = GESTURE_DOWN;  break;   /* IC 上报「下滑」*/
        case 0x03: g = GESTURE_LEFT;  break;
        case 0x04: g = GESTURE_RIGHT; break;
        case 0x05: g = GESTURE_TAP;   break;
        default: break;
        }
        if (g != GESTURE_NONE) dispatch(g);
    }
}

/* -------- 动态刷新率：活跃→30Hz(33ms)，空闲→15Hz(67ms) -------- */
static void dyn_fps_cb(lv_timer_t *t)
{
    (void)t;
    pet_state_t st = pet_state_get();
    bool has_notif = session_store_has_notification();
    bool active = (st != PET_IDLE && st != PET_SLEEPING && st != PET_DISCONNECTED)
                  || has_notif;
    bsp_set_refr_period(active ? 33 : 67);
}

/* -------- 自动休眠：60s 无触摸 / 无有效事件则息屏 --------
 *
 * 唤醒规则：
 *   触摸           → 唤醒 + reset_idle()
 *   bridge 状态事件 → 唤醒 + reset_idle()（新消息/Agent 状态变化/通知/会话更新）
 *   bridge 心跳     → 不唤醒（心跳只是链路保活，用户无感知）
 *   BOOT 手动关屏  → 触摸/事件均不唤醒，必须再按 BOOT */
static bool  s_display_sleep;       /* 当前是否因自动休眠而熄屏 */
static bool  s_boot_off;            /* BOOT 键手动熄屏（不自动唤醒）*/
static int   s_idle_sec;            /* 累计空闲秒数 */

#define SLEEP_TIMEOUT_SEC  60

void app_reset_idle(void)
{
    s_idle_sec = 0;
    /* 自动休眠熄屏后，任意触摸唤醒 */
    if (s_display_sleep) {
        s_display_sleep = false;
        if (!s_boot_off) bsp_display_set_on(true);
    }
    /* 立即将刷新率切到活跃档（避免等待下个 2s 定时器触发）*/
    dyn_fps_cb(NULL);
}

static void sleep_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_idle_sec++;
    if (s_idle_sec >= SLEEP_TIMEOUT_SEC && !s_display_sleep && !s_boot_off) {
        s_display_sleep = true;
        bsp_display_set_on(false);
        ESP_LOGI(TAG, "Auto sleep after %ds idle", SLEEP_TIMEOUT_SEC);
    }
}

/* -------- BOOT 键轮询：下降沿切换息屏/亮屏 -------- */
static void boot_key_poll_cb(lv_timer_t *t)
{
    (void)t;
    static bool prev_high = true;
    bool level = gpio_get_level(BOOT_GPIO);

    if (prev_high && !level) {
        s_boot_off = !s_boot_off;
        s_display_sleep = false;
        bsp_display_set_on(!s_boot_off);
        app_reset_idle();
        ESP_LOGI(TAG, "BOOT -> display %s", s_boot_off ? "OFF" : "ON");
    }
    prev_high = level;
}

static void boot_key_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void build_ui(void)
{
    if (!lv_display_get_default()) {
        ESP_LOGE(TAG, "No display — skipping UI build");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_VOID, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_nav_create(scr);

    /* 手势轮询 10ms：读 LVGL indev 缓存（无 I2C 直读）*/
    lv_timer_create(gesture_poll_cb, 10, NULL);
    /* 动态刷新率：2s 检查 pet 状态，活跃 30Hz / 空闲 15Hz */
    lv_timer_create(dyn_fps_cb, 2000, NULL);
    /* 休眠计时：每秒累加，60s 无操作自动息屏 */
    lv_timer_create(sleep_tick_cb, 1000, NULL);
    /* BOOT 键轮询 40ms（去抖足够）*/
    lv_timer_create(boot_key_poll_cb, 40, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "DeskPet Agent firmware demo starting");
    pet_state_set(PET_IDLE, SRC_CLAUDE_CODE);

    bsp_init();
    boot_key_init();

    if (bsp_lvgl_lock(-1)) {
        serial_protocol_init();
        /* session_store_seed_mock(); -- 已关闭 mock 初始数据 */
        build_ui();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "UI ready");
}
