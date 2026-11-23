#ifndef PONG_TOUCH_H
#define PONG_TOUCH_H
#include <stdint.h>
#define PONG_TOUCH_DOWN 1
#define PONG_TOUCH_MOVE 2
#define PONG_TOUCH_UP 3
#define PONG_BUTTON_DOWN 4
#define PONG_BUTTON_UP 5
#define PONG_BUTTON_POWER 0
#define PONG_BUTTON_VOLUME_UP 1
#define PONG_BUTTON_VOLUME_DOWN 2
#define ZIZHAN_SENSOR_ACCEL 16      /* /dev/zizhan-sensors: finger, x, y = int32 mm/s2 on x, y, z */
struct pong_touch {uint64_t time;uint32_t type,finger,x,y;};
void pong_touch_start(void);
#endif
