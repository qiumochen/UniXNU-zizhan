// SPDX-License-Identifier: MIT
#include "../../../drivers/ArmDTPlatform/ZizhanPowerQuery.h"
#include <assert.h>
#include <string.h>
#include <vector>
using namespace zizhan_power_query;
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"supported-negative")) {
        std::vector<uint32_t> args;
        auto o=observe([&](uint32_t fid,uint32_t value)->uint64_t {
            assert(fid==Version || fid==Features);
            args.push_back(value);
            if(fid==Version){assert(value==0);return 0x10001;}
            if(value==CpuSuspend64)return 3;
            if(value==SystemSuspend64)return 0xffffffffu;
            assert(value==Features || value==SetSuspendMode);
            return 0;
        });
        assert(o.state==State::Pass && o.queries==5 && args.size()==5);
        assert(o.cpu.result==3 && o.system.result==-1 && o.mode.result==0);
        assert(args[0]==0 && args[1]==Features && args[2]==CpuSuspend64 &&
               args[3]==SystemSuspend64 && args[4]==SetSuspendMode);
    } else if(!strcmp(argv[1],"version-gate")) {
        for(uint64_t value:{0xffffffffffffffffull,0xffffffffull,0x20000ull,0x2ull}) {
            unsigned calls=0;
            auto o=observe([&](uint32_t fid,uint32_t arg) {
                ++calls;assert(fid==Version && arg==0);return value;
            });
            assert(calls==1 && o.queries==1 && o.state==State::SkipVersion && !o.cpu.function);
        }
    } else if(!strcmp(argv[1],"errors")) {
        for(uint64_t negative:{0xffffffffffffffffull,0xffffffffull}) {
            auto o=observe([&](uint32_t fid,uint32_t arg)->uint64_t {
                if(fid==Version)return 0x10000;
                assert(fid==Features && arg==Features);return negative;
            });
            assert(o.queries==2 && o.state==State::SkipFeatureApi);
        }
        auto denied=observe([](uint32_t fid,uint32_t arg)->uint64_t {
            if(fid==Version)return 0x10000;
            assert(fid==Features && arg==Features);return 0xfffffffdu;
        });
        assert(denied.queries==2 && denied.state==State::FailFeatureReply);
        auto bad=observe([](uint32_t fid,uint32_t arg)->uint64_t {
            if(fid==Version)return 0x10000;
            assert(fid==Features);return arg==SystemSuspend64?0xfffffffeu:0;
        });
        assert(bad.queries==5 && bad.state==State::FailFeatureReply && bad.system.result==-2);
    } else assert(false);
}
