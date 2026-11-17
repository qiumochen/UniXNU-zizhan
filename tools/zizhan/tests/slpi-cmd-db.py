#!/usr/bin/env python3
"""Actual cmd-db capture core/platform, synthetic readonly region only."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'drivers/PongPower/ZizhanCmdDb.cpp').read_text()
source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
source=source.replace('__asm__ volatile("dmb oshld" ::: "memory");','')
harness=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include "ZizhanCmdDbCapture.h"
#include "ZizhanRpmhReceipt.h"
static unsigned query_mode=0,query_calls=0;
static unsigned mode=1,maps,releases,threads,allocs,frees;static bool wrong_board,wrong_base,thread_fail,map_fail,alloc_fail;
static uint8_t region[zizhan_cmd_db::Bytes];static std::vector<std::string> logs;
static int gIODTPlane;
constexpr unsigned kIOMapAnywhere=1,kIOMapInhibitCache=2,kIOMapReadOnly=4;
using thread_t=void*;using wait_result_t=int;constexpr int KERN_SUCCESS=0;
static bool PE_parse_boot_argn(const char*name,void*out,size_t n){assert(!strcmp(name,"zizhan_cmd_db_observe")&&n==4);memcpy(out,&mode,4);return mode!=0;}
static void IOLog(const char*fmt,...){char s[256];va_list a;va_start(a,fmt);vsnprintf(s,sizeof s,fmt,a);va_end(a);logs.emplace_back(s);}
static void *current_thread(){return (void*)1;}
static void thread_set_thread_name(void*,const char*){}
static int kernel_thread_start(void(*f)(void*,int),void*ctx,thread_t*out){++threads;if(thread_fail)return 1;f(ctx,0);*out=(void*)1;return 0;}
static void thread_deallocate(thread_t t){assert(t);}
static uint64_t mach_absolute_time(){return 0;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*n){*n=t;}
static void IOSleep(unsigned n){assert(n==1||(mode==2&&n==10000));}
static void *IOMalloc(size_t n){assert(n==sizeof region);++allocs;return alloc_fail?nullptr:malloc(n);}
static void IOFree(void*p,size_t n){assert(p&&n==sizeof region);++frees;free(p);}
struct OSData{
 bool base;
 size_t getLength(){return base?8:strlen(wrong_board?"other":"st,fts-zizhan-pri")+1;}
 const void *getBytesNoCopy(){static uint64_t physical;physical=wrong_base?0x80880000:0x80860000;return base?(const void*)&physical:(const void*)(wrong_board?"other":"st,fts-zizhan-pri");}
};
struct IORegistryEntry{
 static IORegistryEntry *fromPath(const char*p,int){assert(!strcmp(p,"/arm-io/pong-touch"));static IORegistryEntry node;return &node;}
 OSData *getProperty(const char*p){static OSData base{true},compatible{false};assert(!strcmp(p,"compatible")||!strcmp(p,"cmd-db-base"));return !strcmp(p,"compatible")?&compatible:&base;}
 void release(){}
};
#define OSDynamicCast(type,p) static_cast<type*>(p)
struct IOMemoryMap{uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(region);}void release(){++releases;delete this;}};
struct IODeviceMemory{
 static IODeviceMemory *withRange(uint64_t base,size_t bytes){assert(base==0x80860000&&bytes==sizeof region);return new IODeviceMemory;}
 IOMemoryMap *map(unsigned flags){assert(flags==7);++maps;return map_fail?nullptr:new IOMemoryMap;}
 void release(){delete this;}
};
static const char *const names[] = {"xo.lvl","lcx.lvl","lmx.lvl","CE0","SN3","SN0","SH0","MC0","ACV"};
static void w16(unsigned at, unsigned v) { region[at] = v; region[at + 1] = v >> 8; }
static void w32(unsigned at, uint32_t v) { w16(at, v); w16(at + 2, v >> 16); }
static unsigned entry(unsigned n) { return 144 + 24 * n; }
static unsigned aux(unsigned n) { return 512 + 16 * n; }
static void fixture(void) {
    memset(region, 0, sizeof region); w32(0, 1); w32(4, 0x0c0330db);
    w16(8, 3); w16(10, 0); w16(12, 512 - 144); w16(14, 3);
    w16(24, 5); w16(26, 3 * 24); w16(28, 512 - 144); w16(30, 6);
    const unsigned vcd[] = {2,1,2,0,0,0};
    for (unsigned n = 0; n < 9; ++n) {
        memcpy(region + entry(n), names[n], strlen(names[n]));
        w32(entry(n) + 16, ((n < 3 ? 3u : 5u) << 16) + 0x100 + 4 * n);
        w16(entry(n) + 22, 16 * n);
        if (n && n < 3) {
            w16(entry(n) + 20, 12);
            w16(aux(n) + 2, 64); w16(aux(n) + 4, 128);
            w16(aux(n) + 6, 384); w16(aux(n) + 8, 512);
        } else if (n >= 3) {
            w16(entry(n) + 20, 8); w32(aux(n), 1000); w16(aux(n) + 4, 16);
            region[aux(n) + 6] = vcd[n - 3];
        }
    }
}

extern "C" bool pong_rpmh_completed_request(uint32_t address,zizhan_rpmh::CompletedRequest *out){
 assert(mode==2&&out);++query_calls;*out={};
 if(query_mode==1||(query_mode==2&&query_calls%2==0))return false;
 *out={address,query_calls-1,uint64_t(query_calls)};return true;
}
/* ACTUAL_SOURCE */
int main(int argc,char**argv){
 assert(argc==2);std::string test=argv[1];
 if(test=="off")mode=0;
 else if(test=="unsupported")mode=3;
 else if(test=="receipts"||test=="receipts-unknown"||test=="receipts-mixed"||test=="receipts-invalid")mode=2,query_mode=test=="receipts-unknown"?1u:test=="receipts-mixed"?2u:0u;
 else if(test=="board")wrong_board=true;
 else if(test=="base")wrong_base=true;
 else if(test=="thread")thread_fail=true;
 else if(test=="map")map_fail=true;
 else if(test=="alloc")alloc_fail=true;
 else assert(test=="invalid"||test=="valid");
 if(test=="valid"||(mode==2&&test!="receipts-invalid"))fixture();
 uint8_t original[sizeof region];memcpy(original,region,sizeof region);
 zizhan_cmd_db_probe();assert(!memcmp(original,region,sizeof region));
 if(test=="off"||test=="unsupported")assert(logs.empty()&&!maps&&!threads);
 else if(test=="board"||test=="base")assert(!maps&&!threads&&logs.back().find("SKIP board-resource")!=std::string::npos);
 else if(test=="thread")assert(!maps&&threads==1&&logs.back().find("FAIL thread-create")!=std::string::npos);
 else if(test=="map")assert(maps==1&&!releases&&!allocs&&logs.back().find("FAIL readonly-map")!=std::string::npos);
 else if(test=="alloc")assert(maps==1&&releases==1&&allocs==2&&!frees&&logs.back().find("FAIL allocation")!=std::string::npos);
 else if(mode==2){
  assert(maps==1&&releases==1&&allocs==2&&frees==2);
  unsigned raw=0,rows=0;std::string terminal;
  for(const auto &line:logs){if(line.find("ZIZHAN_CMD_DB_DATA:")==0)++raw;if(line.find("ZIZHAN_SLPI_REQUEST: resource=")==0)++rows;if(line.find("ZIZHAN_SLPI_REQUEST: PASS")==0)terminal=line;}
  assert(!raw);
  if(test=="receipts-invalid")assert(!query_calls&&!rows&&terminal.empty()&&logs.back().find("FAIL snapshot")!=std::string::npos);
  else{assert(query_calls==9&&rows==9&&logs.back().find("PASS snapshot")!=std::string::npos);unsigned known=query_mode==1?0u:query_mode==2?5u:9u;
   char expected[96];snprintf(expected,sizeof expected,"PASS queried=9 known=%u unknown=%u",known,9-known);assert(terminal.find(expected)!=std::string::npos&&terminal.find("inherited_baseline=0 votes=0 slpi_start=0")!=std::string::npos);}
 }
 else if(test=="valid"){
  assert(maps==1&&releases==1&&allocs==2&&frees==2&&logs.back().find("PASS snapshot")!=std::string::npos);
  unsigned chunks=0,resources=0;uint8_t restored[sizeof region]={};
  const char *hex="0123456789abcdef";
  for(const auto&line:logs){
   if(line.find("ZIZHAN_CMD_DB: resource=")==0)++resources;
   if(line.find("ZIZHAN_CMD_DB_DATA:")!=0)continue;
   unsigned offset=0;char data[129];assert(sscanf(line.c_str(),"ZIZHAN_CMD_DB_DATA: offset=%u hex=%128s",&offset,data)==2);
   assert(offset==chunks*64&&strlen(data)==128);
   for(unsigned i=0;i<64;++i){const char*a=strchr(hex,data[2*i]),*b=strchr(hex,data[2*i+1]);assert(a&&b);restored[offset+i]=uint8_t(((a-hex)<<4)|(b-hex));}
   ++chunks;
  }
  assert(chunks==2048&&resources==9&&!memcmp(restored,region,sizeof region));
 }else {
  assert(maps==1&&releases==1&&allocs==2&&frees==2&&logs.back().find("FAIL snapshot")!=std::string::npos);
  unsigned chunks=0;for(const auto&line:logs)if(line.find("ZIZHAN_CMD_DB_DATA:")==0)++chunks;
  assert(chunks==2048); // Stable invalid data exported, still FAIL/no plan.
 }
 unsigned before=threads;zizhan_cmd_db_probe();assert(threads==before);
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-cmd-db-') as temp:
 temp=Path(temp)
 obj=temp/'plan.o'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-fsanitize=address,undefined','-I'+str(ROOT/'drivers/PongPower'),'-c',str(ROOT/'drivers/PongPower/ZizhanSlpiPower.c'),'-o',str(obj)],check=True,timeout=30)
 flags=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'drivers/PongPower')]
 binary=temp/'core'
 subprocess.run([*flags,str(ROOT/'tools/zizhan/tests/slpi-cmd-db.cpp'),str(obj),'-o',str(binary)],check=True,timeout=30)
 subprocess.run([str(binary)],check=True,timeout=10)
 cpp=temp/'platform.cpp';cpp.write_text(harness.replace('/* ACTUAL_SOURCE */',source));binary=temp/'platform'
 subprocess.run([*flags,str(cpp),str(obj),'-o',str(binary)],check=True,timeout=30)
 for case in ('off','unsupported','board','base','thread','map','alloc','invalid','valid','receipts','receipts-unknown','receipts-mixed','receipts-invalid'):
  subprocess.run([str(binary),case],check=True,timeout=10)
print('PASS actual cmd-db platform:9 controls,readonly mapping/default-off/resource rejection,finite failure paths and no region mutation')
