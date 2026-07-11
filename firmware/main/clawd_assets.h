/* clawd_assets.h — 把 assets/clawd/c/clawd_image_data.h 里的原始 ARGB8888
 * 字节数组包装成 lv_image_dsc_t，并提供「状态 → 图片」查表。
 */
#ifndef CLAWD_ASSETS_H
#define CLAWD_ASSETS_H

#include "lvgl.h"
#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clawd 素材边长（像素），与 clawd_image_data.h 的 CLAWD_IMG_SIZE 一致 */
#define CLAWD_ASSET_SIZE 150

/* 返回某个桌宠状态对应的 Clawd 图片描述符（150x150 ARGB8888）。
 * 素材缺失时回退到 idle。 */
const lv_image_dsc_t *clawd_image_for(pet_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* CLAWD_ASSETS_H */
