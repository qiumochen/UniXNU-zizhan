#!/usr/bin/env python3
"""Run the actual default-off kernel observer with mocked DT, clock and MMIO.

Only the ARM read barriers are removed for the host compiler; register access,
input gates, logging, worker lifetime and map options use the production code.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
probe = (ROOT / "drivers/ArmDTPlatform/ZizhanHallProbe.h").read_text()
probe = "\n".join(line for line in probe.splitlines()
                  if not line.startswith(("#include", "#pragma")))
probe = probe.replace('__asm__ volatile("dmb oshld" ::: "memory");', '')
# FOLD-01 follow mode (zizhan_hall_observe=2) collaborators; mode 1 never calls them.
LID_STUBS = r"""
extern "C" int zizhan_display_power_state(void) { return 1; }
extern "C" { volatile uint32_t pong_zc_flipped_tag = 1; }
extern "C" void pong_touch_post(uint32_t, uint32_t, uint32_t, uint32_t) {}
extern "C" void zizhan_outer_fold_show(int) {}
"""
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "ZizhanHallState.h"
#include "ZizhanLidPolicy.h"
using wait_result_t = int;
using thread_t = int;
constexpr int THREAD_NULL=0, THREAD_CONTINUE_NULL=0, KERN_SUCCESS=0;
constexpr unsigned kIOMapAnywhere=1, kIOMapInhibitCache=2, kIOMapReadOnly=4;
struct vnode {};
extern "C" { struct vnode *rootvnode = reinterpret_cast<vnode *>(1); }
static unsigned mode, maps, releases, allocations, sleeps, started;
static uint64_t clock_ns;
static uint32_t registers[1024];
static bool null_map, transitions;
static std::vector<std::string> logs;
static void IOLog(const char *fmt,...) {
  char buf[1024];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof buf,fmt,ap);va_end(ap);
  logs.emplace_back(buf);
}
static uint64_t mach_absolute_time() { return clock_ns; }
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *out) { *out=ticks; }
static unsigned sleep_limit;
static void IOSleep(unsigned ms) {
  clock_ns+=uint64_t(ms)*1000000;++sleeps;
  if(sleep_limit&&sleeps>=sleep_limit){throw 18;}  /* follow mode never ends on its own */
  if(transitions) registers[1]=(clock_ns>=30000000000ULL && clock_ns<70000000000ULL)?0:1;
}
static int current_thread(){return 1;}
static void thread_terminate(int) {}
static void thread_block(int) { throw 17; }
static void thread_deallocate(int) {}
static int kernel_thread_start(void(*worker)(void*,int),void *arg,int *thread) {
  ++started;*thread=1;worker(arg,0);return 0;
}
static bool daily_absent;
static bool PE_parse_boot_argn(const char *key, void *out, size_t n) {
  if(!strcmp(key,"zizhan_hall_simulate")){return false;}
  if(!strcmp(key,"zizhan_daily")){unsigned off=0;assert(n==sizeof off);if(daily_absent){return false;}memcpy(out,&off,n);return true;}
  assert(!strcmp(key,"zizhan_hall_observe") && n==sizeof(mode));
  memcpy(out,&mode,n);return mode!=0;
}
#include "ZizhanDailyProfile.h"
#define IONew(type, count) (++allocations, new type[count])
#define IODelete(ptr, type, count) do { delete[] ptr; ptr=nullptr; --allocations; } while(0)
struct OSData {
  std::vector<uint8_t> value;
  size_t getLength(){return value.size();}
  const void *getBytesNoCopy(){return value.data();}
};
#define OSDynamicCast(type, ptr) static_cast<type *>(ptr)
struct IORegistryEntry {
  std::map<std::string, OSData> props;
  OSData *getProperty(const char *key) {
    auto at=props.find(key);return at==props.end()?nullptr:&at->second;
  }
  void release(){}
};
static IORegistryEntry node;
static int gIODTPlane;
struct IOService {
  IORegistryEntry *childFromPath(const char *path,int) {
    assert(!strcmp(path,"arm-io/pong-touch"));return &node;
  }
};
struct FakeMap {
  uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(registers);}
  void release(){++releases;delete this;}
};
struct IODeviceMemory {
  static IODeviceMemory *withRange(uint64_t pa,uint64_t bytes) {
    assert(pa==0xf13e000 && bytes==0x1000);return new IODeviceMemory;
  }
  FakeMap *map(unsigned options) {
    assert(options==(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly));
    ++maps;return null_map?nullptr:new FakeMap;
  }
  void release(){delete this;}
};
/* ACTUAL_PROBE */
static void set(const char *name,uint64_t v,unsigned width=4) {
  auto &out=node.props[name].value;out.resize(width);
  for(unsigned i=0;i<width;++i)out[i]=uint8_t(v>>(i*8));
}
static bool logged(const char *needle) {
  for(auto &line:logs)if(line.find(needle)!=std::string::npos)return true;
  return false;
}
int main(int argc,char **argv) {
  assert(argc==2);std::string which=argv[1];mode=1;
  set("tlmm-base",0xf000000,8);set("tlmm-size",0x200000);set("gpio-offset",0x100000);
  set("hall-gpio",62);set("hall-active-low",1);set("hall-debounce-ms",15);
  const char id[]="st,fts-zizhan-pri";
  node.props["compatible"].value.assign(id,id+sizeof(id));
  registers[1]=1;
  if(which=="off")mode=0;
  else if(which=="daily"){mode=0;daily_absent=true;sleep_limit=200;}  /* no boot args: daily profile = follow mode 2 */
  else if(which=="mode")mode=3;   /* 2 = FOLD-01 follow mode */
  else if(which=="foreign")node.props["compatible"].value[0]='x';
  else if(which=="missing")node.props.erase("hall-gpio");
  else if(which=="width")node.props["hall-gpio"].value.resize(3);
  else if(which=="pin")set("hall-gpio",63);
  else if(which=="base")set("tlmm-base",0xe000000,8);
  else if(which=="root")rootvnode=nullptr;
  else if(which=="map")null_map=true;
  else if(which=="output")registers[0]=1<<9;
  else if(which=="mux")registers[0]=1<<2;
  else if(which=="transitions")transitions=true;
  else assert(which=="baseline");
  IOService platform;
  try {zizhan_hall::start(&platform);} catch(int n) {assert(n==17||(n==18&&which=="daily"));}
  if(which=="daily"){assert(started==1 && maps==1 && logged("display_switch=1") && sleeps==200);return 0;}
  assert(allocations==0);
  if(which=="off")assert(logs.empty() && !maps && !started);
  else if(which=="mode")assert(logged("unsupported-mode") && !maps && !started);
  else if(which=="foreign"||which=="missing"||which=="width"||which=="pin"||which=="base")
    assert(logged("invalid-or-missing-board-inputs") && !maps && !started);
  else if(which=="root")assert(logged("SKIP root-timeout") && !maps && sleeps==1200);
  else if(which=="map")assert(logged("FAIL tlmm-map") && maps==1 && !releases);
  else if(which=="output"||which=="mux") {
    assert(logged("SKIP pin-not-gpio-input") && logged("SKIP baseline=0") && releases==1 && !sleeps);
    assert(registers[0]==(which=="output"?1u<<9:1u<<2));
  } else {
    assert(maps==1 && releases==1 && sleeps==24000 && clock_ns==120000000000ULL);
    assert(logged("PASS observer baseline=1") && logged("fold_qualified=0"));
    assert(logged(which=="transitions"?"transitions=2":"transitions=0"));
    assert(registers[0]==0 && registers[1]==1);
  }
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-hall-probe-") as temp:
    source = Path(temp) / "probe.cpp"
    source.write_text(harness.replace("/* ACTUAL_PROBE */", probe + LID_STUBS))
    binary = Path(temp) / "probe"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "drivers/ArmDTPlatform"), str(source),
                    "-o", str(binary)], check=True, timeout=30)
    for case in ("off", "mode", "foreign", "missing", "width", "pin", "base", "root",
                 "map", "output", "mux", "baseline", "transitions", "daily"):
        # daily: the endless follow loop is cut by a throw, so the worker never frees its state.
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0") if case == "daily" else None
        subprocess.run([str(binary), case], check=True, timeout=10, env=env)
print("PASS actual Hall probe: 14 default-off/daily-profile, DT, map, pin mode, finite clock and transition controls; no device")
