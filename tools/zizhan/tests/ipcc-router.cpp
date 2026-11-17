/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <atomic>
#include <thread>
#include <vector>
#include "../../../drivers/PongPower/ZizhanIpccRouter.h"

struct Context { uint32_t expected; unsigned calls = 0; bool *cleared; };
static void receive(void *arg, uint32_t id) {
    auto &ctx = *static_cast<Context *>(arg);
    assert(id == ctx.expected && *ctx.cleared);
    ++ctx.calls;
}
static void other(void *, uint32_t) { assert(false); }
static void atomic_receive(void *arg, uint32_t id) {
    auto *counter = static_cast<std::atomic<unsigned> *>(arg);
    assert(id == 0x40002);
    counter->fetch_add(1);
}

int main() {
    using namespace zizhan_ipcc;
    Router router;
    bool cleared = false;
    Context contexts[] = {{0, 0, &cleared}, {0x30000, 0, &cleared}, {0x30002, 0, &cleared},
                          {0x40000, 0, &cleared}, {0x40002, 0, &cleared}};
    for (auto &ctx : contexts) assert(subscribe(router, ctx.expected, receive, &ctx) == Result::Ok);
    assert(subscribe(router, 0x40002, receive, &contexts[4]) == Result::Duplicate);
    assert(subscribe(router, 0x40002, other, &contexts[4]) == Result::Conflict);
    assert(subscribe(router, 0x40002, receive, &contexts[3]) == Result::Conflict);
    assert(subscribe(router, 1, nullptr, nullptr) == Result::Invalid);
    assert(subscribe(router, UINT32_MAX, other, nullptr) == Result::Invalid);
    assert(router.used == 5);
    for (unsigned i = 0; i < 3; ++i) assert(subscribe(router, 99 + i, other, nullptr) == Result::Ok);
    assert(subscribe(router, 200, other, nullptr) == Result::Full);
    assert(router.used == 8);

    std::vector<uint32_t> input = {0x30002, 0x40002, 0, 0x40000, 0x30000, 0x90000};
    size_t next = 0;
    unsigned acks = 0;
    auto result = drain(router, [&] {
        cleared = false;
        return next == input.size() ? UINT32_MAX : input[next];
    }, [&](uint32_t id) {
        assert(id == input[next++]);
        ++acks; cleared = true;
    });
    assert(result.received == 6 && result.delivered == 5 && result.unknown == 1 && !result.budget_used);
    assert(acks == 6);
    for (auto &ctx : contexts) assert(ctx.calls == 1);
    for (unsigned i = 0; i < 5; ++i) assert(router.slots[i].delivered == 1);
    for (unsigned i = 5; i < 8; ++i) assert(router.slots[i].delivered == 0);

    // An invalid/unknown client is acknowledged once but not misdelivered.
    assert(!dispatch(router, 0x50000));
    unsigned read_count = 0;
    result = drain(router, [&] { ++read_count; return UINT32_MAX; }, [](uint32_t) { assert(false); });
    assert(read_count == 1 && !result.received);
    result = drain(router, [] { return 0x90000u; }, [](uint32_t) {});
    assert(result.received == 32 && result.unknown == 32 && result.budget_used);

    // Append publication while an IRQ-like reader dispatches: immutable slots
    // become visible only after callback/context initialization is complete.
    Router concurrent;
    std::atomic<unsigned> counter{0};
    std::atomic<bool> ready{false};
    std::thread reader([&] {
        while (!ready.load()) (void)dispatch(concurrent, 0x40002);
        for (unsigned i = 0; i < 10000; ++i) assert(dispatch(concurrent, 0x40002));
    });
    assert(subscribe(concurrent, 0x40002, atomic_receive, &counter) == Result::Ok);
    ready.store(true);
    reader.join();
    assert(counter >= 10000);
    assert(__atomic_load_n(&concurrent.slots[0].delivered, __ATOMIC_RELAXED) == counter.load());
}
