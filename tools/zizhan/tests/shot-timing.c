/* Actual scheduling helper: fastboot counter offsets must not trigger a
 * capture before its requested delay, including near the clock limit. */
#include <assert.h>
#include <stdint.h>
#include "ZizhanShotTiming.h"

int main(void)
{
    const uint64_t delays[] = {1, 150, 90000, UINT32_MAX};
    const uint64_t origins[] = {0, 579205789, 86400000000ULL};
    for (unsigned i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
        for (unsigned j = 0; j < sizeof(delays) / sizeof(delays[0]); ++j) {
            uint64_t deadline = zizhan_shot_deadline(origins[i], delays[j]);
            assert(deadline - origins[i] == delays[j]);
            assert(origins[i] < deadline);
            assert(origins[i] + delays[j] - 1 < deadline);
            assert(origins[i] + delays[j] == deadline);
            /* A late frame starts the next interval at actual capture time. */
            uint64_t captured = deadline + 100;
            assert(zizhan_shot_deadline(captured, 90) - captured == 90);
        }
    }
    assert(zizhan_shot_deadline(UINT64_MAX - 7, 8) == UINT64_MAX);
    assert(zizhan_shot_deadline(UINT64_MAX - 7, 7) == UINT64_MAX);
    assert(zizhan_shot_deadline(UINT64_MAX, 1) == UINT64_MAX);
    return 0;
}
