/* clawd_assets.c — 原始字节数组 → lv_image_dsc_t 包装 + 状态查表
 *
 * clawd_image_data.h 里定义了 12 个 `static const uint8_t clawd_<state>_data[90000]`
 * （150x150 ARGB8888）。这里为每个数组建一个 lv_image_dsc_t，并按 pet_state_t 索引。
 */
#include "clawd_assets.h"
#include "clawd_image_data.h"

#define CLAWD_STRIDE (CLAWD_IMG_SIZE * 4)  /* ARGB8888: 4 字节/像素 */

#define CLAWD_DSC(sym)                                     \
    {                                                      \
        .header = {                                        \
            .magic = LV_IMAGE_HEADER_MAGIC,                \
            .cf = LV_COLOR_FORMAT_ARGB8888,                \
            .w = CLAWD_IMG_SIZE,                           \
            .h = CLAWD_IMG_SIZE,                           \
            .stride = CLAWD_STRIDE,                        \
        },                                                 \
        .data_size = sizeof(sym),                          \
        .data = sym,                                       \
    }

static const lv_image_dsc_t img_idle         = CLAWD_DSC(clawd_idle_data);
static const lv_image_dsc_t img_thinking     = CLAWD_DSC(clawd_thinking_data);
static const lv_image_dsc_t img_typing       = CLAWD_DSC(clawd_typing_data);
static const lv_image_dsc_t img_building     = CLAWD_DSC(clawd_building_data);
static const lv_image_dsc_t img_notification = CLAWD_DSC(clawd_notification_data);
static const lv_image_dsc_t img_waiting      = CLAWD_DSC(clawd_waiting_data);
static const lv_image_dsc_t img_permission   = CLAWD_DSC(clawd_permission_data);
static const lv_image_dsc_t img_speaking     = CLAWD_DSC(clawd_speaking_data);
static const lv_image_dsc_t img_happy        = CLAWD_DSC(clawd_happy_data);
static const lv_image_dsc_t img_error        = CLAWD_DSC(clawd_error_data);
static const lv_image_dsc_t img_sleeping     = CLAWD_DSC(clawd_sleeping_data);
static const lv_image_dsc_t img_disconnected = CLAWD_DSC(clawd_disconnected_data);

const lv_image_dsc_t *clawd_image_for(pet_state_t st)
{
    switch (st) {
    case PET_IDLE:         return &img_idle;
    case PET_THINKING:     return &img_thinking;
    case PET_TYPING:       return &img_typing;
    case PET_BUILDING:     return &img_building;
    case PET_NOTIFICATION: return &img_notification;
    case PET_WAITING:      return &img_waiting;
    case PET_PERMISSION:   return &img_permission;
    case PET_SPEAKING:     return &img_speaking;
    case PET_HAPPY:        return &img_happy;
    case PET_ERROR:        return &img_error;
    case PET_SLEEPING:     return &img_sleeping;
    case PET_DISCONNECTED: return &img_disconnected;
    default:               return &img_idle;
    }
}
