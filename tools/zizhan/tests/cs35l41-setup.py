#!/usr/bin/env python3
"""Production setup core and actual optional platform entry; no hardware."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "drivers/PongPower/ZizhanSe15Prepare.cpp").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
for asm in ('__asm__ volatile("dmb oshld" ::: "memory");', '__asm__ volatile("dsb oshst" ::: "memory");'):
    source = source.replace(asm, '')
harness = r'''
#include <assert.h>
#include <stdlib.h>
#include "ZizhanAmpSha256.h"
static void *IOMalloc(size_t n){return malloc(n);}
static void IOFree(void *p,size_t){free(p);}
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <map>
#include <vector>
#include <string>
#include "ZizhanSe15Setup.h"
#include "ZizhanGeniTiming.h"
#include "ZizhanCs35l41.h"
#include "ZizhanAmpReset.h"
#include "ZizhanAudioSilent.h"
#include "ZizhanAudioSck.h"
#include "ZizhanAudioDataPin.h"
static unsigned mode=1,audio,observe,maps,threads,releases;
static bool board_ok=true,thread_fail,ram_missing,ram_short,coeff_missing,cal_missing,cal_bad;
static unsigned property_size=34068;
static uint8_t ram_input[34068];
static bool chosen_data;
static uint64_t fail_base,time_us;
static std::map<uint64_t,std::vector<uint32_t>> pages;
static std::vector<std::string> logs;
using IOOptionBits=unsigned;
constexpr unsigned kIOMapAnywhere=1,kIOMapInhibitCache=2,kIOMapReadOnly=4;
static int gIODTPlane;
using thread_t=void*;using wait_result_t=int;
constexpr int KERN_SUCCESS=0;
static bool PE_parse_boot_argn(const char *key,void *out,size_t bytes){if(!strcmp(key,"zizhan_audio_checkpoint")||!strcmp(key,"zizhan_pulse_hold")||!strcmp(key,"zizhan_pulse_dry")||!strcmp(key,"zizhan_pulse_nopup")||!strcmp(key,"zizhan_amp_lvl"))return false;
 assert(bytes==4);unsigned v=0;
 if(!strcmp(key,"zizhan_se15_prepare"))v=mode;
 else if(!strcmp(key,"pong_audio"))v=audio;
 else if(!strcmp(key,"zizhan_cal_t")||!strcmp(key,"zizhan_cal_b"))v=cal_missing?0:cal_bad?0xffffffu:12001;
 else {assert(!strcmp(key,"zizhan_se15_observe"));v=observe;}
 memcpy(out,&v,4);return v!=0;
}
static void IOLog(const char *fmt,...){char text[512];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);logs.emplace_back(text);}
extern "C" __attribute__((weak)) int pong_klog_flush(int){return -1;}
static void *current_thread(){return (void*)1;}
static void thread_set_thread_name(void*,const char*){}
static int kernel_thread_start(void(*worker)(void*,int),void *ctx,thread_t *out){++threads;if(thread_fail)return 1;worker(ctx,0);*out=(void*)1;return 0;}
static void thread_deallocate(thread_t t){assert(t);}
static uint64_t mach_absolute_time(){return time_us*1000;}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *ns){*ns=ticks;}
static void IODelay(unsigned us){assert(us==100||us==2100);time_us+=us;}
struct OSData{
 size_t getLength(){if(chosen_data)return property_size-(ram_short?1:0);return strlen(board_ok?"st,fts-zizhan-pri":"other")+1;}
 const void *getBytesNoCopy(){if(chosen_data)return ram_input;return board_ok?"st,fts-zizhan-pri":"other";}
};
struct IORegistryEntry{
 static IORegistryEntry *fromPath(const char *path,int){assert(!strcmp(path,"/arm-io/pong-touch")||!strcmp(path,"/chosen"));chosen_data=!strcmp(path,"/chosen");static IORegistryEntry n;return &n;}
 OSData *getProperty(const char *key){assert(!strcmp(key,"compatible")||!strcmp(key,"zizhan-cs35l41-protection")||!strcmp(key,"zizhan-cs35l41-prot-t")||!strcmp(key,"zizhan-cs35l41-prot-b"));
 property_size=!strcmp(key,"zizhan-cs35l41-prot-t")?5060:!strcmp(key,"zizhan-cs35l41-prot-b")?5148:34068;
 static OSData d;return (chosen_data&&ram_missing)||(coeff_missing&&property_size!=34068)?nullptr:&d;}
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
 static IODeviceMemory *withRange(uint64_t base,uint64_t bytes){assert(pages.count(base)&&pages[base].size()*4==bytes);return new IODeviceMemory{base};}
 IOMemoryMap *map(unsigned flags){
  bool writable=base==0x162000||base==0xf138000||base==0xf139000||(base==0x880000&&mode>=2)||(base==0xf101000&&mode>=3);
  assert(flags==(writable?3u:7u));++maps;return base==fail_base?nullptr:new IOMemoryMap{base};
 }
 void release(){delete this;}
};
/* ACTUAL_SOURCE */
int main(int argc,char **argv){
 assert(argc==2);std::string test=argv[1];
 for(uint64_t base:{0x162000,0x12e000,0x133000,0xf138000,0xf139000,0xf101000})pages[base].resize(1024);
 pages[0x880000].resize(4096);
 pages[0xf138000][0]=pages[0xf139000][0]=1;
 pages[0xf101000][0]=1;pages[0x880000][0x68/4]=3<<8;
 pages[0x880000][0xe24/4]=pages[0x880000][0xe28/4]=(32u<<24)|(16u<<16);
 if(test=="native-data-missing"||test=="native-data-proof"||test=="native-data-input"){
  mode=35;ZizhanPrmState lease={};ZizhanAudioSilentReceipt receipt={};
  lease.phase=ZizhanPrmClockReleaseReady;lease.native_wire=lease.native_framework=lease.clock_mode=1;lease.framework_ready=lease.lease_held=lease.clock_held=lease.request_ack=lease.clock_request_ack=1;lease.sent=lease.acknowledged=3;
  const ZizhanAudioTransfer transfer={nullptr,[](void *,unsigned){assert(false);return 0;},nullptr};
  if(test=="native-data-proof")lease.clock_request_ack=0;
  zizhan_amp_native_data_run(&lease,&receipt,test=="native-data-missing"?nullptr:&transfer);
  assert(!threads&&!receipt.amp_pass&&!receipt.data_connected);
  if(test=="native-data-proof")assert(!maps&&!receipt.invoked&&receipt.release_safe);
  else if(test=="native-data-missing")assert(!maps&&receipt.invoked&&receipt.release_safe);
  else assert(maps==7&&receipt.invoked&&receipt.reset_asserted&&receipt.pin_restored&&receipt.release_safe);
  return 0;
 }
 if(test=="off")mode=0;
 else if(test=="unsupported")mode=20;
 else if(test=="ram-missing")mode=13,ram_missing=true;
 else if(test=="ram-short")mode=13,ram_short=true;
 else if(test=="identity-reset-fail")mode=3;
 else if(test=="snapshot-reset-fail")mode=4;
 else if(test=="safeoff-reset-fail")mode=5;
 else if(test=="handoff-reset-fail")mode=6;
 else if(test=="rx-reset-fail")mode=7;
 else if(test=="source-reset-fail")mode=8;
 else if(test=="config-reset-fail")mode=9;
 else if(test=="owned-reset-fail")mode=10;
 else if(test=="b2-reset-fail")mode=11;
 else if(test=="trim-reset-fail")mode=12;
 else if(test=="ram-reset-fail")mode=13;
 else if(test=="header-reset-fail")mode=14;
 else if(test=="burst-reset-fail")mode=15;
 else if(test=="coeff-reset-fail")mode=16;
 else if(test=="run-reset-fail")mode=17;
 else if(test=="mpu-reset-fail")mode=18;
 else if(test=="observed-reset-fail")mode=19;
 else if(test=="silent-proof"||test=="silent-missing"||test=="silent-reset"||test=="silent-once")mode=20;
 else if(test=="native-core-run-input")mode=34;
 else if(test=="native-core-input")mode=33;
 else if(test=="format-input")mode=32;
 else if(test=="rx-input")mode=31;
 else if(test=="rates-input")mode=30;
 else if(test=="prerun-missing"||test=="prerun-bad"||test=="prerun-input")mode=29;
 else if(test=="coeff-missing")mode=16,coeff_missing=true;
 else if(test=="timing")mode=2;
 else if(test=="clock-invalid"){mode=2;pages[0x12e000][0x18/4]=0x100;}
 else if(test=="fifo"){mode=2;pages[0x880000][0xe24/4]=0;}
 else if(test=="audio")audio=2;
 else if(test=="observe")observe=1;
 else if(test=="board")board_ok=false;
 else if(test=="thread")thread_fail=true;
 else if(test=="clock")pages[0x12e000][3]=0x80000000;
 else if(test=="protocol")pages[0x880000][0x68/4]=0;
 else if(test=="busy")pages[0x880000][0x40/4]=1;
 else if(test=="pins")pages[0xf138000][0]=4;
 else if(test=="reset")pages[0xf101000][1]=1;
 else if(test=="map")fail_base=0x162000;
 else if(test=="engine-map")fail_base=0x880000;
 else assert(test=="ready");
 if(test=="silent-proof"||test=="silent-missing"||test=="silent-reset"||test=="silent-once"||test=="prerun-missing"||test=="prerun-bad"||test=="prerun-input"||test=="rates-input"||test=="rx-input"||test=="format-input"||test=="native-core-input"||test=="native-core-run-input"){
  ZizhanPrmState lease={};ZizhanAudioSilentReceipt receipt={};
  lease.phase=ZizhanPrmClockReleaseReady;lease.native_wire=lease.native_framework=lease.clock_mode=1;
  lease.framework_ready=lease.lease_held=lease.clock_held=lease.request_ack=lease.clock_request_ack=1;
  lease.sent=lease.acknowledged=3;
  if(test=="silent-proof")lease.clock_request_ack=0;
  if(test=="silent-missing")ram_missing=true;
  if(test=="prerun-missing"){cal_missing=true;}if(test=="prerun-bad")cal_bad=true;
  if(mode==34)zizhan_amp_native_core_run(&lease,&receipt);else if(mode==33)zizhan_amp_native_core_capture(&lease,&receipt);else if(mode==32)zizhan_amp_asp_format_run(&lease,&receipt);else if(mode==31)zizhan_amp_asp_rx_run(&lease,&receipt);else if(mode==30)zizhan_amp_native_rates_run(&lease,&receipt);else if(mode==29)zizhan_amp_native_protection_run(&lease,&receipt);else zizhan_amp_silent_clock_run(&lease,&receipt);
  assert(!threads&&!receipt.amp_pass);
  if(test=="silent-proof")assert(!maps&&!receipt.invoked&&receipt.release_safe);
  else if(test=="silent-missing")assert(!maps&&receipt.invoked&&receipt.release_safe);
  else if(test=="prerun-missing"||test=="prerun-bad")assert(!maps&&receipt.invoked&&receipt.release_safe);
  else{assert(maps==7&&receipt.invoked&&receipt.reset_asserted&&receipt.pin_restored&&receipt.release_safe);
   assert(pages[0xf101000][1]==0&&!receipt.pin_connected);}
  if(test=="silent-once"){unsigned before=maps;zizhan_amp_silent_clock_run(&lease,&receipt);assert(maps==before&&!receipt.invoked&&!receipt.release_safe);}
  return 0;
 }
 auto before=pages;zizhan_se15_prepare_probe();
 for(const auto &p:pages){
  if(ram_missing||ram_short||coeff_missing){assert(p.second==before[p.first]);continue;}
  if(p.first==0x162000){auto expected=before[p.first];if((mode>=1&&mode<=19)&&!audio&&!observe&&board_ok&&!thread_fail&&!fail_base&&test!="reset")expected[4]|=0x1f;
   if(test=="engine-map")expected[4]|=0x1f;
   assert(p.second==expected);
  }else if((p.first==0xf138000||p.first==0xf139000)&&(test=="ready"||test=="timing"||test=="fifo"||(test=="identity-reset-fail"||test=="snapshot-reset-fail"||(test=="safeoff-reset-fail"||(test=="handoff-reset-fail"||(test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail"))))))))))))))))){
   auto expected=before[p.first];expected[0]=7;assert(p.second==expected);
  }else if(p.first==0x880000&&(test=="timing"||(test=="identity-reset-fail"||test=="snapshot-reset-fail"||(test=="safeoff-reset-fail"||(test=="handoff-reset-fail"||(test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail"))))))))))))))))){
   auto expected=before[p.first];expected[GENI_CLK_CFG/4]=0x71;
   expected[GENI_SCL_COUNTERS/4]=(10u<<20)|(11u<<10)|26;
   expected[GENI_TX_PACK0/4]=expected[GENI_RX_PACK0/4]=0x7f8fe;
   expected[GENI_TX_PACK1/4]=expected[GENI_RX_PACK1/4]=0xffefe;
   expected[GENI_M_IRQ_CLEAR/4]=0xffffffff;expected[GENI_M_IRQ_EN/4]=GENI_M_IRQ_WANTED;
   if((test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail"))))))))))))){expected[0x810/4]=15;expected[0x814/4]=16;}
   assert(p.second==expected);
  }else if(p.first==0xf101000&&(test=="identity-reset-fail"||test=="snapshot-reset-fail"||(test=="safeoff-reset-fail"||(test=="handoff-reset-fail"||(test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail")))))))))))))))){
   auto expected=before[p.first];expected[0]=0x3c0;expected[1]=0;assert(p.second==expected);
  }else assert(p.second==before[p.first]);
 }
 if(mode<1||mode>19)assert(logs.empty()&&!maps&&!threads);
 else if(test=="ram-missing"||test=="ram-short"||coeff_missing)assert(!maps&&threads==1&&logs.back().find(coeff_missing?"SKIP missing-private-input":"SKIP missing-private-boot-input")!=std::string::npos);
 else if(audio||observe||!board_ok)assert(!maps&&!threads);
 else if(test=="thread")assert(!maps&&threads==1);
 else if(test=="ready")assert(logs.back().find("PASS bus-setup")!=std::string::npos&&maps==7&&releases==7);
 else if(test=="clock")assert(logs.back().find("FAIL clocks se_mapped=0")!=std::string::npos&&maps==6&&time_us<=100000);
 else if(test=="protocol"||test=="busy")assert(logs.back().find("SKIP engine-not-idle-i2c")!=std::string::npos);
 else if(test=="reset")assert(pages==before&&logs.back().find("SKIP reset-state-changed")!=std::string::npos);
 else if(test=="timing")assert(logs.back().find("PASS timing-fifo")!=std::string::npos&&maps==7);
 else if(test=="clock-invalid")assert(logs.back().find("SKIP no-qualified-xo")!=std::string::npos&&maps==6);
 else if(test=="fifo")assert(logs.back().find("FAIL timing-fifo")!=std::string::npos&&maps==7);
 else if((test=="identity-reset-fail"||test=="snapshot-reset-fail"||(test=="safeoff-reset-fail"||(test=="handoff-reset-fail"||(test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail"))))))))))))))))assert(logs.back().find(test=="identity-reset-fail"?"FAIL otp-identity":test=="snapshot-reset-fail"?"FAIL snapshot":test=="safeoff-reset-fail"?"SKIP safe-off-prerequisite":(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail")))))))))?"SKIP readiness-prerequisite":"SKIP handoff-prerequisite")!=std::string::npos&&((test=="safeoff-reset-fail"||(test=="handoff-reset-fail"||(test=="rx-reset-fail"||(test=="source-reset-fail"||(test=="config-reset-fail"||(test=="owned-reset-fail"||(test=="b2-reset-fail"||(test=="trim-reset-fail"||(test=="ram-reset-fail"||(test=="header-reset-fail"||(test=="burst-reset-fail"||(test=="coeff-reset-fail"||(test=="run-reset-fail"||(test=="mpu-reset-fail"||test=="observed-reset-fail"))))))))))))))?logs[logs.size()-2]:logs.back()).find("reset_asserted=1")!=std::string::npos);
 if((mode>=1&&mode<=19)&&!audio&&!observe&&board_ok){unsigned n=threads;zizhan_se15_prepare_probe();assert(threads==n);}
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-se15-setup-") as temp:
    temp = Path(temp)
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower")]
    binary = temp / "core"
    subprocess.run([*flags, str(ROOT / "tools/zizhan/tests/cs35l41-setup.cpp"), "-o", str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
    cpp = temp / "platform.cpp"
    cpp.write_text(harness.replace("/* ACTUAL_SOURCE */", source))
    binary = temp / "platform"
    obj = temp / "geni.o"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "drivers/PongPower"), "-c", str(ROOT / "drivers/PongPower/GeniI2c.c"), "-o", str(obj)], check=True, timeout=30)
    amp = temp / "amp.o"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "drivers/PongPower"), "-c", str(ROOT / "drivers/PongPower/ZizhanCs35l41.c"), "-o", str(amp)], check=True, timeout=30)
    subprocess.run([*flags, str(cpp), str(obj), str(amp), "-o", str(binary)], check=True, timeout=30)
    for case in ("off","unsupported","audio","observe","board","thread","clock","protocol","busy","pins","reset","map","engine-map","ready","timing","clock-invalid","fifo","identity-reset-fail","snapshot-reset-fail","safeoff-reset-fail","handoff-reset-fail","rx-reset-fail","source-reset-fail","config-reset-fail","owned-reset-fail","b2-reset-fail","trim-reset-fail","ram-missing","ram-short","ram-reset-fail","header-reset-fail","burst-reset-fail","coeff-reset-fail","coeff-missing","run-reset-fail","mpu-reset-fail","observed-reset-fail","silent-proof","silent-missing","silent-reset","silent-once","prerun-missing","prerun-bad","prerun-input","rates-input","rx-input","format-input","native-core-input","native-core-run-input","native-data-missing","native-data-proof","native-data-input"):
        subprocess.run([str(binary),case],check=True,timeout=10)
print("PASS actual SE15 setup platform: 52 controls, prior modes unchanged,lease proof/privatecachedinput bounds/missing/reset/single-use gates")
