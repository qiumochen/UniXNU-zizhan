#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "../../../drivers/PongPower/PongDsiLease.h"

static uint32_t state, active;
static unsigned transfers;
static void *client(void *argument)
{
    uint32_t owner = (uint32_t)(uintptr_t)argument;
    for (unsigned i = 0; i < 10000; i++) {
        while (!pong_dsi_lease_try(&state, owner)) sched_yield();
        assert(__atomic_fetch_add(&active, 1u, __ATOMIC_RELAXED) == 0);
        ++transfers;
        assert(__atomic_fetch_sub(&active, 1u, __ATOMIC_RELAXED) == 1);
        assert(pong_dsi_lease_release(&state, owner));
    }
    return NULL;
}
int main(void)
{
    assert(!pong_dsi_lease_try(&state, 0));
    assert(!pong_dsi_lease_try(&state, 3));
    assert(pong_dsi_lease_try(&state, PongDsiTiming));
    assert(!pong_dsi_lease_try(&state, PongDsiTiming));
    assert(!pong_dsi_lease_release(&state, PongDsiBacklight));
    assert(state == PongDsiTiming);
    /* A still-active timed-out DMA keeps ownership; the other user waits. */
    assert(!pong_dsi_lease_try(&state, PongDsiBacklight));
    assert(pong_dsi_lease_release(&state, PongDsiTiming));
    assert(!pong_dsi_lease_release(&state, PongDsiTiming));
    pthread_t timing, backlight;
    assert(!pthread_create(&timing, NULL, client, (void *)(uintptr_t)PongDsiTiming));
    assert(!pthread_create(&backlight, NULL, client, (void *)(uintptr_t)PongDsiBacklight));
    assert(!pthread_join(timing, NULL));
    assert(!pthread_join(backlight, NULL));
    assert(!state && !active && transfers == 20000);
    puts("PASS: DSI lease rejects wrong owners, retains busy ownership and serializes timing/backlight clients");
}
