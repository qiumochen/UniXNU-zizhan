/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <string>
#include <vector>
#include "ZizhanRpmhBatch.h"

struct FakeWindow {
    static constexpr unsigned b = 0xd00 + 4 * 0x2a0, other = 0xd00 + 3 * 0x2a0;
    std::array<uint32_t, 0x10000 / 4> memory{};
    std::vector<std::pair<unsigned, uint32_t>> writes;
    uint8_t *v = reinterpret_cast<uint8_t *>(1);
    uint64_t us = 0;
    unsigned reads = 0, pauses = 0, polls = 0, opens = 0;
    bool triggered = false, stale = false, stuck = false, partial = false, instant = false;
    bool clear_stuck = false, control_stuck = false, cleanup_stuck = false, frozen_clock = false;
    bool map_failure = false;
    FakeWindow() {
        memory[0] = 2u << 16; memory[0xc / 4] = (8u << 12) | (16u << 27);
        memory[(b + 0x18) / 4] = memory[(other + 0x18) / 4] = 1;
        memory[0xd00 / 4] = 0xffff;
        for (unsigned i = 0; i < 16; ++i) memory[(b + 0x3c + 20 * i) / 4] = 0x10100;
    }
    bool open(uint64_t pa, size_t bytes) {
        assert(pa == 0x17a20000 && bytes == 0x10000); ++opens;
        if (!map_failure) v = reinterpret_cast<uint8_t *>(1);
        return !map_failure;
    }
    uint32_t r(unsigned at) {
        assert(!(at & 3) && at < 0x10000); ++reads;
        if (triggered && at == b + 0x18) {
            ++polls;
            if (stale) return 1;
            if (stuck || (!instant && polls == 1)) return 0;
            if (!partial) memory[0xd04 / 4] |= 1u << 4;
            return 1;
        }
        if (triggered && at >= b + 0x3c && at <= b + 0x3c + 20 * 15 && (at - b - 0x3c) % 20 == 0) {
            if (partial && at != b + 0x3c) return 0;
        }
        return memory[at / 4];
    }
    void w(unsigned at, uint32_t value) {
        // Never allow a sleep/wake TCS, the GPU's active TCS3 or IRQ attach.
        assert(at == 0xd00 || at == 0xd08 || (at >= b && at < b + 0x2a0));
        writes.emplace_back(at, value);
        if (at == 0xd08) { if (!clear_stuck) memory[0xd04 / 4] &= ~value; return; }
        if (at == b + 0x14 && control_stuck) return;
        if (at == b + 0x1c && cleanup_stuck && triggered && !value) return;
        memory[at / 4] = value;
        if (at == b + 0x14 && (value & (1u << 24))) triggered = true;
    }
    uint64_t now() const { return frozen_clock ? 0 : us / 1000; }
    void pause(unsigned delay) { assert(delay == 10); us += delay; ++pauses; }
};

#ifndef ZIZHAN_RPMH_OWNER_TEST
static zizhan_rpmh::Result run(FakeWindow &w, zizhan_rpmh::BatchState &s, unsigned count = 3) {
    uint32_t addresses[16], values[16];
    for (unsigned i = 0; i < 16; ++i) { addresses[i] = 0x50100 + 4 * i; values[i] = 0x20000003 + i; }
    values[count ? (count <= 16 ? count : 16) - 1 : 0] |= 1u << 30;
    return zizhan_rpmh::batch(w, s, addresses, values, count, [&] { return w.now(); }, [&](unsigned us) { w.pause(us); });
}
int main() {
    using namespace zizhan_rpmh;
    for (unsigned count : {1u, 3u, 16u}) {
        FakeWindow w; BatchState s;
        assert(run(w, s, count) == Ok && !s.uncertain);
        const unsigned mask = (1u << count) - 1;
        unsigned triggers = 0;
        for (const auto &write : w.writes)
            triggers += write.first == FakeWindow::b + 0x14 && !!(write.second & (1u << 24));
        assert(triggers == 1 && w.memory[(FakeWindow::b + 0x10) / 4] == mask);
        assert(!w.memory[(FakeWindow::b + 0x1c) / 4]);
        for (unsigned i = 0; i < count; ++i) {
            assert(w.memory[(FakeWindow::b + 0x30 + 20 * i) / 4] == 0x10108);
            assert(w.memory[(FakeWindow::b + 0x34 + 20 * i) / 4] == 0x50100 + i * 4);
        }
    }
    for (unsigned test = 0; test < 9; ++test) {
        FakeWindow w; BatchState s; Result expected = Argument;
        uint32_t a[] = {0x50100, 0x50104}, v[] = {0, 0};
        if (test == 0) a[0] = 0;
        if (test == 1) a[0] = 1;
        if (test == 2) a[1] = a[0];
        if (test == 3) { w.memory[0] = 3u << 16; expected = Capacity; }
        if (test == 4) { w.memory[0xc / 4] = (7u << 12) | (16u << 27); expected = Capacity; }
        if (test == 5) { w.memory[0xc / 4] = (8u << 12) | (1u << 27); expected = Capacity; }
        if (test == 6) { w.memory[(FakeWindow::b + 0x18) / 4] = 0; expected = Busy; }
        if (test >= 7) {
            w.memory[(FakeWindow::other + 0x18) / 4] = 0;
            w.memory[(FakeWindow::other + 0x1c) / 4] = test == 7 ? 2u : 1u << 20;
            w.memory[(FakeWindow::other + 0x34 + 20) / 4] = a[0]; expected = Conflict;
        }
        assert(batch(w, s, a, v, 2, [&] { return w.now(); }, [&](unsigned us) { w.pause(us); }) == expected);
        assert(w.writes.empty() && !s.uncertain);
    }
    for (unsigned count : {0u, 17u}) {
        FakeWindow w; BatchState s; assert(run(w, s, count) == Argument && !w.reads && w.writes.empty());
    }
    {
        FakeWindow w; BatchState s; uint32_t a = 0x50100;
        assert(batch(w, s, &a, nullptr, 1, [&] { return w.now(); }, [&](unsigned us) { w.pause(us); }) == Argument && !w.reads);
    }
    for (unsigned test = 0; test < 7; ++test) {
        FakeWindow w; BatchState s;
        if (test == 0) { w.clear_stuck = true; w.memory[0xd04 / 4] = 1u << 4; }
        if (test == 1) { w.control_stuck = true; w.memory[(FakeWindow::b + 0x14) / 4] = 1u << 24; }
        if (test == 2) w.stale = true;
        if (test == 3) w.partial = true;
        if (test == 4) w.stuck = true;
        if (test == 5) w.cleanup_stuck = true;
        if (test == 6) { w.stale = true; w.frozen_clock = true; }
        Result expected = test < 2 ? ClearTimeout : test == 5 ? CleanupTimeout : ResponseTimeout;
        assert(run(w, s) == expected && s.uncertain && w.pauses <= 11000);
        const auto writes = w.writes; const unsigned reads = w.reads;
        // Simulated late hardware completion cannot unpoison the owner.
        w.stale = w.partial = w.stuck = false;
        assert(run(w, s) == Uncertain && w.writes == writes && w.reads == reads);
        if (test < 2) assert(!w.triggered);
    }
    { FakeWindow w; BatchState s; w.instant = true; assert(run(w, s) == Ok && !s.uncertain); }
    { FakeWindow w; BatchState s;
      w.memory[(FakeWindow::other + 0x18) / 4] = 0;
      w.memory[(FakeWindow::other + 0x1c) / 4] = 1;
      w.memory[(FakeWindow::other + 0x34) / 4] = 0x30100;
      assert(run(w, s) == Ok); }
    puts("PASS actual RPMh batch core: payload/capacity/conflict/fresh completion/deadlines/retained failures; synthetic MMIO only");
}
#endif
