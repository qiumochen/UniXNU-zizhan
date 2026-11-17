#!/usr/bin/env python3
"""Actual read-only CPU adapter against a controlled reserved buffer."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
source='\n'.join(l for l in (R/'drivers/PongPower/ZizhanAudioZero.cpp').read_text().splitlines() if not l.startswith('#include'))
harness=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "ZizhanAudioZero.h"
using boolean_t=int;using ppnum_t=uint64_t;
static uint64_t tagged=0x1f0000000ull;static uint64_t words[8192];
static bool managed,map_fail;static unsigned calls,maps,releases;
constexpr unsigned kIOMapAnywhere=1,kIOMapWriteCombineCache=2,kIOMapReadOnly=4;
extern "C" uint64_t zizhan_audio_dma_map_address(){return tagged;}
extern "C" boolean_t pmap_valid_page(ppnum_t p){assert(p>=0xf0000&&p<0xf0010);++calls;return managed;}
static void IOLog(const char *,...){}
struct IOMemoryMap{uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(words);}void release(){++releases;}};
struct IODeviceMemory{static IODeviceMemory *withRange(uint64_t p,uint64_t n){assert(p==0xf0000000&&n==65536&&calls==16);static IODeviceMemory m;return &m;}IOMemoryMap *map(unsigned flags){assert(flags==7);++maps;static IOMemoryMap m;return map_fail?nullptr:&m;}void release(){}};
/* SOURCE */
int main(int argc,char **argv){assert(argc==2);bool expected=false;
 if(!strcmp(argv[1],"pass"))expected=true;
 else if(!strcmp(argv[1],"missing"))tagged=0;
 else if(!strcmp(argv[1],"managed"))managed=true;
 else if(!strcmp(argv[1],"map"))map_fail=true;
 else if(!strcmp(argv[1],"nonzero"))words[8191]=1;
 else assert(false);
 assert(zizhan_audio_zero_verify()==expected);
 if(!tagged)assert(!calls&&!maps&&!releases);
 else if(managed)assert(calls==1&&!maps&&!releases);
 else{assert(maps==1&&releases==(map_fail?0u:1u));if(!map_fail)assert(calls==32);}
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-zero-platform-") as temp:
    cpp=Path(temp)/"zero.cpp";cpp.write_text(harness.replace('/* SOURCE */',source));binary=Path(temp)/"zero"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(cpp),"-o",str(binary)],check=True,timeout=30)
    for case in ('pass','missing','managed','map','nonzero'):
        subprocess.run([str(binary),case],check=True,timeout=10)
print('PASS actual CPU zero adapter5controls/ownedaddress/managed-refusal-before-map/readonlyNC/fullzero/resource lifetime;no hardware')
