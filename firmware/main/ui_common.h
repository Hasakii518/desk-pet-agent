/* ui_common.h — 屏幕尺寸、设计 Token、状态枚举、通用工具
 *
 * 设计系统见 docs/DESIGN-SYSTEM.md。所有颜色 / 字号 / 间距在此集中定义，
 * 各 UI 视图引用 Token，不硬编码。
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 画布 ============================ */
#define SCREEN_SIZE      466              /* 圆形屏直径 */
#define SCREEN_CENTER    (SCREEN_SIZE / 2)/* 圆心 233 */
#define SAFE_RADIUS      210              /* 安全内容半径 */
#define EDGE_SAFE        24               /* 边缘安全区 */

/* ============================ 色彩 Token ============================ */
/* 见 DESIGN-SYSTEM.md §2 */
#define COLOR_VOID        lv_color_hex(0x0A0A0C)  /* 纯黑背景 */
#define COLOR_STONE       lv_color_hex(0x1E1F25)  /* 卡片 / 未激活 tile */
#define COLOR_MIST        lv_color_hex(0xE8ECF2)  /* 主文字 / 图标 */
#define COLOR_WORKBLUE    lv_color_hex(0x58B4FC)  /* WorkBuddy 来源 / WiFi / 信息 */
#define COLOR_CLAUDEPURPLE lv_color_hex(0xB18CFF) /* Claude Code 来源 / 通知 */
#define COLOR_MINT        lv_color_hex(0x5EE9B0)  /* 成功 / 下一步 / 传输 */
#define COLOR_CORAL       lv_color_hex(0xF87171)  /* 错误 / 断连 / 录音 */
#define COLOR_AMBER       lv_color_hex(0xFBBF24)  /* 权限 / 警告 */

/* ============================ 间距 Token ============================ */
#define SP_XS   4
#define SP_SM   8
#define SP_MD   16
#define SP_LG   24
#define SP_XL   32

/* 圆角 */
#define RAD_CARD_S   14
#define RAD_CARD_L   16
#define RAD_TILE     12

/* ============================ 字体 Token ============================ */
/* UI 目前为英文标签（无 CJK 字库时避免方框，见 firmware/README.md）。
 * 圆形大屏（466）上字号整体放大：T1=48 大数字 / T2=32 标题 / T3=28 正文 / T4=24 角标 */
#define FONT_T1   (&lv_font_montserrat_48)
#define FONT_T2   (&lv_font_montserrat_32)
#define FONT_T3   (&lv_font_montserrat_28)
#define FONT_T4   (&lv_font_montserrat_24)

/* ============================ 桌宠状态机（12 态）============================ */
/* 优先级（高→低），见 docs/ARCHITECTURE.md §3：
 * disconnected > error > permission > notification > building > typing
 * > thinking > speaking > waiting > happy > idle > sleeping */
typedef enum {
    PET_DISCONNECTED = 0,
    PET_ERROR,
    PET_PERMISSION,
    PET_NOTIFICATION,
    PET_BUILDING,
    PET_TYPING,
    PET_THINKING,
    PET_SPEAKING,
    PET_WAITING,
    PET_HAPPY,
    PET_IDLE,
    PET_SLEEPING,
    PET_STATE_COUNT
} pet_state_t;

/* Agent 来源 */
typedef enum {
    SRC_WORKBUDDY = 0,
    SRC_CLAUDE_CODE,
} agent_source_t;

/* 四个视图 */
typedef enum {
    VIEW_PET = 0,     /* 主页桌宠 */
    VIEW_NEGATIVE,    /* 负一屏 */
    VIEW_SESSION,     /* 会话页 */
    VIEW_CONTROL,     /* 控制中心（下拉覆盖层）*/
} view_id_t;

/* ============================ 通用工具 ============================ */

/* 来源色 */
lv_color_t ui_source_color(agent_source_t src);

/* 状态对应的 Source Halo 颜色 */
lv_color_t ui_state_halo_color(pet_state_t st, agent_source_t src);

/* 状态英文短标签（用于调试 / 主页角标）*/
const char *ui_state_label(pet_state_t st);

/* 在 parent 上创建一张标准卡片（stone 填充，圆角，内边距 md）。
 * w<=0 时使用内容自适应宽度。返回卡片对象。 */
lv_obj_t *ui_card(lv_obj_t *parent, int w, int h);

/* 创建一个占位 slot 卡片（虚线框 + 标题 + “未启用”），见负一屏 §4 */
lv_obj_t *ui_slot_placeholder(lv_obj_t *parent, const char *slot_name);

/* 底部提示文字（T4，mist 40%），落在安全圆内 */
lv_obj_t *ui_hint(lv_obj_t *parent, const char *text);

/* 创建 Source Halo：屏幕外缘细弧环。返回 arc 对象，可用 ui_halo_apply 更新。 */
lv_obj_t *ui_halo_create(lv_obj_t *parent);

/* 依据状态 / 来源刷新 Halo 颜色与动画 */
void ui_halo_apply(lv_obj_t *halo, pet_state_t st, agent_source_t src);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMMON_H */
