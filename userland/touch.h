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
#define ZIZHAN_SENSOR_ACCEL    16   /* /dev/zizhan-sensors: finger, x, y = int32 mm/s2 on x, y, z */
#define ZIZHAN_SENSOR_LIGHT    17   /* /dev/zizhan-sensors: millilux, raw_milli, accuracy */
#define ZIZHAN_SENSOR_GYRO     18   /* /dev/zizhan-sensors: mrad/s on x, y, z */
#define ZIZHAN_SENSOR_MAG      19   /* /dev/zizhan-sensors: milli-uT on x, y, z */
#define ZIZHAN_SENSOR_PRESSURE  20  /* /dev/zizhan-sensors: Pascals, raw_milli, accuracy */
#define ZIZHAN_SENSOR_HINGE     21  /* /dev/zizhan-sensors: milli-deg, posture(0..2), accuracy */
#define ZIZHAN_SENSOR_PROXIMITY 22  /* /dev/zizhan-sensors: near(0/1), raw_adc, accuracy */
#define ZIZHAN_SENSOR_GNSS_SV   23  /* /dev/zizhan-sensors: eng|fix|vis|trk, cn0_tenths|used<<16, sys_mask */
#define ZIZHAN_SENSOR_GNSS_POS  24  /* /dev/zizhan-sensors: lat_e7, lon_e7, hor_unc_dm */
#define ZIZHAN_SENSOR_GNSS_ALT  25  /* /dev/zizhan-sensors: alt_dm, speed_cms, heading_ddeg */
struct pong_touch {uint64_t time;uint32_t type,finger,x,y;};
void pong_touch_start(void);
#endif
