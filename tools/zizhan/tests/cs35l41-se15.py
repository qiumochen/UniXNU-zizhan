#!/usr/bin/env python3
"""Actual default-off SE15 platform observer, synthetic mapped pages only."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "drivers/PongPower/ZizhanSe15.cpp").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
source = source.replace('__asm__ volatile("dmb oshld" ::: "memory");', '')
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <map>
#include <vector>
#include <string>
#include "GeniI2c.h"
#include "ZizhanSe15State.h"
static unsigned mode=1,audio,maps,threads,releases;
static bool board_ok=true,thread_fail;
static uint64_t fail_base;
static std::map<uint64_t,std::vector<uint32_t>> pages;
static std::vector<std::string> logs;
constexpr unsigned kIOMapAnywhere=1,kIOMapInhibitCache=2,kIOMapReadOnly=4;
static int gIODTPlane;
using thread_t=void*;using wait_result_t=int;
constexpr int KERN_SUCCESS=0;
static bool PE_parse_boot_argn(const char *key,void *out,size_t bytes){
 assert(bytes==4);if(!strcmp(key,"zizhan_se15_observe")){memcpy(out,&mode,4);return mode!=0;}
 assert(!strcmp(key,"pong_audio"));memcpy(out,&audio,4);return audio!=0;
}
static void IOLog(const char *fmt,...){char text[512];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);logs.emplace_back(text);}
static void *current_thread(){return (void*)1;}
static void thread_set_thread_name(void*,const char*){}
static int kernel_thread_start(void(*worker)(void*,int),void *ctx,thread_t *out){++threads;if(thread_fail)return 1;worker(ctx,0);*out=(void*)1;return 0;}
static void thread_deallocate(thread_t t){assert(t);}
struct OSData{
 size_t getLength(){return strlen(board_ok?"st,fts-zizhan-pri":"st,fts-other-pri")+1;}
 const void *getBytesNoCopy(){return board_ok?"st,fts-zizhan-pri":"st,fts-other-pri";}
};
struct IORegistryEntry{
 static IORegistryEntry *fromPath(const char *path,int){assert(!strcmp(path,"/arm-io/pong-touch"));static IORegistryEntry n;return &n;}
 OSData *getProperty(const char *key){assert(!strcmp(key,"compatible"));static OSData d;return &d;}
 void release(){}
};
#define OSDynamicCast(type,value) static_cast<type*>(value)
struct IOMemoryMap{
 uint64_t base;
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(pages[base].data());}
 void release(){++releases;delete this;}
};
struct IODeviceMemory{
 uint64_t base;
 static IODeviceMemory *withRange(uint64_t base,uint64_t bytes){
  assert(pages.count(base)&&pages[base].size()*4==bytes);return new IODeviceMemory{base};
 }
 IOMemoryMap *map(unsigned flags){assert(flags==7);++maps;return base==fail_base?nullptr:new IOMemoryMap{base};}
 void release(){delete this;}
};
/* ACTUAL_SOURCE */
int main(int argc,char **argv){
 assert(argc==2);std::string test=argv[1];
 for(uint64_t base:{0x162000,0x12e000,0xf138000,0xf139000,0xf101000})pages[base].resize(1024);
 pages[0x880000].resize(4096);
 pages[0x162000][4]=0x16;pages[0xf138000][0]=pages[0xf139000][0]=7;
 pages[0xf101000][0]=0x200;pages[0xf101000][1]=2;
 pages[0x880000][GENI_FW_REVISION/4]=GENI_PROTO_I2C<<8;
 if(test=="off")mode=0;
 else if(test=="unsupported")mode=2;
 else if(test=="audio")audio=2;
 else if(test=="board")board_ok=false;
 else if(test=="thread")thread_fail=true;
 else if(test=="clock")pages[0x162000][4]=0;
 else if(test=="halt")pages[0x12e000][3]=0x80000000;
 else if(test=="pins")pages[0xf138000][0]=3;
 else if(test=="direction")pages[0xf139000][0]|=1<<9;
 else if(test=="reset")pages[0xf101000][1]=0;
 else if(test=="reset-mux")pages[0xf101000][0]|=4;
 else if(test=="map")fail_base=0x162000;
 else if(test=="engine-map")fail_base=0x880000;
 else assert(test=="ready");
 auto original=pages;
 zizhan_se15_probe();
 assert(pages==original); // zero writes to every mapped byte, all branches
 if(test=="off"||test=="unsupported")assert(logs.empty()&&!maps&&!threads);
 else if(test=="audio"||test=="board")assert(!maps&&!threads&&logs.back().find("SKIP")!=std::string::npos);
 else if(test=="thread")assert(!maps&&threads==1&&logs.back().find("FAIL thread-create")!=std::string::npos);
 else if(test=="map")assert(maps==1&&!releases&&logs.back().find("FAIL resource-map")!=std::string::npos);
 else if(test=="engine-map")assert(maps==6&&releases==5&&logs.back().find("FAIL engine-map")!=std::string::npos);
 else if(test!="ready")assert(maps==5&&releases==5&&logs.back().find("se_mapped=0 i2c_commands=0")!=std::string::npos);
 else assert(maps==6&&releases==6&&logs.back().find("supply-and-controller-lease-unverified")!=std::string::npos);
 if(mode==1&&!audio&&board_ok){const unsigned before=threads;zizhan_se15_probe();assert(threads==before);}
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-se15-") as temp:
    path = Path(temp)
    cpp = path / "observer.cpp"
    cpp.write_text(harness.replace("/* ACTUAL_SOURCE */", source))
    binary = path / "observer"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
                    str(cpp), "-o", str(binary)], check=True, timeout=30)
    for case in ("off", "unsupported", "audio", "board", "thread", "clock", "halt", "pins", "direction",
                 "reset", "reset-mux", "map", "engine-map", "ready"):
        subprocess.run([str(binary), case], check=True, timeout=10)
print("PASS actual SE15 observer: 14 controls, readonly maps/zero writes, precondition skips and no I2C/amp qualification")
