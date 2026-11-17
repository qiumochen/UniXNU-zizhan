/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AMP_RESET_H
#define ZIZHAN_AMP_RESET_H
#include <stdint.h>
namespace zizhan_amp_reset {
enum Result { Ok, ForeignOwner, AssertFailed, ReleaseFailed };
struct State { uint32_t initial_config=0, initial_io=0; bool claimed=false, released=false; };
// Actual phone module cs35l41_probe: GPIOD_OUT_LOW, delay2000..2100us,
// high, delay2000..2100us; second amp tolerates shared-reset EBUSY.
// Saved cape DT GPIO1: GPIO, bias-disable,16mA/output-high. Never use TFA
// reset polarity or GPIO89. Caller owns BOTH amps and SE15 for the boot.
template<typename Pin,typename Pause>
Result release(Pin &pin,State &s,Pause pause) {
    s.initial_config=pin.read(0);s.initial_io=pin.read(4);
    if ((s.initial_config & 0x3ff)!=1 || (s.initial_io & 3) ||
        ((s.initial_config & (1u<<11)) && !(s.initial_config & (1u<<12)))) return ForeignOwner;
    const uint32_t config=(s.initial_config & ~0x3ffu)|0x3c0;
    s.claimed=true;
    pin.write(4,0);pin.write(0,config);
    pause(2100);
    if(pin.read(0)!=config || (pin.read(4)&3))return AssertFailed;
    pin.write(4,2);
    pause(2100);
    if(pin.read(0)!=config || (pin.read(4)&3)!=3)return ReleaseFailed;
    s.released=true;return Ok;
}
template<typename Pin>
bool assert_and_retain(Pin &pin,State &s) {
    if(!s.claimed)return true;
    pin.write(4,0);s.released=false;
    return pin.read(0)==((s.initial_config & ~0x3ffu)|0x3c0) && !(pin.read(4)&3);
}
} // namespace zizhan_amp_reset
#endif
