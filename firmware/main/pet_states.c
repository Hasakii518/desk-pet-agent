/* pet_states.c — 表情状态机实现 */
#include "pet_states.h"
#include "esp_log.h"
#include <string.h>

static pet_state_t    s_state  = PET_IDLE;
static agent_source_t s_source = SRC_CLAUDE_CODE;
static pet_state_t    s_before_disconnect = PET_IDLE;

/* demo 循环顺序：按「叙事」而非优先级排列，方便逐一展示每个表情 */
static const pet_state_t s_cycle[] = {
    PET_IDLE, PET_THINKING, PET_TYPING, PET_BUILDING,
    PET_NOTIFICATION, PET_WAITING, PET_PERMISSION, PET_SPEAKING,
    PET_HAPPY, PET_ERROR, PET_SLEEPING, PET_DISCONNECTED,
};
#define CYCLE_COUNT (sizeof(s_cycle) / sizeof(s_cycle[0]))

pet_state_t    pet_state_get(void)  { return s_state; }
agent_source_t pet_source_get(void) { return s_source; }

void pet_state_set(pet_state_t st, agent_source_t src)
{
    ESP_LOGI("pet", "state %d → %d  src=%d", (int)s_state, (int)st, (int)src);
    s_state  = st;
    s_source = src;
}

pet_state_t pet_state_cycle(void)
{
    int idx = 0;
    for (int i = 0; i < (int)CYCLE_COUNT; i++) {
        if (s_cycle[i] == s_state) { idx = i; break; }
    }
    idx = (idx + 1) % CYCLE_COUNT;
    s_state = s_cycle[idx];

    /* demo：每切一圈交替一次来源色，展示 workblue / claudepurple 两套 Halo */
    if (idx == 0)
        s_source = (s_source == SRC_CLAUDE_CODE) ? SRC_WORKBUDDY : SRC_CLAUDE_CODE;

    return s_state;
}

void pet_state_set_disconnected(bool disconnected)
{
    if (disconnected) {
        if (s_state != PET_DISCONNECTED) s_before_disconnect = s_state;
        s_state = PET_DISCONNECTED;
    } else if (s_state == PET_DISCONNECTED) {
        s_state = s_before_disconnect;
    }
}

pet_state_t pet_state_from_string(const char *str)
{
    if (!str) return PET_IDLE;

    if (strcmp(str, "error")        == 0) return PET_ERROR;
    if (strcmp(str, "permission")   == 0) return PET_PERMISSION;
    if (strcmp(str, "notification") == 0) return PET_NOTIFICATION;
    if (strcmp(str, "building")     == 0) return PET_BUILDING;
    if (strcmp(str, "typing")       == 0) return PET_TYPING;
    if (strcmp(str, "thinking")     == 0) return PET_THINKING;
    if (strcmp(str, "speaking")     == 0) return PET_SPEAKING;
    if (strcmp(str, "waiting")      == 0) return PET_WAITING;
    if (strcmp(str, "happy")        == 0) return PET_HAPPY;
    if (strcmp(str, "idle")         == 0) return PET_IDLE;
    if (strcmp(str, "sleeping")     == 0) return PET_SLEEPING;
    /* "disconnected" is never sent from bridge — it's local-only */
    if (strcmp(str, "disconnected") == 0) return PET_DISCONNECTED;

    return PET_IDLE;
}

const char *pet_state_to_string(pet_state_t st)
{
    switch (st) {
    case PET_ERROR:         return "error";
    case PET_PERMISSION:    return "permission";
    case PET_NOTIFICATION:  return "notification";
    case PET_BUILDING:      return "building";
    case PET_TYPING:        return "typing";
    case PET_THINKING:      return "thinking";
    case PET_SPEAKING:      return "speaking";
    case PET_WAITING:       return "waiting";
    case PET_HAPPY:         return "happy";
    case PET_IDLE:          return "idle";
    case PET_SLEEPING:      return "sleeping";
    case PET_DISCONNECTED:  return "disconnected";
    default:                return "idle";
    }
}

agent_source_t pet_source_from_string(const char *str)
{
    if (str && strcmp(str, "workbuddy") == 0) return SRC_WORKBUDDY;
    /* default / "claude-code" / unknown */
    return SRC_CLAUDE_CODE;
}
