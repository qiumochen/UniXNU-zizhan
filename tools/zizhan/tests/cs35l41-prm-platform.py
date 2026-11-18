#!/usr/bin/env python3
"""Execute actual optional PRM platform with synthetic channel/clock only."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/"drivers/PongPower/ZizhanAudioPrm.cpp").read_text()
source="\n".join(x for x in source.splitlines() if not x.startswith("#include"))
harness=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "ShengAdsp.h"
#include "ZizhanAudioPrm.h"
#include "ZizhanAudioService.h"
#include "ZizhanAudioServiceGate.h"
#include "ZizhanAudioFlow.h"
#include "ZizhanAudioSilent.h"
#include "ZizhanTdm.h"
#include "ZizhanPcm.h"
#include "ZizhanAudioDma.h"
#include "ZizhanAudioMemory.h"
#include "ZizhanAudioWrite.h"
#include "ZizhanAudioZero.h"
#include "ZizhanAudioDmaMap.h"
#include "ZizhanAudioGraph.h"
#include "ZizhanAudioBatch.h"
#include "ZizhanAudioTone48.h"
static unsigned mode=1,audio,prepare_mode,adsp=2,threads,registrations,sends,allocations;
static bool board=true,lock_fail,channel_fail,thread_fail,down,drop,frozen,send_fail;
static unsigned reject_stage;static bool not_ready;
static unsigned observe_mode,silent_calls,silent_model;
static unsigned tdm_sends,tdm_reject,pcm_sends,pcm_reject;
static bool pins_bad,pin_map_fail;static unsigned pin_maps;
static unsigned attended;
static unsigned zero_calls,write_sends,write_fault;static bool zero_fail,wire_claim_fail,wire_reset_fail,output_cfg_fail;
static unsigned dma_begin,dma_end,dma_retain,memory_sends;static unsigned memory_fault;static bool dma_begin_fail,dma_end_fail;
extern "C" bool zizhan_audio_zero_verify(){++zero_calls;assert((mode>=28)&&dma_begin==1&&!memory_sends&&!write_sends);return !zero_fail;}
extern "C" bool zizhan_audio_dma_map_begin(){++dma_begin;assert(pcm_sends==4&&!silent_calls);return !dma_begin_fail;}
extern "C" bool zizhan_audio_dma_map_end(){++dma_end;assert(memory_sends==(mode==27?0u:2u)&&pcm_sends==4&&silent_calls==(mode>=29?1u:0u));return !dma_end_fail;}
extern "C" uint64_t zizhan_audio_dma_map_address(){return 0x1f0000000ull;}
extern "C" void zizhan_audio_dma_map_retain(){++dma_retain;}
static uint64_t time_us;
static std::vector<std::string> logs;
struct IOLock{};
static IOLock *IOLockAlloc(){++allocations;static IOLock l;return lock_fail?nullptr:&l;}
static void IOLockLock(IOLock *){}
static void IOLockUnlock(IOLock *){}
constexpr unsigned kIOMapAnywhere=1,kIOMapInhibitCache=2,kIOMapReadOnly=4;
// mode38 (finite ring runtime) is not exercised by this harness; these stubs only let it compile and abort if reached.
using boolean_t=int;using ppnum_t=uint32_t;using IOOptionBits=unsigned;
constexpr unsigned kIOMapWriteCombineCache=8,kIODirectionInOut=3;static void *kernel_task;
extern "C" boolean_t pmap_valid_page(ppnum_t){abort();}
struct IOBufferMemoryDescriptor{static IOBufferMemoryDescriptor *inTaskWithOptions(void *,IOOptionBits,size_t,size_t){abort();}void *getBytesNoCopy(){abort();}void release(){abort();}};
struct mach_timebase_info_data_t{uint32_t numer,denom;};static void clock_timebase_info(mach_timebase_info_data_t *){abort();}
static uint32_t pin_reg,dma_regs[0x100000/4];static bool dma_mapping=false,chosen_path=false,dma_missing=false,dma_lock_fail=false,dma_map_fail=false;static unsigned dma_locks,dma_unlocks,dma_maps;
static uint64_t dma_extent[2]={0xf0000000,65536};
extern "C" bool pong_smmu_lease_lock(){++dma_locks;return !dma_lock_fail;}
extern "C" void pong_smmu_lease_unlock(){++dma_unlocks;}
struct IOMemoryMap{uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(dma_mapping?dma_regs:&pin_reg);}void release(){}};
struct IODeviceMemory{
 static IODeviceMemory *withRange(uint64_t base,uint64_t size){dma_mapping=base==0x15000000;assert((dma_mapping&&size==0x100000)||((base==0xf179000||base==0xf17b000||base==0xf17a000||base==0xf17c000)&&size==0x1000));static IODeviceMemory m;return &m;}
 IOMemoryMap *map(unsigned flags){assert(flags==(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly));static IOMemoryMap m;if(dma_mapping){++dma_maps;return dma_map_fail?nullptr:&m;}++pin_maps;pin_reg=pins_bad?4:1;return pin_map_fail?nullptr:&m;}
 void release(){}
};
static void IOLog(const char *fmt,...){char line[512];va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof line,fmt,ap);va_end(ap);logs.emplace_back(line);}
static void IOSleep(unsigned ms){assert(ms==10);time_us+=ms*1000;}
static uint64_t mach_absolute_time(){return frozen?0:time_us*1000;}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *ns){*ns=ticks;}
static void *current_thread(){return (void *)1;}
static void thread_set_thread_name(void *,const char *){}
using wait_result_t=int;using thread_t=void *;
constexpr int KERN_SUCCESS=0;
static int kernel_thread_start(void (*fn)(void *,int),void *context,thread_t *out){++threads;if(thread_fail)return 1;fn(context,0);*out=(void *)1;return 0;}
static void thread_deallocate(thread_t){}
static int gIODTPlane;
struct OSData{size_t getLength(){if(chosen_path)return sizeof dma_extent;return board?sizeof("st,fts-zizhan-pri"):sizeof("other");}const void *getBytesNoCopy(){if(chosen_path)return dma_extent;return board?"st,fts-zizhan-pri":"other";}};
struct IORegistryEntry{
 static IORegistryEntry *fromPath(const char *path,int){assert(!strcmp(path,"/arm-io/pong-touch")||!strcmp(path,"/chosen"));chosen_path=!strcmp(path,"/chosen");static IORegistryEntry n;return &n;}
 OSData *getProperty(const char *p){assert(!strcmp(p,"compatible")||!strcmp(p,"pong-audio-dma"));static OSData d;return chosen_path&&dma_missing?nullptr:&d;}void release(){}
};
#define OSDynamicCast(type,value) static_cast<type *>(value)
static unsigned service_arg;static ZizhanAudioHandoff handoff;
extern "C" volatile ZizhanAudioControl *zizhan_audio_service_control(void){abort();}
extern "C" int16_t *zizhan_audio_service_ring(void){abort();}
extern "C" ZizhanAudioHandoff *zizhan_audio_service_handoff(void){return &handoff;}
extern "C" unsigned zizhan_amp_pulse_flags;unsigned zizhan_amp_pulse_flags;
extern "C" void zizhan_amp_native_play_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *){abort();}
static bool PE_parse_boot_argn(const char *key,void *out,size_t size){assert(size==4);unsigned value;
 if(!strcmp(key,"zizhan_pre_off")||!strcmp(key,"zizhan_tone_repeat")||!strcmp(key,"zizhan_pulse_dry")||!strcmp(key,"zizhan_write_ms")||!strcmp(key,"zizhan_write_flush")||!strcmp(key,"zizhan_write_probe")||!strcmp(key,"zizhan_audio_stream_ms")||!strcmp(key,"zizhan_audio_inflight"))return false;
 if(!strcmp(key,"zizhan_audio_service"))value=service_arg;else if(!strcmp(key,"zizhan_audio_attended"))value=attended;else if(!strcmp(key,"zizhan_audio_prm"))value=mode;else if(!strcmp(key,"pong_audio"))value=audio;
 else if(!strcmp(key,"zizhan_se15_prepare"))value=prepare_mode;else if(!strcmp(key,"zizhan_se15_observe"))value=observe_mode;else{assert(!strcmp(key,"pong_adsp"));value=adsp;}
 memcpy(out,&value,4);return value!=0;
}
#ifdef __APPLE__
extern "C" __attribute__((weak)) int pong_klog_flush(int){return -1;} /* ld64 rejects undefined weak refs */
#else
extern "C" int pong_klog_flush(int) __attribute__((weak));
#endif
extern "C" {volatile int32_t native_adsp_stats[16];char native_adsp_text[96];}
struct ShengAdspChannel{};
static ShengAdspClient client;static void *client_context;
extern "C" ShengAdspChannel *sheng_adsp_channel(const char *name,uint32_t size,uint32_t count,int init,const ShengAdspClient *c,void *ctx){
 ++registrations;assert(!strcmp(name,"adsp_apps")&&size==512&&count==16&&init==1);
 if(channel_fail){return nullptr;}client=*c;client_context=ctx;if(!down)client.state(ctx,1);static ShengAdspChannel ch;return &ch;
}
extern "C" int sheng_adsp_send(ShengAdspChannel *,const void *bytes,uint32_t length){
 ++sends;if(send_fail)return -1;if(drop)return 0;GprPacket request;assert(!gpr_decode((const uint8_t *)bytes,length,&request));
 if(request.src_port==ZizhanMemoryPort){
  ++memory_sends;assert((mode==26||mode>=28)&&pcm_sends==4&&silent_calls==(mode>=29&&memory_sends==2?1u:0u)&&dma_begin==1&&!dma_end);
  uint8_t payload[8]={},reply[512];GprPacket p={};p.src_domain=2;p.dst_domain=3;p.src_port=1;p.dst_port=ZizhanMemoryPort;p.token=request.token;
  if(memory_sends==1){assert(request.opcode==AR_OP_MEM_MAP&&length==44);p.opcode=AR_OP_MEM_MAP_DONE;payload[0]=0x56;}
  else{if(mode>=28)assert(write_sends==(mode==34?4u:mode>=29?2u:1u));assert(request.opcode==AR_OP_MEM_UNMAP&&length==28&&zizhan_tdm_le32((const uint8_t *)bytes+24)==0x56);p.opcode=GPR_OP_BASIC_RESULT;for(unsigned i=0;i<4;++i)payload[i]=(uint8_t)(request.opcode>>(8*i));}
  unsigned payload_bytes=memory_sends==1?4u:8u;
  if(memory_fault==memory_sends){if(memory_sends==1)payload[0]=0;else payload[4]=5;}
  unsigned n=gpr_encode(reply,512,&p,payload,payload_bytes);client.receive(client_context,reply,n);return 0;
 }
 if(request.src_port==ZizhanPcmPort&&request.opcode==AR_OP_WRITE){
  ++write_sends;assert((mode>=28)&&zero_calls==1&&!zero_fail&&memory_sends==1&&dma_begin==1&&!dma_end&&silent_calls==(mode>=29?1u:0u)&&pcm_sends==4&&length==68&&request.dst_port==ZizhanPcmShmem);
  assert(zizhan_tdm_le32(request.payload)==(mode==34&&(write_sends==1||write_sends==3)?0xf0004000u:0xf0000000u)&&zizhan_tdm_le32(request.payload+4)==1&&zizhan_tdm_le32(request.payload+8)==0x56&&zizhan_tdm_le32(request.payload+12)==11520);
  if(write_fault==2||(write_fault==4&&write_sends==2))return 0;
  uint8_t payload[32]={},reply[512];memcpy(payload,request.payload,12);if(write_fault==1||(write_fault==3&&write_sends==2))payload[12]=5;
  GprPacket p={};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;p.token=request.token;p.opcode=AR_OP_WRITE_DONE;
  unsigned n=gpr_encode(reply,512,&p,payload,32);client.receive(client_context,reply,n);return 0;
 }
 if(request.src_port==ZizhanPcmPort){
  ++pcm_sends;assert(mode>=23&&tdm_sends==4&&request.dst_port==(pcm_sends==2?(unsigned)ZizhanPcmShmem:1u));
  if(mode>=24&&pcm_sends>=5){assert(length==72&&zizhan_tdm_le32((const uint8_t *)bytes+56)==2&&zizhan_tdm_le32((const uint8_t *)bytes+60)==ZizhanPcmSg&&zizhan_tdm_le32((const uint8_t *)bytes+64)==ZizhanTdmSg);}
  if(pcm_sends>=5)assert(silent_calls==1);else assert(!silent_calls);
  uint8_t payload[8]={},reply[512];for(unsigned i=0;i<4;++i)payload[i]=(uint8_t)(request.opcode>>(8*i));if(pcm_reject==pcm_sends)payload[4]=5;
  GprPacket p={};p.src_domain=2;p.dst_domain=3;p.src_port=request.dst_port;p.dst_port=ZizhanPcmPort;p.token=request.token;p.opcode=GPR_OP_BASIC_RESULT;
  unsigned n=gpr_encode(reply,512,&p,payload,8);client.receive(client_context,reply,n);return 0;
 }
 if(request.src_port==ZizhanTdmPort){
  if(mode>=23&&tdm_sends>=4)assert(pcm_sends==6&&!pcm_reject);
  ++tdm_sends;assert(mode>=9&&request.dst_port==1&&request.dst_domain==2&&request.src_domain==3&&pin_maps==4);
  uint8_t payload[8]={},reply[512];for(unsigned i=0;i<4;++i)payload[i]=(uint8_t)(request.opcode>>(8*i));
  if(tdm_reject==tdm_sends)payload[4]=5;
  GprPacket p={};p.src_domain=2;p.dst_domain=3;p.src_port=1;p.dst_port=ZizhanTdmPort;p.token=request.token;p.opcode=GPR_OP_BASIC_RESULT;
  unsigned n=gpr_encode(reply,512,&p,payload,8);client.receive(client_context,reply,n);return 0;
 }
 unsigned expected_port=mode>=4?(request.opcode==GPR_OP_APM_GET_SPF_STATE?3u:7u):mode==3?7u:(unsigned)ZizhanPrmPort;
 assert(request.src_port==expected_port&&request.src_domain==3&&request.dst_domain==2);
 if(mode>=4)assert(time_us>=1000000&&request.token==0);
 uint8_t payload[8]={},reply[512];GprPacket p={};p.src_domain=2;p.dst_domain=3;p.src_port=request.dst_port;p.dst_port=request.src_port;p.token=request.token;
 if(request.opcode==GPR_OP_APM_GET_SPF_STATE){p.opcode=GPR_OP_APM_SPF_STATE;payload[0]=not_ready?0:1;}
 else{assert(request.opcode==AR_OP_PRM_REQUEST||request.opcode==AR_OP_PRM_RELEASE);
  if(length==60)assert(zizhan_prm_le32((const uint8_t *)bytes+56)==2);
  else{assert(mode>=6&&(length==76||length==64)&&pin_maps==(mode>=9?4u:2u));assert(zizhan_prm_le32((const uint8_t *)bytes+60)==0x204);}
  p.opcode=request.opcode==AR_OP_PRM_REQUEST?AR_OP_PRM_REQUEST_DONE:AR_OP_PRM_RELEASE_DONE;
  if(sends==reject_stage)payload[0]=5;
 }
 unsigned payload_bytes=4;
 if(mode>=5&&request.opcode!=GPR_OP_APM_GET_SPF_STATE){unsigned status=payload[0];uint32_t id=zizhan_prm_le32((const uint8_t *)bytes+44);for(unsigned i=0;i<4;++i)payload[i]=(uint8_t)(id>>(8*i));payload[4]=status;payload_bytes=8;}
 unsigned n=gpr_encode(reply,sizeof reply,&p,payload,payload_bytes);client.receive(client_context,reply,n);return 0;
}
extern "C" void zizhan_amp_silent_clock_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){
 if(mode>=23)assert(pcm_sends==4);
 ++silent_calls;assert(lease->phase==ZizhanPrmClockReleaseReady&&lease->framework_ready&&lease->lease_held&&lease->clock_held&&lease->sent==3&&lease->acknowledged==3&&!lease->uncertain);
 memset(r,0,sizeof *r);r->invoked=1;r->amp_pass=1;r->release_safe=1;r->reset_asserted=r->pin_restored=1;
 time_us+=25000000;
 if(silent_model==1)r->amp_pass=0;
 if(silent_model==2)r->release_safe=r->reset_asserted=0;
 if(silent_model==3)time_us+=36000000;
 if(silent_model==4)client.state(client_context,0);
}
extern "C" void zizhan_amp_pll_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){
 if(mode>=9)assert(tdm_sends==4);
 zizhan_amp_silent_clock_run(lease,r);r->amp_pass=0;r->diagnostic_capture=1;
}
extern "C" void zizhan_amp_monitor_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_pll_clock_capture(lease,r);}
extern "C" void zizhan_amp_global_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_pll_clock_capture(lease,r);}
extern "C" void zizhan_amp_global_silent_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_silent_clock_run(lease,r);}
extern "C" void zizhan_amp_global_mpu_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_pll_clock_capture(lease,r);}
extern "C" void zizhan_amp_global_observed_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_silent_clock_run(lease,r);}
extern "C" void zizhan_amp_native_frame_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_silent_clock_run(lease,r);}
extern "C" void zizhan_amp_native_debug_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_silent_clock_run(lease,r);r->amp_pass=0;r->diagnostic_capture=1;}
extern "C" void zizhan_amp_native_protection_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){assert((mode>=17&&mode<=26)&&tdm_sends==4);zizhan_amp_silent_clock_run(lease,r);}
extern "C" void zizhan_amp_native_rates_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_native_protection_run(lease,r);}
extern "C" void zizhan_amp_asp_rx_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_native_protection_run(lease,r);}
extern "C" void zizhan_amp_asp_format_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_native_protection_run(lease,r);}
extern "C" void zizhan_amp_native_core_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_pll_clock_capture(lease,r);}
extern "C" void zizhan_amp_native_data_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r,const ZizhanAudioTransfer *transfer){
 assert(mode>=29&&memory_sends==1&&dma_begin==1&&!dma_end&&zero_calls==1&&transfer&&transfer->once);zizhan_amp_silent_clock_run(lease,r);
 if(wire_claim_fail){r->amp_pass=0;return;}
 r->data_connected=1;
 for(unsigned chip=0;chip<2;++chip){++r->transfer_attempts[chip];if(transfer->once(transfer->context,chip)!=1){r->amp_pass=0;r->data_restored=1;return;}++r->transfer_acks[chip];}
 r->data_restored=1;if(wire_reset_fail)r->release_safe=r->data_restored=0;
}
extern "C" void zizhan_amp_native_output_off_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r,const ZizhanAudioTransfer *transfer){
 if(output_cfg_fail){++silent_calls;r->invoked=r->release_safe=1;r->output_configured=0;return;}
 zizhan_amp_native_data_run(lease,r,transfer);r->output_configured=1;
}
extern "C" void zizhan_amp_native_mailbox_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r,const ZizhanAudioTransfer *transfer){zizhan_amp_native_output_off_run(lease,r,transfer);r->mailbox_verified=1;}
static unsigned tone_prepare_calls,tone_clear_calls;static bool tone_prepare_fail,tone_clear_fail;
extern "C" bool zizhan_audio_tone_prepare(){++tone_prepare_calls;assert(dma_begin==1&&zero_calls==1&&!memory_sends&&!write_sends&&!silent_calls);return !tone_prepare_fail;}
extern "C" bool zizhan_audio_tone_clear(){++tone_clear_calls;assert(tone_prepare_calls==1&&memory_sends==1&&write_sends==(mode==34?4u:2u)&&silent_calls==1);return !tone_clear_fail;}
extern "C" bool zizhan_audio_tone48_prepare(){assert(false);return false;}
extern "C" bool zizhan_audio_tone48_clear(){assert(false);return false;}
extern "C" void zizhan_amp_native_pulse_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r,const ZizhanAudioTransfer *t){
 assert(mode==34&&attended==1&&memory_sends==1&&!write_sends);zizhan_amp_silent_clock_run(lease,r);r->data_connected=r->data_restored=r->output_configured=r->mailbox_verified=r->iv_verified=r->pulse_verified=1;
 for(unsigned chip:{2u,0u,3u,1u})if(t->once(t->context,chip)!=1){r->amp_pass=r->pulse_verified=0;return;}
}
extern "C" void zizhan_amp_native_iv_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r,const ZizhanAudioTransfer *transfer){zizhan_amp_native_mailbox_run(lease,r,transfer);r->iv_verified=1;}
extern "C" void zizhan_amp_native_core_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *r){zizhan_amp_silent_clock_run(lease,r);}
/* SOURCE */
int main(int argc,char **argv){assert(argc==2);std::string test=argv[1];
 if(test=="off")mode=0;else if(test=="unsupported")mode=39;else if(test=="unsupported42")mode=42;else if(test=="service-off")mode=40;else if(test=="service-nostart")mode=40,service_arg=1;else if(test=="play-unattended")mode=41,service_arg=1;else if(test=="play-noservice")mode=41;else if(test=="direct")mode=2;else if(test=="native")mode=3;
 else if(test=="pulse"||test=="pulse-preroll"||test=="pulse-timeout"||test=="pulse-tone-timeout")mode=34,attended=1,write_fault=test=="pulse-preroll"?1u:test=="pulse-timeout"?2u:test=="pulse-tone-timeout"?4u:0u;else if(test=="unattended-pulse")mode=34;else if(test=="tone"||test=="tone-prepare"||test=="tone-clear")mode=33,tone_prepare_fail=test=="tone-prepare",tone_clear_fail=test=="tone-clear";else if(test=="iv")mode=32;else if(test=="mailbox")mode=31;else if(test=="outputoff")mode=30;else if(test=="outputoff-config")mode=30,output_cfg_fail=true;
 else if(test=="wire")mode=29;else if(test=="wire-claim")mode=29,wire_claim_fail=true;else if(test=="wire-data")mode=29,write_fault=1;else if(test=="wire-second")mode=29,write_fault=3;else if(test=="wire-reset")mode=29,wire_reset_fail=true;else if(test=="wire-unmap")mode=29,memory_fault=2;else if(test=="wire-end")mode=29,dma_end_fail=true;else if(test=="wire-timeout")mode=29,write_fault=2;
 else if(test=="write")mode=28;else if(test=="write-zero")mode=28,zero_fail=true;else if(test=="write-bad")mode=28,write_fault=1;else if(test=="write-timeout")mode=28,write_fault=2;else if(test=="write-unmap")mode=28,memory_fault=2;else if(test=="write-end")mode=28,dma_end_fail=true;
 else if(test=="dmacontrol")mode=27;else if(test=="dmacontrol-begin")mode=27,dma_begin_fail=true;else if(test=="dmacontrol-end")mode=27,dma_end_fail=true;
 else if(test=="dmamap")mode=26;else if(test=="dmamap-begin")mode=26,dma_begin_fail=true;
 else if(test=="dmamap-map")mode=26,memory_fault=1;else if(test=="dmamap-unmap")mode=26,memory_fault=2;else if(test=="dmamap-end")mode=26,dma_end_fail=true;
 else if((test=="dma"||test=="dmamap"))mode=25;else if(test=="dma-missing")mode=25,dma_missing=true;
 else if(test=="dma-lock")mode=25,dma_lock_fail=true;else if(test=="dma-map")mode=25,dma_map_fail=true;
 else if((test=="joint"||(test=="dma"||test=="dmamap")))mode=24;else if(test=="joint-stop")mode=24,pcm_reject=5;else if(test=="joint-close")mode=24,pcm_reject=6;
 else if((test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))mode=23;
 else if(test=="pcm-open")mode=23,pcm_reject=1;else if(test=="pcm-media")mode=23,pcm_reject=2;
 else if(test=="pcm-start")mode=23,pcm_reject=4;else if(test=="pcm-stop")mode=23,pcm_reject=5;else if(test=="pcm-close")mode=23,pcm_reject=6;
 else if((test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap")))))mode=22;
 else if(test=="native-core")mode=21;
 else if((test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))))mode=20;
 else if((test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap")))))))mode=19;
 else if((test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))))))mode=18;
 else if((test=="native-protection"||(test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap")))))))))mode=17;
 else if(test=="core-debug")mode=16;
 else if((test=="native-frame"||(test=="native-protection"||(test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))))))))mode=15;
 else if(test=="powered-run")mode=14;
 else if(test=="powered-mpu")mode=13;
 else if(test=="global-run")mode=12;
 else if(test=="global")mode=11;
 else if(test=="fsmon")mode=10;
 else if(test=="tdm")mode=9;else if(test=="tdm-open")mode=9,tdm_reject=1;
 else if(test=="tdm-config")mode=9,tdm_reject=2;else if(test=="tdm-start")mode=9,tdm_reject=4;
 else if(test=="tdm-stop")mode=9,tdm_reject=5;else if(test=="tdm-close")mode=9,tdm_reject=6;
 else if(test=="pll-capture")mode=8;
 else if(test=="silent")mode=7;else if(test=="silent-ampfail")mode=7,silent_model=1;
 else if(test=="silent-cleanup")mode=7,silent_model=2;else if(test=="silent-timeout")mode=7,silent_model=3;
 else if(test=="silent-close")mode=7,silent_model=4;else if(test=="silent-observe")mode=7,observe_mode=1;
 else if(test=="silent-notready")mode=7,not_ready=true;else if(test=="silent-rejectclock")mode=7,reject_stage=3;
 else if(test=="clock")mode=6;else if(test=="clock-pins")mode=6,pins_bad=true;else if(test=="clock-map")mode=6,pin_map_fail=true;
 else if(test=="clock-reject")mode=6,reject_stage=3;else if(test=="clock-release-reject")mode=6,reject_stage=4;
 else if(test=="parameter")mode=5;else if(test=="parameter-reject")mode=5,reject_stage=2;
 else if(test=="native-ready")mode=4;else if(test=="native-not-ready")mode=4,not_ready=true;
 else if(test=="settle-frozen")mode=4,frozen=true;else if(test=="settle-drop")mode=4,drop=true;
 else if(test=="audio")audio=1;else if(test=="prepare")prepare_mode=19;
 else if(test=="adsp")adsp=1;else if(test=="board")board=false;else if(test=="lock")lock_fail=true;
 else if(test=="channel")channel_fail=true;else if(test=="thread")thread_fail=true;
 else if(test=="down")down=true;else if(test=="drop")drop=true;else if(test=="frozen")down=true,frozen=true;
 else if(test=="send")send_fail=true;else if(test=="reject-request")reject_stage=2;else if(test=="reject-release")reject_stage=3;else assert(test=="pass");
 dma_regs[0x20/4]=(1u<<30)|(15u<<9)|120;dma_regs[0x24/4]=78;dma_regs[0x28/4]=(1u<<12)|0x22;
 zizhan_audio_prm_probe();
 if(test=="unattended-pulse"){assert(!allocations&&!registrations&&!threads&&logs.size()==1);return 0;}
 if(test=="off"||test=="unsupported"||test=="unsupported42")assert(!allocations&&!registrations&&!threads&&logs.empty());
 else if(test=="play-unattended"){assert(!allocations&&!registrations&&!threads&&logs.size()==1&&logs[0].find("SKIP attended=0")!=std::string::npos&&!handoff.owner);return 0;}
 else if(test=="play-noservice"){assert(!allocations&&!registrations&&!threads&&logs.back().find("SKIP")!=std::string::npos&&!handoff.owner);return 0;}
 else if(test=="service-off"){assert(!allocations&&!registrations&&!threads&&logs.size()==1&&logs[0].find("SKIP mode40-needs-service")!=std::string::npos&&!handoff.owner);return 0;}
 else if(test=="service-nostart"){
  // Registered as owner, waited the bounded 170 s, took no PRM lease and sent nothing; later Starts are refused.
  assert(handoff.owner==1&&handoff.phase==ZizhanHandoffFailed&&threads==1&&!sends&&!owner.state.lease_held&&time_us==170000000u);
  bool skipped=false;for(const auto &l:logs)skipped|=l.find("SKIP no-start")!=std::string::npos;
  assert(skipped&&!zizhan_handoff_request_start(&handoff));return 0;}
 else if(audio||prepare_mode||observe_mode||adsp!=2||!board)assert(!allocations&&!registrations&&!threads&&logs.back().find("SKIP")!=std::string::npos);
 else if(test=="lock")assert(!registrations&&!threads);
 else if(test=="channel")assert(registrations==1&&!threads);
 else if(test=="thread")assert(registrations==1&&threads==1&&!sends);
 else if(test=="pass"||test=="direct"||test=="native"||test=="native-ready"||test=="parameter")assert(owner.state.phase==ZizhanPrmDone&&sends==((mode==2||mode==3)?2u:3u)&&owner.state.request_ack&&owner.state.release_ack&&!owner.state.lease_held);
 else if(test=="clock")assert(owner.state.phase==ZizhanPrmDone&&sends==5&&owner.state.clock_request_ack&&owner.state.clock_release_ack&&!owner.state.clock_held&&!owner.state.lease_held&&!owner.state.uncertain&&pin_maps==2);
 else if(test=="silent"||test=="silent-ampfail"||test=="pll-capture"||test=="tdm"||test=="fsmon"||test=="global"||test=="global-run"||test=="powered-mpu"||test=="powered-run"||(test=="native-frame"||(test=="native-protection"||(test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap")))))))))||test=="core-debug"||test=="native-core"){
  assert(silent_calls==1&&owner.state.phase==ZizhanPrmDone&&sends==(mode==26?17u:mode>=24?15u:mode>=23?17u:mode>=9?11u:5u)&&!owner.state.lease_held&&!owner.state.clock_held&&!owner.state.uncertain);
  assert(owner.silent.amp_pass==((test=="silent"||test=="global-run"||test=="powered-run"||(test=="native-frame"||(test=="native-protection"||(test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))))))))?1u:0u));if(test=="pll-capture")assert(owner.silent.diagnostic_capture);
 }
 else if(test=="wire"||test=="outputoff"||test=="mailbox"||test=="iv"||test=="tone"||test=="pulse"){assert(owner.state.phase==ZizhanPrmDone&&sends==(mode==34?21u:19u)&&silent_calls==1&&!owner.state.lease_held&&!owner.state.clock_held&&!owner.state.uncertain);}
 else if(test=="write"){assert(owner.state.phase==ZizhanPrmDone&&sends==18&&silent_calls==1&&!owner.state.lease_held&&!owner.state.clock_held&&!owner.state.uncertain);}
 else if(test=="dmacontrol"){assert(owner.state.phase==ZizhanPrmDone&&sends==15&&silent_calls==1&&!owner.state.lease_held&&!owner.state.clock_held&&!owner.state.uncertain);}
 else{assert(owner.state.phase==ZizhanPrmFailed);if(test=="reject-release")assert(owner.state.lease_held&&owner.state.uncertain);}
 assert(time_us<=(mode>=7?90010000u:40010000u));
 if(test=="silent-cleanup"||test=="silent-timeout"||test=="silent-close")assert(silent_calls==1&&sends==3&&owner.state.lease_held&&owner.state.clock_held&&owner.state.uncertain&&!owner.state.clock_release_ack);
 if(test=="silent-notready"||test=="silent-rejectclock")assert(!silent_calls);
 if(mode>=23&&mode<=25&&test!="dma-missing"&&test!="dma-lock"&&test!="dma-map"){
  if(!pcm_reject)assert(owner.pcm.phase==ZizhanPcmDone&&pcm_sends==6&&tdm_sends==(mode>=24?4u:6u)&&!owner.state.uncertain);
  else assert(owner.pcm.phase==ZizhanPcmFailed&&owner.state.lease_held&&owner.state.clock_held&&owner.state.uncertain&&tdm_sends==4&&silent_calls==(pcm_reject>=5?1u:0u));
 }
 if(mode>=9&&mode<=22){if(test=="global-run"||test=="powered-run"||(test=="native-frame"||(test=="native-protection"||(test=="native-rates"||(test=="asp-rx"||(test=="asp-format"||(test=="native-core-run"||(test=="pcm"||(test=="joint"||(test=="dma"||test=="dmamap"))))))))))assert(owner.tdm.phase==ZizhanTdmDone&&tdm_sends==6&&owner.silent.amp_pass);
  else if(test=="tdm"||test=="fsmon"||test=="global"||test=="powered-mpu"||test=="core-debug"||test=="native-core")assert(owner.tdm.phase==ZizhanTdmDone&&tdm_sends==6&&owner.silent.diagnostic_capture);
  else{assert(owner.state.lease_held&&owner.state.clock_held&&owner.state.uncertain&&owner.tdm.phase==ZizhanTdmFailed);if(tdm_reject<5)assert(!silent_calls);}}
 if(mode==25){
  if((test=="dma"||test=="dmamap")){assert(dma_maps==1&&dma_locks==1&&dma_unlocks==1&&owner.state.phase==ZizhanPrmDone);bool saw=false;for(const auto &l:logs)if(l.find("ZIZHAN_AUDIO_DMA: PASS")!=std::string::npos)saw=true;assert(saw);}
  else{assert(!silent_calls&&!pcm_sends&&!tdm_sends&&owner.state.uncertain&&owner.state.lease_held&&owner.state.clock_held);if(test=="dma-missing")assert(!dma_locks&&!dma_maps);if(test=="dma-lock")assert(dma_locks==1&&!dma_unlocks&&!dma_maps);if(test=="dma-map")assert(dma_locks==dma_unlocks&&dma_maps==1);}
 }
 if(mode==26){
  if(test=="dmamap")assert(dma_begin==1&&dma_end==1&&!dma_retain&&memory_sends==2&&owner.memory.phase==ZizhanMemoryDone&&owner.state.phase==ZizhanPrmDone);
  else{assert(dma_begin==1&&dma_retain==1&&owner.state.uncertain&&owner.state.clock_held&&owner.state.lease_held);if(test=="dmamap-end")assert(dma_end==1&&!silent_calls&&pcm_sends==4);else assert(!silent_calls&&!dma_end&&pcm_sends==4);}
 }
 if(mode==34){assert(tone_prepare_calls==1&&silent_calls==1);
  if(test=="pulse")assert(tone_clear_calls==1&&write_sends==4&&owner.write_ack==4&&owner.transfer_acks==4&&owner.tone_cleared&&owner.silent.pulse_verified&&memory_sends==2&&dma_end==1&&!dma_retain);
  else assert(!tone_clear_calls&&write_sends==(test=="pulse-tone-timeout"?2u:1u)&&!owner.silent.pulse_verified&&memory_sends==1&&!dma_end&&dma_retain&&owner.state.uncertain&&time_us<90000000);
 }
 if(mode==33){assert(tone_prepare_calls==1);
  if(test=="tone")assert(tone_clear_calls==1&&owner.tone_prepared&&owner.tone_cleared&&owner.state.phase==ZizhanPrmDone&&memory_sends==2&&write_sends==2&&!dma_retain);
  else if(test=="tone-prepare")assert(!tone_clear_calls&&!silent_calls&&!memory_sends&&!write_sends&&dma_retain);
  else assert(tone_clear_calls==1&&owner.tone_prepared&&!owner.tone_cleared&&memory_sends==1&&write_sends==2&&dma_retain&&owner.state.uncertain);
 }
 if(mode>=29&&mode<=32){assert(zero_calls==1&&dma_begin==1&&silent_calls==1);if(test=="wire"||test=="outputoff"||test=="mailbox"||test=="iv")assert(dma_end==1&&!dma_retain&&memory_sends==2&&write_sends==2&&owner.transfer_acks==2&&owner.write_ack==2&&owner.silent.data_connected&&owner.silent.data_restored&&owner.dma_restored&&pcm_sends==6);
  else{assert(dma_retain==1&&owner.state.uncertain&&owner.state.clock_held&&owner.state.lease_held&&pcm_sends==4);if(wire_claim_fail||output_cfg_fail)assert(!write_sends&&memory_sends==1&&!dma_end);else if(write_fault)assert(memory_sends==1&&!dma_end&&owner.transfer_acks==(write_fault==3?1u:0u));else if(wire_reset_fail)assert(write_sends==2&&memory_sends==1&&!dma_end);else assert(memory_sends==2&&write_sends==2&&dma_end==(dma_end_fail?1u:0u));}}
 if(mode==28){assert(zero_calls==1&&dma_begin==1);if(test=="write"){assert(write_sends==1&&owner.write.phase==ZizhanWriteDone&&owner.write.ack==1&&!owner.write.uncertain&&memory_sends==2&&dma_end==1&&!dma_retain&&owner.dma_restored&&pcm_sends==6);}
  else{assert(dma_retain==1&&!silent_calls&&pcm_sends==4&&owner.state.uncertain&&owner.state.clock_held&&owner.state.lease_held);if(zero_fail)assert(!write_sends&&!memory_sends&&!dma_end);else assert(write_sends==1&&memory_sends==(memory_fault==2||dma_end_fail?2u:1u)&&dma_end==(dma_end_fail?1u:0u));}}
 if(mode==27){assert(!memory_sends&&dma_begin==1);if(test=="dmacontrol")assert(dma_end==1&&!dma_retain&&owner.dma_restored&&pcm_sends==6);else assert(dma_end==(dma_begin_fail?0u:1u)&&dma_retain==1&&!silent_calls&&pcm_sends==4&&owner.state.uncertain&&owner.state.clock_held);}
 if(test=="native-not-ready"||test=="settle-drop")assert(sends==1&&!owner.state.framework_ready&&!owner.state.request_ack&&!owner.state.uncertain);
 if(test=="settle-frozen")assert(!sends&&!owner.state.framework_ready);
 if(test=="parameter-reject")assert(sends==2&&owner.state.framework_ready&&owner.state.reason==ZizhanPrmRemoteError&&!owner.state.request_ack&&owner.state.uncertain);
 if(test=="clock-pins"||test=="clock-map")assert(sends==2&&owner.state.lease_held&&!owner.state.clock_request_ack&&!owner.state.uncertain);
 if(test=="clock-reject"||test=="clock-release-reject")assert(owner.state.lease_held&&owner.state.uncertain&&!owner.state.release_ack);
 if(mode>=1&&mode<=34&&!audio&&!prepare_mode&&!observe_mode&&adsp==2&&board){unsigned before=allocations;zizhan_audio_prm_probe();assert(allocations==before);}
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-prm-platform-") as temp:
    temp=Path(temp);cpp=temp/"platform.cpp";cpp.write_text(harness.replace("/* SOURCE */",source))
    objects=[]
    for name in ("ShengGpr.c","ShengAudioReach.c","ZizhanAudioBatch.c"):
        obj=temp/(name+".o")
        subprocess.run([os.environ.get("CC","cc"),"-std=c11","-fsanitize=address,undefined","-I"+str(ROOT/"drivers/PongPower"),"-c",str(ROOT/"drivers/PongPower"/name),"-o",str(obj)],check=True,timeout=30)
        objects.append(str(obj))
    binary=temp/"platform"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(ROOT/"drivers/PongPower"),str(cpp),*objects,"-o",str(binary)],check=True,timeout=30)
    for case in ("off","unsupported","unsupported42","play-unattended","play-noservice","service-off","service-nostart","audio","prepare","adsp","board","lock","channel","thread","down","drop","frozen","send","reject-request","reject-release","pass","direct","native","native-ready","native-not-ready","settle-frozen","settle-drop","parameter","parameter-reject","clock","clock-pins","clock-map","clock-reject","clock-release-reject","silent","silent-ampfail","silent-cleanup","silent-timeout","silent-close","silent-observe","silent-notready","silent-rejectclock","pll-capture","tdm","tdm-open","tdm-config","tdm-start","tdm-stop","tdm-close","fsmon","global","global-run","powered-mpu","powered-run","native-frame","core-debug","native-protection","native-rates","asp-rx","asp-format","native-core","native-core-run","pcm","pcm-open","pcm-media","pcm-start","pcm-stop","pcm-close","joint","joint-stop","joint-close","dma","dma-missing","dma-lock","dma-map","dmamap","dmamap-begin","dmamap-map","dmamap-unmap","dmamap-end","dmacontrol","dmacontrol-begin","dmacontrol-end","write","write-zero","write-bad","write-timeout","write-unmap","write-end","wire","wire-claim","wire-data","wire-second","wire-reset","wire-unmap","wire-end","wire-timeout","outputoff","outputoff-config","mailbox","iv","unattended-pulse","pulse","pulse-preroll","pulse-timeout","pulse-tone-timeout","tone","tone-prepare","tone-clear"):
        subprocess.run([str(binary),case],check=True,timeout=10)
print("PASS actual PRM platform:94 controls,defaultoff/conflicts,owned nativeframe/coredebug/TDM/HALO,graphstop/close before release,strict retention;no hardware")
