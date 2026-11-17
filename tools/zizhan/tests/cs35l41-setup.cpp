/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <vector>
#include "ZizhanSe15Setup.h"
#include "ZizhanAudioSck.h"
struct Page {
    std::map<unsigned, uint32_t> words;
    std::vector<std::pair<unsigned, uint32_t>> writes;
    bool refuse = false;
    uint32_t read(unsigned at) { return words[at]; }
    void write(unsigned at, uint32_t value) { writes.emplace_back(at, value); if (!refuse) words[at] = value; }
};
int main() {
    using namespace zizhan_se15_setup;
    Page votes, gates, core; State s;
    votes.words[0x10] = 0x80000000;
    uint64_t us = 0; unsigned pauses = 0;
    auto now = [&] { return us; }; auto pause = [&](unsigned v) { assert(v == 100); us += v; ++pauses; };
    assert(clocks(votes, gates, core, s, now, pause) == Ok && s.retained && s.writes == 1);
    assert(votes.read(0x10) == 0x8000001f && votes.writes.size() == 1 && gates.writes.empty() && core.writes.empty());
    assert(clocks(votes, gates, core, s, now, pause) == Ok && votes.writes.size() == 1);
    for (unsigned i = 0; i < 4; ++i) {
        votes = Page{}; gates = Page{}; core = Page{}; s = State{}; pauses = 0;
        if (i == 0) votes.refuse = true;
        if (i == 1) gates.words[0xc] = 1u << 31;
        if (i == 2) core.words[0x28c] = 1u << 31;
        if (i == 3) gates.words[8] = 1u << 31;
        auto frozen = [] { return 0ull; };
        assert(clocks(votes, gates, core, s, frozen, pause) == (i == 0 ? VoteMismatch : ClockTimeout));
        assert(s.retained && votes.writes.size() == 1 && pauses <= 1000);
    }
    votes = Page{}; gates = Page{}; core = Page{}; s = State{};
    gates.words[4] = gates.words[8] = 0x80000002;
    assert(clocks(votes, gates, core, s, now, pause) == Ok); // matched AHB HWCG semantics
    Page se; uint32_t fw, status, irq;
    assert(engine(se, fw, status, irq) == EngineUnavailable);
    se.words[0x68] = 3u << 8;
    assert(engine(se, fw, status, irq) == Ok && se.writes.empty());
    for (unsigned at : {0x40u,0x610u,0x804u}) {
        se.words[at] = 1; assert(engine(se, fw, status, irq) == EngineBusy); se.words[at] = 0;
    }
    for (unsigned i = 0; i < 5; ++i) {
        Page a, b; s = State{}; a.words[0] = 1; b.words[0] = 0x80000001;
        if (i == 1) a.words[0] = 4;
        if (i == 2) b.words[0] |= 1u << 11;
        if (i == 3) a.refuse = true;
        if (i == 4) { a.words[0] = 7; b.words[0] = 7; }
        const Result result = pins(a, b, s);
        if (i == 1 || i == 2) assert(result == PinOwned && a.writes.empty() && b.writes.empty());
        else if (i == 3) assert(result == PinMismatch && s.retained);
        else { assert(result == Ok); assert(a.read(0) == 7 && b.read(0) == (i == 4 ? 7 : 0x80000007)); }
    }
    for(unsigned test=0;test<6;++test){
        Page pin;pin.words[0]=1;zizhan_audio_sck::State state;
        if(test==1){pin.words[0]=4;}if(test==2)pin.words[4]=1;if(test==3)pin.refuse=true;
        const bool connected=zizhan_audio_sck::connect(pin,state);
        if(test==1||test==2)assert(!connected&&!state.claimed&&pin.writes.empty());
        else if(test==3)assert(!connected&&state.claimed);
        else{assert(connected&&state.claimed&&pin.words[0]==0x284&&pin.words[4]==2);
            assert(pin.writes[0]==std::make_pair(4u,2u)&&pin.writes[1]==std::make_pair(0u,0x284u));}
        if(test==4)pin.refuse=true;else pin.refuse=false;
        const bool restored=zizhan_audio_sck::restore(pin,state);
        if(test==4)assert(!restored);else assert(restored&&state.restored);
        if(state.claimed&&test!=4)assert(pin.words[0]==state.config&&(pin.words[4]&2)==(state.io&2));
    }
    {Page ws;ws.words[0]=1;zizhan_audio_sck::State state;
     assert(zizhan_audio_sck::connect_ws(ws,state)&&ws.words[0]==0x2c4&&ws.words[4]==2);
     assert(zizhan_audio_sck::restore(ws,state)&&ws.words[0]==1&&ws.words[4]==0);
     Page invalid;zizhan_audio_sck::State rejected;assert(!zizhan_audio_sck::connect_native(invalid,rejected,0x3c4)&&invalid.writes.empty());}
    puts("PASS production SE15/SCK+WS: exact native6/8mA pads/ownership/latch-first connect,isolate-first restore/readback failure");
}
