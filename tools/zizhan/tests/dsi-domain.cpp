// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../../../drivers/PongPower/ZizhanDsiDomain.h"
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
using namespace zizhan_dsi_domain;
struct Io {
    std::map<uint32_t,uint32_t> values{{0x20,0x4c017e71},{0x24,0x6000004b},
        {0x80c,0x84022800},{0xc0c,3},{0x180c,1},{0x83000,0xe0},{0x83058,0}};
    std::vector<uint32_t> reads;
    unsigned fail_at=0,change_at=0;
    bool get(uint32_t address,uint32_t &v) {
        assert(values.count(address));
        reads.push_back(address);
        if(fail_at==reads.size())return false;
        v=values[address];
        if(change_at==reads.size())v^=1;
        return true;
    }
    bool global(uint32_t offset,uint32_t &v) {assert(offset<0x2000);return get(offset,v);}
    bool context(uint32_t offset,uint32_t &v) {
        assert(offset==0x83000||offset==0x83058);return get(offset,v);
    }
    // No write API: production observer must compile using reads only.
};
int main() {
    Board b{0x1000,0x100000,1,3,3,0x2800,0x402,1,0}; // artificial base
    {
        Io io;auto o=observe(b,io);
        assert(o.state==State::IdentityObserved&&o.context_read&&o.reads==11);
        assert(io.reads==std::vector<uint32_t>({0x20,0x24,0x80c,0xc0c,0x180c,
            0x83000,0x83058,0x80c,0xc0c,0x180c,0x83000}));
    }
    for(unsigned i=0;i<9;++i) {
        Board bad=b;
        switch(i) {
            case 0:bad.version=0;break;case 1:bad.group=4;break;case 2:bad.bank=4;break;
            case 3:bad.base=0;break;case 4:bad.span=0x1000;break;case 5:bad.sid=0x8000;break;
            case 6:bad.mask=0x8000;break;case 7:bad.cba2r=0;break;case 8:bad.reserved=1;break;
        }
        Io io;assert(observe(bad,io).state==State::InvalidBoard&&io.reads.empty());
    }
    for(unsigned i=0;i<5;++i) {
        Io io;
        if(i==0)io.values[0x24]=0xe000004b; // unexpected register page layout
        if(i==1)io.values[0x80c]^=0x80000000;
        if(i==2)io.values[0xc0c]=4;
        if(i==3)io.values[0xc0c]=0x10003; // bypass is not the expected CB form
        if(i==4)io.values[0x180c]=0;
        const auto o=observe(b,io);
        assert(!o.context_read&&o.state!=State::IdentityObserved);
        for(auto addr:io.reads)assert(addr<0x2000);
    }
    for(unsigned i=1;i<=11;++i) {
        Io io;io.fail_at=i;auto o=observe(b,io);
        assert(o.state==State::ReadError&&o.reads==i);
    }
    for(unsigned i=8;i<=11;++i) {
        Io io;io.change_at=i;assert(observe(b,io).state==State::Changed);
    }
    {
        Io io;io.values[0x83058]=0xc00001feu;assert(observe(b,io).state==State::Faulted);
        io.values[0x83058]=0x400;assert(observe(b,io).state==State::IdentityObserved);
        io.values[0x83058]=0;io.values[0x83000]|=1;
        assert(observe(b,io).state==State::TranslationActive);
    }
    puts("PASS: fixed display-domain reads, prerequisite gates, every read failure, mutable snapshots, faults and translation; no writes");
}
