/* pet_states.c — 表情状态机实现 */
#include "pet_states.h"

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
