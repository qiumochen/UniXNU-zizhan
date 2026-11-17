#!/usr/bin/env python3
"""Actual writable Normal-NC adapter with controlled unmanaged reserved bytes."""
import os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2] if HERE.name=='tests' else HERE.parents[2]/'UniXNU-zizhan'
SOURCE=ROOT/'drivers/PongPower' if HERE.name=='tests' else HERE
source='\n'.join(l for l in (SOURCE/'ZizhanAudioTone.cpp').read_text().splitlines() if not l.startswith('#include'))
harness=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "ZizhanAudioTone.h"
static bool PE_parse_boot_argn(const char *,void *,size_t){return false;}
using boolean_t=int;using ppnum_t=uint64_t;
static uint64_t tagged=0x1f0000000ull,clock_ticks=0;static uint32_t words[16384];
static bool managed,map_fail;static unsigned maps,releases;
constexpr unsigned kIOMapAnywhere=1,kIOMapWriteCombineCache=2;
extern "C" uint64_t zizhan_audio_dma_map_address(){return tagged;}
extern "C" boolean_t pmap_valid_page(ppnum_t p){assert(p>=0xf0000&&p<0xf0010);return managed;}
static void IOLog(const char *,...){}
static uint64_t mach_absolute_time(){return ++clock_ticks;}
static void absolutetime_to_nanoseconds(uint64_t v,uint64_t *ns){*ns=v*1000;}
struct IOMemoryMap{uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(words);}void release(){++releases;}};
struct IODeviceMemory{static IODeviceMemory *withRange(uint64_t p,uint64_t n){assert(p==0xf0000000&&n==65536);static IODeviceMemory m;return &m;}IOMemoryMap *map(unsigned flags){assert(flags==3);++maps;static IOMemoryMap m;return map_fail?nullptr:&m;}void release(){}};
/* SOURCE */
int main(int argc,char **argv){assert(argc==2);
 assert(!zizhan_audio_tone_clear()&&!maps);
 if(!strcmp(argv[1],"missing")){tagged=0;assert(!zizhan_audio_tone_prepare()&&!maps);return 0;}
 if(!strcmp(argv[1],"managed")){managed=true;assert(!zizhan_audio_tone_prepare()&&!maps);return 0;}
 if(!strcmp(argv[1],"map")){map_fail=true;assert(!zizhan_audio_tone_prepare()&&maps==1&&!releases);map_fail=false;assert(!zizhan_audio_tone_prepare()&&maps==1);return 0;}
 if(!strcmp(argv[1],"foreign")){words[16383]=1;assert(!zizhan_audio_tone_prepare()&&words[16383]==1&&maps==releases&&maps==1);assert(!zizhan_audio_tone_prepare());return 0;}
 assert(zizhan_audio_tone_prepare()&&maps==1&&releases==1);
 assert(!zizhan_audio_tone_prepare()&&maps==1);
 if(!strcmp(argv[1],"changed")){tagged=0x1f0001000ull;assert(!zizhan_audio_tone_clear()&&maps==1);return 0;}
 if(!strcmp(argv[1],"corrupt")){words[10]^=1;uint32_t before=words[10];assert(!zizhan_audio_tone_clear()&&words[10]==before&&maps==2&&releases==2);return 0;}
 assert(!strcmp(argv[1],"pass"));assert(zizhan_audio_tone_clear()&&maps==2&&releases==2);
 for(uint32_t v:words){assert(!v);}assert(!zizhan_audio_tone_clear()&&!zizhan_audio_tone_prepare()&&maps==2);
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-tone-platform-') as temp:
    cpp=Path(temp)/'tone.cpp';cpp.write_text(harness.replace('/* SOURCE */',source));binary=Path(temp)/'tone'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(SOURCE),str(cpp),'-o',str(binary)],check=True,timeout=30)
    for case in ('pass','missing','managed','map','foreign','changed','corrupt'):
        subprocess.run([str(binary),case],check=True,timeout=10)
print('PASS actual CPU toneadapter7controls/NormalNC/unmanaged/exclusive singleclaim/complete readback/no foreign overwrite/strictclear;no hardware')
