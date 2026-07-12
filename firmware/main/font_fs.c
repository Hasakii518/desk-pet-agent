/* font_fs.c — LVGL 内存文件系统驱动实现 */
#include "font_fs.h"
#include <string.h>

#define MAX_FILES 8

typedef struct {
    char           name[64];
    const uint8_t *data;
    size_t         len;
} mem_file_t;

static mem_file_t s_files[MAX_FILES];
static int        s_count;
static uint32_t   s_offsets[MAX_FILES]; /* 每个文件的读偏移：seek / read / tell 共享 */

/* ---- LVGL 文件系统回调 ---- */

static void *mem_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    if (mode != LV_FS_MODE_RD) return NULL;

    for (int i = 0; i < s_count; i++) {
        if (strcmp(path, s_files[i].name) == 0) {
            s_offsets[i] = 0;  /* 每次打开重置偏移 */
            return (void *)(intptr_t)i;
        }
    }
    return NULL;
}

static lv_fs_res_t mem_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    (void)file_p;
    return LV_FS_RES_OK;
}

static lv_fs_res_t mem_read(lv_fs_drv_t *drv, void *file_p,
                            void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    int idx = (int)(intptr_t)file_p;
    if (idx < 0 || idx >= s_count) return LV_FS_RES_UNKNOWN;

    uint32_t off = s_offsets[idx];
    uint32_t remain = (uint32_t)s_files[idx].len - off;
    uint32_t to_read = btr < remain ? btr : remain;
    memcpy(buf, s_files[idx].data + off, to_read);
    s_offsets[idx] += to_read;
    *br = to_read;
    return to_read > 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t mem_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos,
                             lv_fs_whence_t whence)
{
    (void)drv;
    int idx = (int)(intptr_t)file_p;
    if (idx < 0 || idx >= s_count) return LV_FS_RES_UNKNOWN;

    uint32_t base;
    switch (whence) {
    case LV_FS_SEEK_SET: base = 0; break;
    case LV_FS_SEEK_CUR: base = s_offsets[idx]; break;
    case LV_FS_SEEK_END: base = (uint32_t)s_files[idx].len; break;
    default: return LV_FS_RES_UNKNOWN;
    }
    s_offsets[idx] = base + pos;
    return LV_FS_RES_OK;
}

static lv_fs_res_t mem_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
    int idx = (int)(intptr_t)file_p;
    if (idx < 0 || idx >= s_count) return LV_FS_RES_UNKNOWN;
    *pos_p = s_offsets[idx];
    return LV_FS_RES_OK;
}

/* ---- 公开 API ---- */

void font_fs_init(void)
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter   = 'M';
    drv.cache_size = 0;
    drv.open_cb  = mem_open;
    drv.close_cb = mem_close;
    drv.read_cb  = mem_read;
    drv.seek_cb  = mem_seek;
    drv.tell_cb  = mem_tell;
    lv_fs_drv_register(&drv);
}

bool font_fs_add(const char *name, const uint8_t *data, size_t len)
{
    if (s_count >= MAX_FILES) return false;
    strncpy(s_files[s_count].name, name, sizeof(s_files[0].name) - 1);
    s_files[s_count].name[sizeof(s_files[0].name) - 1] = '\0';
    s_files[s_count].data = data;
    s_files[s_count].len  = len;
    s_count++;
    return true;
}
