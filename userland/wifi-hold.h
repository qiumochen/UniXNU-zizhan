#ifndef ZIZHAN_WIFI_HOLD_H
#define ZIZHAN_WIFI_HOLD_H
#include <stdbool.h>

enum pong_wifi_hold_action {
    PongWifiHoldWait,
    PongWifiHoldProbe,
    PongWifiHoldStopped,
    PongWifiHoldLinkLost,
    PongWifiHoldStartupTimeout,
    PongWifiHoldInvalidClock
};

struct pong_wifi_hold {
    double began, first_success, last_success, next_probe;
    unsigned attempts, successes, failures, link_losses;
    bool stopped;
};

static inline enum pong_wifi_hold_action
pong_wifi_hold_poll(struct pong_wifi_hold *state, double now, bool link, bool ipv4,
    bool reported, bool probe_running)
{
    if (state->stopped) return PongWifiHoldStopped;
    if (!(now >= state->began)) {
        state->stopped = true;
        return PongWifiHoldInvalidClock;
    }
    if (state->successes && (!link || !ipv4)) {
        state->stopped = true;
        ++state->link_losses;
        return PongWifiHoldLinkLost;
    }
    if (!state->successes && now - state->began >= 330) {
        state->stopped = true;
        return PongWifiHoldStartupTimeout;
    }
    if (link && ipv4 && reported && !probe_running && now >= state->next_probe)
        return PongWifiHoldProbe;
    return PongWifiHoldWait;
}

static inline void
pong_wifi_hold_result(struct pong_wifi_hold *state, double now, bool success)
{
    if (state->stopped) return;
    ++state->attempts;
    if (success) {
        if (!state->successes) state->first_success = now;
        ++state->successes;
        state->last_success = now;
        state->next_probe = now + 60;
        if (state->successes >= 64) state->stopped = true;
    } else {
        ++state->failures;
        state->next_probe = now + 8;
        if (state->successes || state->failures >= 4) state->stopped = true;
    }
}
#endif
