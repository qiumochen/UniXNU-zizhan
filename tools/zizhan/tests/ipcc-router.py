#!/usr/bin/env python3
"""Actual shared IPCC router/owner replays; no real GIC, MMIO or DSP boot."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "drivers/PongPower/ZizhanIpcc.cpp").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
source = source.replace('__asm__ volatile("dmb oshld" ::: "memory");', '')
source = source.replace('__asm__ volatile("dsb oshst" ::: "memory");', '')
source = source.replace('__attribute__((weak_import))', '__attribute__((weak))')
harness = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <vector>
#include <string>
#include "ZizhanIpccRouter.h"
#include "QcomScm.h"
constexpr int kIOReturnSuccess=0, kIOReturnNotReady=1, kIOReturnBusy=2,
  kIOReturnBadArgument=3, kIOReturnNoMemory=4, kIOReturnUnsupported=5,
  kIOReturnExclusiveAccess=6, kIOReturnNoResources=7;
constexpr unsigned kIOMapAnywhere=1, kIOMapInhibitCache=2;
static unsigned mode=1, maps, released, attaches, calls;
static int attach_result;
static bool map_missing;
static bool early_event;
static std::vector<std::string> logs;
static uint32_t mmio[1024];
static void (*irq_handler)(void *);
static void *irq_context;
static void IOLog(const char *fmt,...) {
  char line[1024];va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
  logs.emplace_back(line);
}
static bool PE_parse_boot_argn(const char *key,void *value,size_t size) {
  assert(!strcmp(key,"zizhan_ipcc_route") && size==4);memcpy(value,&mode,4);return mode!=0;
}
struct OSData {
  std::vector<uint8_t> bytes;
  size_t getLength(){return bytes.size();}
  const void *getBytesNoCopy(){return bytes.data();}
};
static OSData metadata;
static int gIODTPlane;
struct IORegistryEntry {
  static IORegistryEntry *fromPath(const char *path,int) {
    assert(!strcmp(path,"/chosen"));static IORegistryEntry chosen;return &chosen;
  }
  OSData *getProperty(const char *key) {
    assert(!strcmp(key,"zizhan-ipcc-v1"));return &metadata;
  }
  void release(){}
};
#define OSDynamicCast(type, value) static_cast<type *>(value)
struct IOMemoryMap {
  uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(mmio);}
  void release(){++released;delete this;}
};
struct IODeviceMemory {
  static IODeviceMemory *withRange(uint64_t base,uint64_t bytes) {
    assert(base==0xed18000 && bytes==0x1000);return new IODeviceMemory;
  }
  IOMemoryMap *map(unsigned options) {
    assert(options==(kIOMapAnywhere|kIOMapInhibitCache));++maps;
    return map_missing?nullptr:new IOMemoryMap;
  }
  void release(){delete this;}
};
// Darwin weak_import nullness cannot be modelled by a host stub that has a body; GCC would flag the kernel guard.
#pragma GCC diagnostic ignored "-Waddress"
/* ACTUAL_OWNER */
extern "C" int armcpu_spi_attach(uint32_t id,bool edge,void (*handler)(void *),void *context,
                                   uint32_t *,uint64_t *) {
  assert(id==261 && !edge);++attaches;
  if(attach_result)return attach_result;
  irq_handler=handler;irq_context=context;
  if(early_event)handler(context);
  return 0;
}
static void receive(void *context,uint32_t id) {
  assert(id==*static_cast<uint32_t *>(context));
  assert(mmio[QCOM_IPCC_RECV_CLEAR/4]==id);++calls;
  mmio[QCOM_IPCC_RECV_ID/4]=UINT32_MAX;
}
static void other(void *,uint32_t){assert(false);}
int main(int argc,char **argv) {
  assert(argc==2);std::string test=argv[1];
  uint64_t base=0xed18000;uint32_t size=0x1000,intid=261;
  metadata.bytes.resize(16);memcpy(metadata.bytes.data(),&base,8);
  memcpy(metadata.bytes.data()+8,&size,4);memcpy(metadata.bytes.data()+12,&intid,4);
  uint32_t adsp=0x30002,slpi=0x40000;
  mmio[QCOM_IPCC_RECV_ID/4]=UINT32_MAX;
  if(test=="off"||test=="mode") {
    mode=test=="off"?0:2;
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnUnsupported);
    assert(!maps && !attaches && logs.empty());
  } else if(test=="invalid-id"||test=="null-callback") {
    int result=test=="invalid-id"?zizhan_ipcc_subscribe(0x50002,receive,&adsp):
      zizhan_ipcc_subscribe(adsp,nullptr,&adsp);
    assert(result==kIOReturnBadArgument && !maps && !attaches);
  } else if(test=="metadata"||test=="width") {
    if(test=="metadata")metadata.bytes[0]=1;else metadata.bytes.resize(15);
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnBadArgument);
    assert(!maps && !attaches);
  } else if(test=="map") {
    map_missing=true;
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnNoMemory);
    assert(!attaches && maps==1);
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnNoMemory && maps==1);
  } else if(test=="exclusive") {
    attach_result=kIOReturnExclusiveAccess;
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnExclusiveAccess);
    assert(maps==1 && released==1 && attaches==1);
    assert(zizhan_ipcc_subscribe(slpi,receive,&slpi)==kIOReturnExclusiveAccess);
    assert(maps==1 && attaches==1 && router.used==1);
  } else {
    if(test=="retry") {
      attach_result=kIOReturnNotReady;
      assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==kIOReturnNotReady);
      assert(released==1 && maps==1 && router.used==1);
      attach_result=0;
    } else if(test=="first-event") {
      early_event=true;mmio[QCOM_IPCC_RECV_ID/4]=adsp;
    } else assert(test=="route" || test=="observe");
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==0);
    unsigned initial=attaches;
    assert(zizhan_ipcc_subscribe(adsp,receive,&adsp)==0);
    assert(zizhan_ipcc_subscribe(adsp,other,&adsp)==kIOReturnExclusiveAccess);
    assert(zizhan_ipcc_subscribe(slpi,receive,&slpi)==0);
    assert(attaches==initial && router.used==2 && zizhan_ipcc_stats[6]==2);
    assert(mmio[QCOM_IPCC_RECV_ENABLE/4]==slpi);
    writer=1;
    assert(zizhan_ipcc_subscribe(0,receive,&slpi)==kIOReturnBusy);
    writer=0;
    for(uint32_t id:{adsp,slpi}) {
      mmio[QCOM_IPCC_RECV_ID/4]=id;irq_handler(irq_context);
    }
    const unsigned expected=test=="first-event"?3:2;
    assert(calls==expected && zizhan_ipcc_stats[1]==expected && zizhan_ipcc_stats[3]==expected);
    assert(!zizhan_ipcc_stats[4]);
    if(test=="observe") {
      const unsigned logs_before=logs.size(), maps_before=maps, enables_before=mmio[QCOM_IPCC_RECV_ENABLE/4];
      mode=0;zizhan_ipcc_observe(100,77);assert(logs.size()==logs_before);
      mode=1;owner_state=1;zizhan_ipcc_observe(100,77);assert(logs.size()==logs_before);
      owner_state=2;zizhan_ipcc_observe(100,77);
      assert(logs.size()==logs_before+1);
      const auto &line=logs.back();
      assert(line.find("irq=2 received=2 delivered=2 unknown=0 budget=0 subscriptions=2")!=std::string::npos);
      assert(line.find("adsp_callbacks=77 per_id=0/0/1/1/0")!=std::string::npos);
      assert(line.find("coherent_latch=0")!=std::string::npos);
      zizhan_ipcc_observe(99,77);zizhan_ipcc_observe(10000000099ULL,77);
      assert(logs.size()==logs_before+1);
      for(unsigned i=1;i<20;++i)zizhan_ipcc_observe(100+uint64_t(i)*10000000000ULL,77);
      assert(logs.size()==logs_before+8 && reports==8);
      assert(maps==maps_before && mmio[QCOM_IPCC_RECV_ENABLE/4]==enables_before);
      assert(router.slots[0].delivered==1 && router.slots[1].delivered==1);
    }
  }
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-ipcc-") as temp:
    temp = Path(temp)
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-pthread", "-I" + str(ROOT / "drivers/PongPower")]
    core = temp / "core"
    subprocess.run([*flags, str(ROOT / "tools/zizhan/tests/ipcc-router.cpp"), "-o", str(core)],
                   check=True, timeout=30)
    subprocess.run([str(core)], check=True, timeout=10)
    owner = temp / "owner.cpp"
    owner.write_text(harness.replace("/* ACTUAL_OWNER */", source))
    binary = temp / "owner"
    subprocess.run([*flags, str(owner), "-o", str(binary)], check=True, timeout=30)
    for case in ("off", "mode", "invalid-id", "null-callback", "metadata", "width", "map",
                 "exclusive", "retry", "route", "first-event", "observe"):
        subprocess.run([str(binary), case], check=True, timeout=10)

    # Exercise the actual ADSP selection/registration path. In opt-in mode
    # it must never fall back to a second direct GIC receiver on an error.
    adsp = (ROOT / "drivers/PongPower/PongAdsp.cpp").read_text()
    callback = adsp[adsp.index('void routedIpcc('):adsp.index('void rscIrq(')]
    attach = adsp[adsp.index('bool gIrq;'):adsp.index('// The firmware comes from')]
    adsp_harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "QcomScm.h"
constexpr int kIOReturnNotReady=1,kIOReturnBusy=2,StDoorbells=5;
constexpr uint32_t kSpiIpcc=229,kEvSmp2p=1,kEvGlink=2,kEvAop=4;
static bool routed=true;
static unsigned direct,waits,failed,attempts;
static int failure,reply;
static uint32_t pending;
static int native_adsp_stats[16];
static std::vector<uint32_t> subscriptions,enables;
static int gIpcc;
static void wake(uint32_t bits){pending|=bits;}
static void IOLog(const char*,...){}
static void IOSleep(unsigned ms){assert(ms==250);++waits;}
static void fail(int stage,int error){assert(stage==StDoorbells);++failed;failure=error;}
static void wr(int,unsigned offset,uint32_t id){assert(offset==QCOM_IPCC_RECV_ENABLE);enables.push_back(id);}
static void ipccIrq(void*){assert(false);}
static int zizhan_ipcc_routing_enabled(){return routed;}
extern "C" int armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*) __attribute__((weak));
extern "C" int armcpu_spi_attach(uint32_t id,bool edge,void(*handler)(void*),void*,uint32_t*,uint64_t*) {
 assert(id==261&&!edge&&handler==ipccIrq);++direct;return 0;
}
static int zizhan_ipcc_subscribe(uint32_t id,void(*handler)(void*,uint32_t),void *context) {
 assert(handler && !context);subscriptions.push_back(id);++attempts;
 if(reply==99&&attempts<3)return attempts==1?kIOReturnBusy:kIOReturnNotReady;
 return reply==99?0:reply;
}
// Darwin weak_import nullness cannot be modelled by a host stub that has a body; GCC would flag the kernel guard.
#pragma GCC diagnostic ignored "-Waddress"
/* ACTUAL_ADSP */
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"legacy"))routed=false;
 else if(!strcmp(argv[1],"busy-retry"))reply=99;
 else if(!strcmp(argv[1],"conflict"))reply=6;
 else if(!strcmp(argv[1],"timeout"))reply=kIOReturnNotReady;
 else assert(!strcmp(argv[1],"routed"));
 bool result=attachIrqs();
 if(reply==6||reply==kIOReturnNotReady){
  assert(!result&&!gIrq&&failed==1&&failure==reply&&!direct&&enables.empty());
  if(reply==kIOReturnNotReady)assert(attempts==241&&waits==240);
 }else{
  assert(result&&gIrq&&!failed);
  if(!routed)assert(direct==1&&subscriptions.empty()&&enables==std::vector<uint32_t>({0x30002,0x30000,0}));
  else{
   assert(!direct&&enables.empty());
   assert(subscriptions.back()==0);
   assert(attempts==(reply==99?5:3));
   if(reply==99)assert(waits==2);
   for(uint32_t id:{0x30002u,0x30000u,0u})routedIpcc(nullptr,id);
   assert(pending==(kEvSmp2p|kEvGlink|kEvAop)&&native_adsp_stats[2]==3);
  }
 }
}
'''
    adsp_source = temp / "adsp.cpp"
    adsp_source.write_text(adsp_harness.replace("/* ACTUAL_ADSP */", callback + attach))
    adsp_binary = temp / "adsp"
    subprocess.run([*flags, str(adsp_source), "-o", str(adsp_binary)], check=True, timeout=30)
    for case in ("legacy", "routed", "busy-retry", "conflict", "timeout"):
        subprocess.run([str(adsp_binary), case], check=True, timeout=10)

    sys.path.insert(0, str(ROOT / "scripts"))
    from pong_adt_contract import parse_adt, audit_zizhan_ipcc
    adt = temp / "test.adt"
    env = dict(os.environ)
    env.pop("PONG_HARDWARE_DTB", None)
    subprocess.run([sys.executable, str(ROOT / "scripts/make-pong-adt-ipados18.py"),
                    "--output", str(adt), "--dram-size", "0x300000000", "--skip-image-verification"],
                   env=env, check=True, timeout=30, stdout=subprocess.DEVNULL)
    assert audit_zizhan_ipcc(parse_adt(adt.read_bytes())["/chosen"]["zizhan-ipcc-v1"])["gic_intid"] == 261
    for raw in (b"", bytes(16), struct.pack('<QII', 0xed18000, 0x1000, 229)):
        try:
            audit_zizhan_ipcc(raw)
        except ValueError:
            continue
        raise AssertionError("accepted invalid IPCC input")
print("PASS shared IPCC: router/per-id counters/concurrent publication, 12 owner + 5 actual ADSP controls and package ADT; no device")
