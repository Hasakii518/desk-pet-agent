/* mock_data.c — 静态 mock 数据（含中文，验证 CJK 字体渲染） */
#include "mock_data.h"

/* ---------------- 会话 1：Claude Code ---------------- */
static const mock_msg_t hist_a[] = {
    { true,  "用户: 帮我重构登录模块" },
    { false, "Agent: 已生成测试骨架" },
    { false, "Agent: 修改了 3 个文件，正在跑测试" },
    { true,  "用户: 再加个限流逻辑" },
    { false, "Agent: 中间件已添加，2 个测试失败" },
};

/* ---------------- 会话 2：WorkBuddy ---------------- */
static const mock_msg_t hist_b[] = {
    { true,  "用户: 总结今天的站会" },
    { false, "Agent: 发现 3 个阻塞项" },
    { false, "Agent: 已生成摘要，等待审核" },
};

/* ---------------- 会话 3：Claude Code ---------------- */
static const mock_msg_t hist_c[] = {
    { true,  "用户: 修复 CI 不稳定问题" },
    { false, "Agent: 定位到 setup 中的竞态条件" },
    { false, "Agent: 修复完成，全部通过 ✓" },
};

static const mock_session_t s_sessions[] = {
    {
        .name = "登录模块重构",
        .source = SRC_CLAUDE_CODE,
        .status = PET_BUILDING,
        .time = "12:04",
        .last_reply = "修改了 3 个文件，正在跑测试…",
        .next_step = "运行 npm test",
        .history = hist_a,
        .history_len = sizeof(hist_a) / sizeof(hist_a[0]),
    },
    {
        .name = "每日站会总结",
        .source = SRC_WORKBUDDY,
        .status = PET_WAITING,
        .time = "11:38",
        .last_reply = "已生成会议摘要，等待你确认",
        .next_step = "审核草稿",
        .history = hist_b,
        .history_len = sizeof(hist_b) / sizeof(hist_b[0]),
    },
    {
        .name = "CI 稳定性修复",
        .source = SRC_CLAUDE_CODE,
        .status = PET_HAPPY,
        .time = "10:15",
        .last_reply = "修复完成，所有检查通过",
        .next_step = NULL,  /* 等用户输入 */
        .history = hist_c,
        .history_len = sizeof(hist_c) / sizeof(hist_c[0]),
    },
};

static const mock_hardware_t s_hw = {
    .device = "ESP32-S3 AMOLED",
    .battery_pct = 82,
    .charging = true,
    .wifi = "Office-5G",
    .cpu_pct = 23,
    .mem = "6.1G",
    .laptop_battery_pct = 100,
};

static const mock_notification_t s_notif = {
    .session_name = "登录模块重构",
    .source = SRC_CLAUDE_CODE,
    .text = "修改了 3 个文件，正在跑测试…",
};

const mock_session_t *mock_sessions(void)      { return s_sessions; }
int mock_session_count(void)                   { return sizeof(s_sessions) / sizeof(s_sessions[0]); }
const mock_hardware_t *mock_hardware(void)     { return &s_hw; }
const mock_notification_t *mock_current_notification(void) { return &s_notif; }
