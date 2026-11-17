/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <utility>
#include "ZizhanAmpReset.h"
struct Pin {
    uint32_t cfg=1, io=0;
    bool config_fail=false, release_fail=false, assert_fail=false;
    std::vector<std::pair<unsigned,uint32_t>> writes;
    uint32_t read(unsigned at){assert(at==0||at==4);return at?io:cfg;}
    void write(unsigned at,uint32_t value){
        assert((at==0&&(value&0x3ff)==0x3c0)||(at==4&&(value==0||value==2)));
        writes.emplace_back(at,value);
        if(at==0){if(!config_fail)cfg=value;}
        else if(value==2)io=release_fail?2:3;
        else io=assert_fail?1:0;
    }
};
int main(){
    using namespace zizhan_amp_reset;
    for(unsigned test=0;test<7;++test){
        Pin p;State s;unsigned waits=0;auto pause=[&](unsigned us){assert(us==2100);++waits;};
        if(test==1)p.cfg=0x204;
        if(test==2)p.cfg|=1<<11;
        if(test==3)p.config_fail=true;
        if(test==4)p.release_fail=true;
        if(test==5)p.assert_fail=true;
        if(test==6)p.cfg|=0x80000000;
        const Result result=release(p,s,pause);
        if(test==1||test==2){assert(result==ForeignOwner&&!s.claimed&&p.writes.empty()&&!waits);}
        else if(test==3||test==5)assert(result==AssertFailed&&s.claimed&&!s.released&&waits==1);
        else if(test==4)assert(result==ReleaseFailed&&s.claimed&&!s.released&&waits==2);
        else {assert(result==Ok&&s.claimed&&s.released&&waits==2);assert((p.cfg&0x3ff)==0x3c0);}
        bool held=assert_and_retain(p,s);
        if(test==3||test==5)assert(!held);else assert(held&&!s.released);
        if(test==6)assert(p.cfg&0x80000000);
    }
    puts("PASS actual shared-reset sequence: ownership,16mA/pad preservation, pulse/release/readback and final assertion");
}
