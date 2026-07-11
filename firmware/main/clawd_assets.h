/* clawd_assets.h — 桌宠素材查表（GIF 优先，disconnected 回退静态图）
 *
 * 11 个状态使用 LVGL 内置 lv_gif widget 播放 GIF（原始字节在 gif_image_data.h），
 * disconnected 因无对应 GIF，保留静态 ARGB8888（clawd_image_data.h）。
 */
#ifndef CLAWD_ASSETS_H
#define CLAWD_ASSETS_H

#include "lvgl.h"
#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clawd 素材边长（像素），与素材文件一致 */
#define CLAWD_ASSET_SIZE 150

/* 返回某个桌宠状态对应的图片描述符。
 * 大多数状态返回的是指向 GIF 原始字节的 lv_image_dsc_t（供 lv_gif_set_src 使用），
 * disconnected 返回 ARGB8888 静态图描述符（供 lv_image_set_src 使用）。 */
const lv_image_dsc_t *clawd_asset_for(pet_state_t st);

/* 该状态是否使用 GIF 动画（true = 用 lv_gif widget，false = 用 lv_image widget）*/
bool clawd_is_gif(pet_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* CLAWD_ASSETS_H */
