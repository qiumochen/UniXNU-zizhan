/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SE15_STATE_H
#define ZIZHAN_SE15_STATE_H
#include <stdint.h>
namespace zizhan_se15 {
enum Reason { Ready, ClockOff, PinsMismatch, ResetHeld };
struct Snapshot {
    uint32_t votes, se_gate, master_gate, slave_gate;
    uint32_t sda_config, scl_config, reset_config, reset_io;
};
// Fixed Xiaomi cape-gcc cb7a356ae138e77992ae199eebb86559d825a673:
// clock120=wrapper2 S0,138/139=M/S AHB; enabled bits4/2/1 at62010.
// cape pinctrl qup15 is mux1 for GPIO56/57. Require DT pull-up/2mA,
// no GPIO output enable. Shared GPIO1 must already drive physical high;
// never toggle it here or infer amplifier supply state from the reset pin.
inline Reason check(const Snapshot &s) {
    if ((s.votes & 0x16) != 0x16 ||
        ((s.se_gate | s.master_gate | s.slave_gate) & 0x80000000u)) return ClockOff;
    if ((s.sda_config & 0x3ff) != 7 || (s.scl_config & 0x3ff) != 7) return PinsMismatch;
    if ((s.reset_config & 0x23c) != 0x200 || !(s.reset_io & 2)) return ResetHeld;
    return Ready;
}
} // namespace zizhan_se15
#endif
