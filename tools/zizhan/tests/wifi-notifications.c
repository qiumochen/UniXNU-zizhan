/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "PongWifiNotify.h"

int main(void)
{
    struct pong_wifi_notification_queue q = {0};
    struct pong_wifi_notification out;
    assert(!pong_wifi_notification_pop(&q, &out));
    /* Preserve connect/down/connect edges, including their actual result
     * and key state, instead of reducing them to the last link level. */
    const struct pong_wifi_notification transitions[] = {
        {3, 0, -42, 1}, {4, -1, 0, 0}, {3, 0, -55, 1},
        {3, 21, 0, 1}, {3, 0, -31, 0}
    };
    for (unsigned i = 0; i < sizeof(transitions)/sizeof(transitions[0]); ++i)
        assert(pong_wifi_notification_push(&q, transitions[i]));
    for (unsigned i = 0; i < sizeof(transitions)/sizeof(transitions[0]); ++i) {
        assert(pong_wifi_notification_pop(&q, &out));
        assert(out.kind == transitions[i].kind && out.result == transitions[i].result);
        assert(out.rssi == transitions[i].rssi && out.keyed == transitions[i].keyed);
    }
    /* Several laps exercise wrapped occupancy and full-queue refusal. */
    for (unsigned lap = 0; lap < 4; ++lap) {
        for (unsigned i = 0; i < PONG_WIFI_NOTIFY_CAPACITY; ++i) {
            struct pong_wifi_notification event = {1, (int)(lap*100+i), 0, 0};
            assert(pong_wifi_notification_push(&q, event));
        }
        struct pong_wifi_notification extra = {3, 999, -20, 1};
        assert(!pong_wifi_notification_push(&q, extra));
        for (unsigned i = 0; i < PONG_WIFI_NOTIFY_CAPACITY; ++i) {
            assert(pong_wifi_notification_pop(&q, &out));
            assert(out.result == (int)(lap*100+i));
        }
    }
    /* A failed interface discards pending successes before its terminal
     * failure notification; stale success must not reappear afterward. */
    assert(pong_wifi_notification_push(&q, transitions[0]));
    pong_wifi_notification_reset(&q);
    struct pong_wifi_notification failure = {5, 1, 0, 0};
    assert(pong_wifi_notification_push(&q, failure));
    assert(pong_wifi_notification_pop(&q, &out) && out.kind == 5);
    assert(!pong_wifi_notification_pop(&q, &out));
    puts("PASS: notification FIFO preserves link edges, bounds occupancy and removes stale completions on failure");
    return 0;
}
