/* noto_emoji.h — Noto Emoji Regular TTF embedded as C array */
#ifndef NOTO_EMOJI_H
#define NOTO_EMOJI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char emoji_ttf_data[];
extern const size_t  emoji_ttf_len;

#ifdef __cplusplus
}
#endif

#endif
