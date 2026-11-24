#include "ZizhanVolumeInjectPlan.h"
#include <stdio.h>

static int failures;

#define CHECK(cond, name) do { \
 if (cond) { printf("PASS %s\n", name); } \
 else { printf("FAIL %s\n", name); ++failures; } \
} while (0)

int main(void) {
 CHECK(zizhan_vol_inject_mode(0) == ZizhanVolInjectOff, "default off");
 CHECK(zizhan_vol_inject_mode(1) == ZizhanVolInjectUpDown, "mode 1 up+down");
 CHECK(zizhan_vol_inject_mode(2) == ZizhanVolInjectUpOnly, "mode 2 up-only");
 CHECK(zizhan_vol_inject_mode(3) == ZizhanVolInjectSkipMode, "mode 3 skipped");
 CHECK(zizhan_vol_inject_mode(0xffffffffu) == ZizhanVolInjectSkipMode, "max uint skipped");

 CHECK(zizhan_vol_delay_s(0) == 24u, "default delay 24 s");
 CHECK(zizhan_vol_delay_s(1) == 5u, "min clamp 5 s");
 CHECK(zizhan_vol_delay_s(500) == 120u, "max clamp 120 s");
 CHECK(zizhan_vol_delay_s(18) == 18u, "explicit 18 s preserved");

 CHECK(zizhan_vol_up_clicks(1) == 3u && zizhan_vol_down_clicks(1) == 2u && zizhan_vol_total_clicks(1) == 5u,
       "mode 1: 3 up + 2 down");
 CHECK(zizhan_vol_click_key(1, 0) == ZizhanVolKeyUp && zizhan_vol_click_key(1, 2) == ZizhanVolKeyUp,
       "mode 1 first three clicks are Vol+");
 CHECK(zizhan_vol_click_key(1, 3) == ZizhanVolKeyDown && zizhan_vol_click_key(1, 4) == ZizhanVolKeyDown,
       "mode 1 last two clicks are Vol-");
 CHECK(zizhan_vol_after_gap_ms(1, 0) == 600u && zizhan_vol_after_gap_ms(1, 1) == 600u,
       "click gap 600 ms");
 CHECK(zizhan_vol_after_gap_ms(1, 2) == 1500u, "direction gap 1500 ms");
 CHECK(zizhan_vol_after_gap_ms(1, 4) == 0u, "no trailing gap");

 CHECK(zizhan_vol_up_clicks(2) == 4u && zizhan_vol_down_clicks(2) == 0u && zizhan_vol_total_clicks(2) == 4u,
       "mode 2: 4 up + 0 down");
 CHECK(ZizhanVolHoldMs <= 200u, "short click avoids key repeat");

 printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
 return failures ? 1 : 0;
}
