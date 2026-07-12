/* cjk_font.h — CJK 字体的 xxd 生成数据
 *
 * 用法（需先准备一份 CJK TTF，如 Noto Sans SC / 思源黑体）：
 *   xxd -i your_cjk.ttf > assets/font/cjk_font.c
 *   # 然后编辑生成的 .c 第一行把变量名改成 cjk_ttf_data / cjk_ttf_len
 *
 * 未替换前本项目用下方空实现编译通过，FreeType 加载失败但不会宕。
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char cjk_ttf_data[];
extern const size_t  cjk_ttf_len;

#ifdef __cplusplus
}
#endif
