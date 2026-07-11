/* pet_states.h — 桌宠表情状态机（设备端精简移植）
 *
 * 完整优先级逻辑在 bridge/core/state-machine.ts；设备端只保留渲染所需子集，
 * 并提供 demo 用的「点击循环切换状态」。disconnected 为设备级强制态。
 */
#ifndef PET_STATES_H
#define PET_STATES_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 当前状态 / 来源 */
pet_state_t    pet_state_get(void);
agent_source_t pet_source_get(void);

void pet_state_set(pet_state_t st, agent_source_t src);

/* demo：循环到下一个状态（点击桌宠时用），返回新状态 */
pet_state_t pet_state_cycle(void);

/* 设备级：链路断开 / 恢复。断开时强制 disconnected 并记住上一态。 */
void pet_state_set_disconnected(bool disconnected);

#ifdef __cplusplus
}
#endif

#endif /* PET_STATES_H */
