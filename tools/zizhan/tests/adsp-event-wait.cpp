// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../../../drivers/PongPower/ZizhanAdspWait.h"
#include <cassert>
#include <cstdio>

// Inject a doorbell on every boundary of the actual kernel wait algorithm.
// Kernel assert_wait/thread_block are represented by a registered wake state;
// the same production template determines whether blocking is reached.
int main() {
    for (unsigned boundary = 0; boundary < 5; ++boundary) {
        unsigned takes = 0, arms = 0, clears = 0, blocks = 0;
        uint32_t pending = boundary == 0 ? 1u : 0u;
        bool armed = false, awakened = false;
        auto send = [&] { pending |= 4u; if (armed) awakened = true; };
        uint32_t result = zizhan_adsp::wait(0,
            [&] {
                ++takes;
                if ((boundary == 1 && takes == 1) || (boundary == 3 && takes == 2)) send();
                auto value = pending; pending = 0; return value;
            },
            [&](uint32_t ms) {
                assert(ms == 0); ++arms; armed = true;
                if (boundary == 2) send();
            },
            [&] { ++clears; assert(armed); armed = false; },
            [&] {
                ++blocks; assert(armed);
                if (boundary == 4) send();
                assert(awakened); armed = false;
            });
        assert(result == (boundary == 0 ? 1u : 4u));
        assert(pending == 0 && !armed && takes <= 3);
        assert(blocks == (boundary == 4 ? 1u : 0u));
        assert(arms == (boundary < 2 ? 0u : 1u));
        assert(clears == (boundary == 2 || boundary == 3 ? 1u : 0u));
    }
    // Timeouts and spurious wakes consume no invented event.
    const uint32_t timeouts[] = {0u, 100u, 1000u};
    for (uint32_t ms : timeouts) {
        unsigned blocks = 0;
        assert(zizhan_adsp::wait(ms, [] { return 0u; },
            [ms](uint32_t v) { assert(v == ms); },
            [] { assert(false); }, [&] { ++blocks; }) == 0);
        assert(blocks == 1);
    }
    assert(zizhan_adsp::timeout(false, false) == 100);
    assert(zizhan_adsp::timeout(false, true) == 100);
    assert(zizhan_adsp::timeout(true, false) == 0);
    assert(zizhan_adsp::timeout(true, true) == 1000);
    puts("PASS: actual ADSP wait path, five arrival boundaries, spurious/timeout and idle/retry policy");
}
