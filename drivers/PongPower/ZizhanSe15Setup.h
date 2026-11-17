/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SE15_SETUP_H
#define ZIZHAN_SE15_SETUP_H
#include <stdint.h>
namespace zizhan_se15_setup {
enum Result { Ok, ClockTimeout, VoteMismatch, PinMismatch, EngineUnavailable, EngineBusy, PinOwned };
struct State {
    uint32_t original_votes = 0, observed_votes = 0;
    uint32_t original_sda = 0, original_scl = 0;
    unsigned writes = 0;
    bool retained = false;
};
// Fixed cape-gcc cb7a356ae138e77992ae199eebb86559d825a673.
// Add (never remove) wrap2 core/core2x/AHB/S0 votes at62010 bits0..4.
// Dedicated S0 root/PLL/DFS rate, all other engines, sleep and reset untouched.
// Window types expose only the mapped resource used by each phase.
template <typename Votes, typename Gates, typename Core, typename Now, typename Pause>
Result clocks(Votes &votes, Gates &gates, Core &core, State &s, Now now, Pause pause) {
    s.original_votes = votes.read(0x10);
    const uint32_t wanted = s.original_votes | 0x1f;
    if (wanted != s.original_votes) {
        s.retained = true; ++s.writes; votes.write(0x10, wanted);
    }
    s.observed_votes = votes.read(0x10);
    if ((s.observed_votes & wanted) != wanted) return VoteMismatch;
    const uint64_t start = now();
    for (unsigned tries = 0; tries < 1000; ++tries) {
        const uint32_t se = gates.read(0xc), master = gates.read(4), slave = gates.read(8);
        const uint32_t c = core.read(0x280), c2 = core.read(0x28c);
        // AHB HWCG bit1 is declared in the matched source; automatic idle
        // gating may keep HALT set. S0/core branches have no such exemption.
        if (!(se & 0x80000000u) && (!(master & 0x80000000u) || (master & 2)) &&
            (!(slave & 0x80000000u) || (slave & 2)) && !((c | c2) & 0x80000000u)) return Ok;
        if (now() - start >= 100000) break;
        pause(100);
    }
    return ClockTimeout; // retain votes; no blind shared clock rollback
}
template <typename Engine>
Result engine(Engine &se, uint32_t &fw, uint32_t &status, uint32_t &irq) {
    fw = se.read(0x68); status = se.read(0x40); irq = se.read(0x610);
    if (((fw >> 8) & 255) != 3 || (se.read(0x64) & 1)) return EngineUnavailable;
    // Do not steal an existing M/S transaction or discard pending IRQ/data.
    if ((status & 0x1001) || irq || (se.read(0x804) & 0x1ffffffu)) return EngineBusy;
    return Ok;
}
template <typename Pin>
Result pins(Pin &sda, Pin &scl, State &s) {
    s.original_sda = sda.read(0); s.original_scl = scl.read(0);
    // Exact known dormant GPIO/pulldown/2mA state from the prior device
    // round, or already matching qup15. EGPIO presence without APPS enable
    // must be handled by an ownership driver; this candidate never claims it.
    const uint32_t original[] = {s.original_sda, s.original_scl};
    for (uint32_t value : original) {
        const uint32_t low = value & 0x3ff;
        if ((low != 1 && low != 7) || ((value & (1u << 11)) && !(value & (1u << 12)))) return PinOwned;
    }
    const uint32_t a = (s.original_sda & ~0x3ffu) | 7;
    const uint32_t b = (s.original_scl & ~0x3ffu) | 7;
    if (a != s.original_sda) { ++s.writes; s.retained = true; sda.write(0, a); }
    if (b != s.original_scl) { ++s.writes; s.retained = true; scl.write(0, b); }
    if (sda.read(0) != a || scl.read(0) != b) return PinMismatch;
    return Ok;
}
} // namespace zizhan_se15_setup
#endif
