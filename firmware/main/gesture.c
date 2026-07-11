/* gesture.c — 滑动方向识别实现 */
#include "gesture.h"
#include "ui_common.h"

#define SWIPE_MIN   24   /* 主分量最小位移，低于此按 TAP */
#define BOTTOM_EDGE 24   /* 底边回家手势的起点区域高度 */

static int16_t s_x0, s_y0;

void gesture_press(int16_t x, int16_t y)
{
    s_x0 = x;
    s_y0 = y;
}

gesture_t gesture_release(int16_t x, int16_t y)
{
    int dx = x - s_x0;
    int dy = y - s_y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    /* 位移过小 → 点按 */
    if (adx < SWIPE_MIN && ady < SWIPE_MIN)
        return GESTURE_TAP;

    /* 竖直主分量 */
    if (ady >= adx) {
        /* 起点落在屏幕任一短边边缘 → 全局回家。
         * MADCTL 180° 后物理底边=LVGL y≈0、物理顶边=LVGL y≈466，两侧都判。 */
        if (s_y0 >= SCREEN_SIZE - BOTTOM_EDGE || s_y0 <= BOTTOM_EDGE)
            return GESTURE_HOME;
        return dy < 0 ? GESTURE_UP : GESTURE_DOWN;
    }

    /* 水平主分量 */
    return dx < 0 ? GESTURE_LEFT : GESTURE_RIGHT;
}
