#include "../../../userland/wifi-hold.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct pong_wifi_hold state = {.began = 100};
    assert(pong_wifi_hold_poll(&state, 101, false, false, false, false) == PongWifiHoldWait);
    assert(pong_wifi_hold_poll(&state, 102, true, true, true, false) == PongWifiHoldProbe);
    assert(pong_wifi_hold_poll(&state, 102, true, true, true, true) == PongWifiHoldWait);
    pong_wifi_hold_result(&state, 103, true);
    assert(state.first_success == 103 && state.successes == 1);
    assert(pong_wifi_hold_poll(&state, 162, true, true, true, false) == PongWifiHoldWait);
    assert(pong_wifi_hold_poll(&state, 163, true, true, true, false) == PongWifiHoldProbe);
    pong_wifi_hold_result(&state, 164, true);
    assert(state.last_success == 164 && state.attempts == 2);
    assert(pong_wifi_hold_poll(&state, 165, true, false, false, false) == PongWifiHoldLinkLost);
    pong_wifi_hold_result(&state, 166, true);
    assert(state.stopped && state.link_losses == 1 && state.successes == 2);

    struct pong_wifi_hold startup = {.began = 10};
    for (unsigned attempt = 0; attempt < 4; ++attempt)
        pong_wifi_hold_result(&startup, 11 + attempt * 8, false);
    assert(startup.stopped && startup.failures == 4 && !startup.successes);
    struct pong_wifi_hold timeout = {.began = 10};
    assert(pong_wifi_hold_poll(&timeout, 340, false, false, false, false) == PongWifiHoldStartupTimeout);
    struct pong_wifi_hold clock = {.began = 10};
    assert(pong_wifi_hold_poll(&clock, 9, true, true, true, false) == PongWifiHoldInvalidClock);
    struct pong_wifi_hold failure = {.began = 10};
    pong_wifi_hold_result(&failure, 11, true);
    pong_wifi_hold_result(&failure, 72, false);
    assert(failure.stopped && failure.successes == 1 && failure.failures == 1);
    struct pong_wifi_hold bounded = {0};
    for (unsigned attempt = 0; attempt < 65; ++attempt)
        pong_wifi_hold_result(&bounded, 1 + attempt * 60, true);
    assert(bounded.stopped && bounded.successes == 64);
    puts("PASS: actual hold state bounds startup/probes, spaces successful witnesses and never resumes after a post-baseline failure");
}
