/* clawd_assets.c — 桌宠素材查表（GIF + 静态回退）
 *
 * 11 个状态用 lv_gif widget 播放 GIF 动画，素材在 gif_image_data.h；
 * disconnected 无对应 GIF，保留静态 ARGB8888，素材在 clawd_image_data.h。
 */
#include "clawd_assets.h"
#include "gif_image_data.h"      /* 提供 clawd_gif_<state> 描述符 */
#include "clawd_disconnected.h"    /* 仅 clawd_disconnected_data，不拉其余 11 张静态图 */

#define CLAWD_STRIDE (CLAWD_IMG_SIZE * 4)

// ---- disconnected 静态图描述符（ARGB8888）----
static const lv_image_dsc_t img_disconnected = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_ARGB8888,
        .w      = CLAWD_IMG_SIZE,
        .h      = CLAWD_IMG_SIZE,
        .stride = CLAWD_STRIDE,
    },
    .data_size = sizeof(clawd_disconnected_data),
    .data      = clawd_disconnected_data,
};

const lv_image_dsc_t *clawd_asset_for(pet_state_t st)
{
    switch (st) {
    case PET_IDLE:         return &clawd_gif_idle;
    case PET_THINKING:     return &clawd_gif_thinking;
    case PET_TYPING:       return &clawd_gif_typing;
    case PET_BUILDING:     return &clawd_gif_building;
    case PET_NOTIFICATION: return &clawd_gif_notification;
    case PET_WAITING:      return &clawd_gif_waiting;
    case PET_PERMISSION:   return &clawd_gif_permission;
    case PET_SPEAKING:     return &clawd_gif_speaking;
    case PET_HAPPY:        return &clawd_gif_happy;
    case PET_ERROR:        return &clawd_gif_error;
    case PET_SLEEPING:     return &clawd_gif_sleeping;
    case PET_DISCONNECTED: return &img_disconnected;
    default:               return &clawd_gif_idle;
    }
}

bool clawd_is_gif(pet_state_t st)
{
    return st != PET_DISCONNECTED;
}
