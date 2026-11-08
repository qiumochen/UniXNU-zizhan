// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan POWER-02: one fixed display stream/context, read-only. No SMMU scan,
// table walk, register write, DMA mapping or power transition is implemented.
#pragma once
#include <stdint.h>
namespace zizhan_dsi_domain {
struct Board {
    uint64_t base;
    uint32_t span, version, group, bank, sid, mask, cba2r, reserved;
};
static_assert(sizeof(Board)==40,"zizhan display-domain ADT wire");
enum class State { InvalidBoard, UnsupportedLayout, StreamMismatch, ContextMismatch,
                   ReadError, Changed, Faulted, TranslationActive, IdentityObserved };
struct Observation {
    State state=State::InvalidBoard;
    uint32_t id0=0,id1=0,smr=0,s2cr=0,cba2r=0,sctlr=0,fsr=0;
    unsigned reads=0;
    bool context_read=false;
};
inline bool valid(const Board &b) {
    // Bound all callbacks to the already known public HLOS display aperture.
    // Board identity and exact resource match are also checked by the adapter.
    return b.version==1 && b.base && !(b.base&4095) && b.span>=0x84000 &&
        b.span<=0x100000 && b.group<=3 && b.bank<=3 && b.sid<=0x7fff &&
        b.mask<=0x7fff && b.cba2r==1 && !b.reserved;
}
template <typename Io>
Observation observe(const Board &b,Io &io) {
    Observation o;
    if(!valid(b))return o;
    auto global=[&](uint32_t offset,uint32_t &v) {
        ++o.reads;return io.global(offset,v);
    };
    auto context=[&](uint32_t offset,uint32_t &v) {
        ++o.reads;return io.context(0x80000+b.bank*0x1000+offset,v);
    };
    o.state=State::ReadError;
    if(!global(0x20,o.id0)||!global(0x24,o.id1))return o;
    // Historical zizhan IDR1: 4K register pages, NUMPAGENDXB=6. Do not
    // manufacture a new CB aperture from an unqualified layout.
    if(!o.id0 || o.id0==UINT32_MAX || (o.id1&0xf0000000)!=0x60000000 ||
       (o.id1&0xff)<=b.bank) {o.state=State::UnsupportedLayout;return o;}
    if(!global(0x800+b.group*4,o.smr)||!global(0xc00+b.group*4,o.s2cr))return o;
    if(o.smr!=(0x80000000u|(b.mask<<16)|b.sid)) {
        o.state=State::StreamMismatch;return o;
    }
    // Only the previously observed translate-CB form is inspected. A changed
    // bypass/fault mode or bank is not permission to read another context.
    if((o.s2cr&0x300ff)!=(b.bank&0xff)) {o.state=State::ContextMismatch;return o;}
    if(!global(0x1800+b.bank*4,o.cba2r))return o;
    if(o.cba2r!=b.cba2r){o.state=State::ContextMismatch;return o;}
    if(!context(0,o.sctlr)||!context(0x58,o.fsr))return o;
    o.context_read=true;
    uint32_t smr=0,s2cr=0,cba2r=0,sctlr=0;
    if(!global(0x800+b.group*4,smr)||!global(0xc00+b.group*4,s2cr)||
       !global(0x1800+b.bank*4,cba2r)||!context(0,sctlr))return o;
    if(smr!=o.smr||s2cr!=o.s2cr||cba2r!=o.cba2r||sctlr!=o.sctlr) {
        o.state=State::Changed;return o;
    }
    // Match the device's existing ArmSmmu4K fault policy: FSR 0x400 is a
    // nonfatal status bit observed on the display CB in g151; only the
    // documented fatal mask blocks the snapshot. Never clear or acknowledge
    // either value from this read-only observer.
    if(o.fsr & 0xc00001feu){o.state=State::Faulted;return o;}
    o.state=(o.sctlr&1)?State::TranslationActive:State::IdentityObserved;
    // IdentityObserved describes this bounded snapshot only. It does not
    // grant a lasting IOVA lease or prove that DSI command DMA reaches RAM.
    return o;
}
} // namespace zizhan_dsi_domain
