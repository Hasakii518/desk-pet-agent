/* mock_data.c — 静态 mock 数据 */
#include "mock_data.h"

/* ---------------- 会话 1：Claude Code ---------------- */
static const mock_msg_t hist_a[] = {
    { true,  "User: refactor the login module" },
    { false, "Agent: generated test skeleton" },
    { false, "Agent: edited 3 files, running tests" },
    { true,  "User: also add rate limiting" },
    { false, "Agent: added middleware, 2 tests failing" },
};

/* ---------------- 会话 2：WorkBuddy ---------------- */
static const mock_msg_t hist_b[] = {
    { true,  "User: summarize today's standup" },
    { false, "Agent: 3 blockers found" },
    { false, "Agent: drafted summary, awaiting review" },
};

/* ---------------- 会话 3：Claude Code ---------------- */
static const mock_msg_t hist_c[] = {
    { true,  "User: fix the flaky CI job" },
    { false, "Agent: identified race in setup" },
    { false, "Agent: done, all green" },
};

static const mock_session_t s_sessions[] = {
    {
        .name = "auth-refactor",
        .source = SRC_CLAUDE_CODE,
        .status = PET_BUILDING,
        .time = "12:04",
        .last_reply = "Edited 3 files, running tests...",
        .next_step = "Run npm test",
        .history = hist_a,
        .history_len = sizeof(hist_a) / sizeof(hist_a[0]),
    },
    {
        .name = "daily-standup",
        .source = SRC_WORKBUDDY,
        .status = PET_WAITING,
        .time = "11:38",
        .last_reply = "Drafted summary, awaiting your review",
        .next_step = "Review draft",
        .history = hist_b,
        .history_len = sizeof(hist_b) / sizeof(hist_b[0]),
    },
    {
        .name = "ci-flaky-fix",
        .source = SRC_CLAUDE_CODE,
        .status = PET_HAPPY,
        .time = "10:15",
        .last_reply = "Done, all checks green",
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
    .session_name = "auth-refactor",
    .source = SRC_CLAUDE_CODE,
    .text = "Edited 3 files, running tests...",
};

const mock_session_t *mock_sessions(void)      { return s_sessions; }
int mock_session_count(void)                   { return sizeof(s_sessions) / sizeof(s_sessions[0]); }
const mock_hardware_t *mock_hardware(void)     { return &s_hw; }
const mock_notification_t *mock_current_notification(void) { return &s_notif; }
