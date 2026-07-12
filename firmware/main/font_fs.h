/* font_fs.h — 最小 LVGL 文件系统驱动：将 C 数组注册为一个 LVGL 驱动器下的文件。
 *
 * 用法：
 *   font_fs_init();                          // 注册 "M:" 驱动器
 *   font_fs_add("emoji.ttf", data, len);     // 映射 M:emoji.ttf → 数组
 *   lv_freetype_font_create("M:emoji.ttf", …); // FreeType 从内存读取
 */
#ifndef FONT_FS_H
#define FONT_FS_H

#include "lvgl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 LVGL 驱动器 "M:"。必须在 LVGL 初始化之后调用一次。 */
void font_fs_init(void);

/* 将一个 C 数组映射为驱动器下的文件名。返回 true 表示成功。 */
bool font_fs_add(const char *name, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FONT_FS_H */
