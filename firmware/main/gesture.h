/* gesture.h — 触摸滑动分类
 *
 * 圆形屏是一块连续触摸屏，没有窗口拖拽，只有滑动方向。
 * 由触摸按下/抬起坐标算出主分量方向；「底边上滑回家」按起点 y 区分。
 */
#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_LEFT,
    GESTURE_RIGHT,
    GESTURE_UP,
    GESTURE_DOWN,
    GESTURE_HOME,   /* 底边上滑回家（起点距底 <=EDGE 且上滑）*/
} gesture_t;

/* 记录一次按下 */
void gesture_press(int16_t x, int16_t y);

/* 抬起 → 分类。传入抬起坐标，返回手势类型。 */
gesture_t gesture_release(int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif

#endif /* GESTURE_H */
