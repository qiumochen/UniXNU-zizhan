/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#include "ShengAdsp.h"
#include "ZizhanAudioPrm.h"
#include "ZizhanAudioService.h"
#include "ZizhanAudioSilent.h"
#include "ZizhanAudioGraph.h"
#include "ZizhanTdm.h"
#include "ZizhanPcm.h"
#include "ZizhanAudioBatch.h"
#include "ZizhanAudioDma.h"
#include "ZizhanAudioDmaMap.h"
#include "ZizhanAudioMemory.h"
#include "ZizhanAudioWrite.h"
#include "ZizhanAudioZero.h"
#include "ZizhanAudioTone.h"
#include "ZizhanAudioTone48.h"
extern "C" bool pong_smmu_lease_lock(void);
extern "C" void pong_smmu_lease_unlock(void);
extern "C" boolean_t pmap_valid_page(ppnum_t);
namespace {
struct Packet {unsigned bytes;uint8_t data[512];};
struct Runtime {
 unsigned ready_before,ready_after,watermark_gate,underrun_gate,pack_verified;
 unsigned packets,acks,drained,ready_cleared;uint64_t frames;
};
struct Owner {
 IOLock *lock;ShengAdspChannel *channel;Packet queue[16];
 unsigned head,count,up,ever_up,closed,overflow;ZizhanPrmState state;
 unsigned mode,clock_isolated;
 unsigned silent_called;ZizhanAudioSilentReceipt silent;
 ZizhanTdmState tdm;ZizhanPcmState pcm;ZizhanMemoryState memory;unsigned dma_mapped,dma_restored;
 ZizhanWriteState write;unsigned zero_verified;
 unsigned tone_prepared,tone_cleared;
 unsigned transfer_calls,transfer_acks,write_sent,write_ack;
 unsigned graph_acks,batch_acks,tone48_prepared,tone48_cleared;ZizhanAudioBatch batch;
 Runtime runtime;
};
Owner owner={};
uint64_t now_us(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
bool clock_pins_isolated(unsigned data_pads=0){
 /* Saved live DT:TLMM121=SCK/123=WS. This increment may vote the
  * internal clock only while both pads remain GPIO inputs. Read-only
  * mappings,no mux/data/OE write. A pad in any other mode fails closed. */
 const unsigned pins[]={121,123,122,124};for(unsigned i=0;i<(data_pads?4u:2u);++i){const unsigned pin=pins[i];
  auto memory=IODeviceMemory::withRange(0xf100000ull+pin*0x1000ull,0x1000);
  auto map=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly):nullptr;
  if(memory){memory->release();}if(!map)return false;
  auto reg=reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());const uint32_t first=*reg;
#if defined(__aarch64__) && defined(KERNEL)
  __asm__ volatile("dmb oshld":::"memory");
#endif
  const uint32_t second=*reg;map->release();
  const bool isolated=first==second&&!(first&0x23cu);
  IOLog("ZIZHAN_AUDIO_PRM: isolated pin=%u cfg=%x/%x gpio_input=%u readonly=1\n",pin,first,second,isolated?1u:0u);
  if(!isolated)return false;
 }
 return true;
}
void channel_state(void *context,int up){auto &o=*static_cast<Owner *>(context);IOLockLock(o.lock);
 o.up=up?1u:0u;if(up)o.ever_up=1;else if(o.ever_up)o.closed=1;IOLockUnlock(o.lock);}
void receive(void *context,const uint8_t *bytes,uint32_t length){auto &o=*static_cast<Owner *>(context);IOLockLock(o.lock);
 if(length<=512&&o.count<16){auto &p=o.queue[(o.head+o.count)%16];p.bytes=length;memcpy(p.data,bytes,length);++o.count;}else ++o.overflow;
 IOLockUnlock(o.lock);}
// 11-18: the worker thread runs the whole amp/WRITE path on a 16 KiB stack; keep the packet
// builders below out of its permanent frame.
__attribute__((noinline)) bool tdm_commands(bool close){
 auto &o=owner;if(close){if(o.tdm.phase!=ZizhanTdmHold||!o.tdm.running||o.tdm.waiting)return false;o.tdm.phase=ZizhanTdmStop;}
 for(unsigned iteration=0;iteration<(close?400u:800u);++iteration){
  Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
  if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
  if(closed||overflow){zizhan_tdm_fail(&o.tdm,5);break;}
  if(p.bytes){int accepted=zizhan_tdm_receive(&o.tdm,p.data,p.bytes);
   IOLog("ZIZHAN_AUDIO_TDM: rx bytes=%u accepted=%d phase=%u remote=%u ignored=%u\n",p.bytes,accepted,o.tdm.phase,o.tdm.status,o.tdm.ignored);}
  const uint64_t now=now_us();zizhan_tdm_tick(&o.tdm,now);
  if(o.tdm.phase==ZizhanTdmFailed)return false;
  if(o.tdm.phase==(close?ZizhanTdmDone:ZizhanTdmHold))return true;
  uint8_t bytes[512];unsigned n=zizhan_tdm_next(&o.tdm,now,bytes);
  if(n){IOLog("ZIZHAN_AUDIO_TDM: send phase=%u opcode=%x token=%x bytes=%u stream=0 dma=0 data_pads=0\n",o.tdm.phase,o.tdm.opcode,o.tdm.token,n);
   if(sheng_adsp_send(o.channel,bytes,n)){zizhan_tdm_fail(&o.tdm,5);return false;}}
  IOSleep(10);
 }
 zizhan_tdm_fail(&o.tdm,4);return false;
}
__attribute__((noinline)) bool pcm_commands(bool close){
 auto &o=owner;o.pcm.joint=o.mode>=24?1u:0u;if(close){if(o.pcm.phase!=ZizhanPcmHold||!o.pcm.running||o.pcm.waiting)return false;o.pcm.phase=ZizhanPcmStop;}
 for(unsigned iteration=0;iteration<(close?400u:800u);++iteration){
  Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
  if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
  if(closed||overflow){zizhan_pcm_fail(&o.pcm,5);break;}
  if(p.bytes){const unsigned phase_before=o.pcm.phase;int accepted=zizhan_pcm_receive(&o.pcm,p.data,p.bytes);
   if(accepted==1&&o.pcm.joint){
    if(phase_before==ZizhanPcmStop){o.tdm.running=0;o.tdm.phase=ZizhanTdmClose;}
    if(phase_before==ZizhanPcmClose){o.tdm.opened=0;o.tdm.phase=ZizhanTdmDone;}
   }
   IOLog("ZIZHAN_AUDIO_PCM: rx bytes=%u accepted=%d phase=%u remote=%u ignored=%u\n",p.bytes,accepted,o.pcm.phase,o.pcm.status,o.pcm.ignored);}
  const uint64_t now=now_us();zizhan_pcm_tick(&o.pcm,now);
  if(o.pcm.phase==ZizhanPcmFailed)return false;
  if(o.pcm.phase==(close?ZizhanPcmDone:ZizhanPcmHold))return true;
  uint8_t bytes[512];unsigned n=zizhan_pcm_next(&o.pcm,now,bytes);
  if(n){IOLog("ZIZHAN_AUDIO_PCM: send phase=%u opcode=%x token=%x bytes=%u map=0 data=0 audio_output=0\n",o.pcm.phase,o.pcm.opcode,o.pcm.token,n);
   if(sheng_adsp_send(o.channel,bytes,n)){zizhan_pcm_fail(&o.pcm,5);return false;}}
  IOSleep(10);
 }
 zizhan_pcm_fail(&o.pcm,4);return false;
}
__attribute__((noinline)) bool memory_commands(bool unmap){
 auto &o=owner;if(unmap){if(o.memory.phase!=ZizhanMemoryHeld||!o.memory.handle)return false;o.memory.phase=ZizhanMemoryUnmap;}
 else o.memory.address=zizhan_audio_dma_map_address();
 for(unsigned iteration=0;iteration<400;++iteration){
  Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
  if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
  if(closed||overflow){zizhan_memory_fail(&o.memory,5);return false;}
  if(p.bytes){const int accepted=zizhan_memory_receive(&o.memory,p.data,p.bytes);
   IOLog("ZIZHAN_AUDIO_MEMORY: rx bytes=%u accepted=%d phase=%u handle=%x remote=%u ignored=%u\n",p.bytes,accepted,o.memory.phase,o.memory.handle,o.memory.remote,o.memory.ignored);}
  const uint64_t now=now_us();zizhan_memory_tick(&o.memory,now);
  if(o.memory.phase==ZizhanMemoryFailed)return false;
  if(o.memory.phase==(unmap?ZizhanMemoryDone:ZizhanMemoryHeld))return true;
  uint8_t bytes[512];const unsigned n=zizhan_memory_next(&o.memory,now,bytes);
  if(n){IOLog("ZIZHAN_AUDIO_MEMORY: send opcode=%x token=%x bytes=%u address=%llx data=0\n",o.memory.opcode,o.memory.token,n,o.memory.address);
   if(sheng_adsp_send(o.channel,bytes,n)){zizhan_memory_fail(&o.memory,5);return false;}}
  IOSleep(10);
 }
 zizhan_memory_fail(&o.memory,4);return false;
}
bool graph_send(uint8_t *bytes,unsigned n,uint32_t opcode,uint32_t token,uint32_t port){
 auto &o=owner;if(!n||sheng_adsp_send(o.channel,bytes,n))return false;
 IOLog("ZIZHAN_AUDIO_GRAPH: send opcode=%x token=%x port=%x bytes=%u data=0 audio_output=0\n",
  opcode,token,port,n);
 for(unsigned iteration=0;iteration<400;++iteration){
  Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
  if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
  if(closed||overflow)return false;
  if(p.bytes){
   GprPacket raw;
   const int decoded=gpr_decode(p.data,p.bytes,&raw);
   if(decoded==GprOk&&raw.size==p.bytes&&raw.dst_port==port&&
      raw.token==token&&raw.opcode==GPR_OP_BASIC_RESULT){
    if(raw.payload_bytes!=8||zizhan_tdm_le32(raw.payload)!=opcode)return false;
    const uint32_t status=zizhan_tdm_le32(raw.payload+4);
    IOLog("ZIZHAN_AUDIO_GRAPH: ack opcode=%x token=%x src=%x dst=%x status=%u\n",
     opcode,token,raw.src_port,raw.dst_port,status);
    return status==0;
   }
   if(decoded==GprOk)IOLog("ZIZHAN_AUDIO_GRAPH: observe opcode=%x token=%x src=%x dst=%x payload=%u\n",
    raw.opcode,raw.token,raw.src_port,raw.dst_port,raw.payload_bytes);
   else IOLog("ZIZHAN_AUDIO_GRAPH: undecodable bytes=%u decode=%d\n",p.bytes,decoded);
  }
  IOSleep(10);
 }
 IOLog("ZIZHAN_AUDIO_GRAPH: timeout opcode=%x token=%x port=%x\n",opcode,token,port);
 return false;
}
__attribute__((noinline)) bool batch_round(bool zero){
 auto &o=owner;uint8_t out[512];
 if(!o.dma_mapped||!o.memory.handle)return false;
 if(zero){o.zero_verified=zizhan_audio_zero_verify()?1u:0u;if(!o.zero_verified)return false;}
 else if(!o.tone48_prepared)return false;
 if(!zizhan_batch_init_ex(&o.batch,zizhan_audio_dma_map_address(),o.memory.handle,65536,2880,480,4,now_us()))
  return false;
 for(unsigned slot=0;slot<4;++slot){
  if(!zizhan_batch_prepared(&o.batch,slot,2880))return false;
  const uint64_t send_at=now_us();
  const unsigned n=zizhan_batch_next(&o.batch,send_at,out);
  if(!n||sheng_adsp_send(o.channel,out,n))return false;
  bool done=false;
  for(unsigned iteration=0;iteration<40;++iteration){
   Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
   if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
   if(closed||overflow)return false;
   if(p.bytes){
    const int accepted=zizhan_batch_receive(&o.batch,p.data,p.bytes,now_us());
    if(accepted<0)return false;
    if(accepted==1){done=true;break;}
   }
   const uint64_t now=now_us();zizhan_batch_tick(&o.batch,now);
   if(o.batch.phase==ZizhanBatchFailed)return false;
   IOSleep(10);
  }
  if(!done)return false;
  ++o.batch_acks;
 }
 IOLog("ZIZHAN_AUDIO_GRAPH_DATA: PASS sent=4 ack=%u frames=%llu bytes_each=2880 data=%s audio_output=0\n",
  o.batch_acks,(unsigned long long)o.batch.confirmed_frames,zero?"zero":"known48");
 return zizhan_batch_buffers_returned(&o.batch)&&o.batch.confirmed_frames==1920;
}
int16_t runtime_sample(uint64_t frame){
 // 1kHz square at 48kHz, amplitude 512 (~-36 dBFS), deterministic only.
 return ((frame/24u)&1u)?512:-512;
}
/* Mode 38: the verified linked graph, but each 2880-byte period is filled
 * from the 48k/2ch int16 HAL-style ring through the service gate. A
 * kernel-side finite producer publishes out_written; Ready must not appear
 * before a stable watermark, an uncovered period must fail as an underrun
 * without silence, and the last in-flight period must drain after stop.
 * No userland HAL, data pads, amplifier or output is involved. */
__attribute__((noinline)) bool runtime_round(){
 auto &o=owner;auto &r=o.runtime;
 const uint64_t tagged=zizhan_audio_dma_map_address(),base=uint32_t(tagged);
 if((tagged>>32)!=1||!base||(base&4095)||base>0xffff0000ull||!o.dma_mapped||!o.memory.handle)return false;
 for(unsigned i=0;i<16;++i)if(pmap_valid_page((ppnum_t)((base>>12)+i)))return false;
 auto memory=IODeviceMemory::withRange(base,65536);
 auto map=memory?memory->map(kIOMapAnywhere|kIOMapWriteCombineCache):nullptr;if(memory)memory->release();
 if(!map)return false;
 auto dma=reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
 auto control=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,IOOptionBits(kIODirectionInOut),4096,4096);
 auto ringmd=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,IOOptionBits(kIODirectionInOut),ZIZHAN_AUDIO_RING_FRAMES*4,4096);
 if(!control||!ringmd){if(control)control->release();if(ringmd)ringmd->release();map->release();return false;}
 auto ctl=reinterpret_cast<ZizhanAudioControl *>(control->getBytesNoCopy());
 auto ring=reinterpret_cast<int16_t *>(ringmd->getBytesNoCopy());
 bzero(ctl,4096);bzero(ring,ZIZHAN_AUDIO_RING_FRAMES*4);
 ctl->magic=ZIZHAN_AUDIO_MAGIC;ctl->version=ZIZHAN_AUDIO_VERSION;ctl->rate=ZIZHAN_AUDIO_RATE;
 ctl->out_channels=ZIZHAN_AUDIO_OUT_CHANNELS;ctl->in_channels=ZIZHAN_AUDIO_IN_CHANNELS;
 ctl->period_frames=ZIZHAN_AUDIO_PERIOD_FRAMES;ctl->ring_frames=ZIZHAN_AUDIO_RING_FRAMES;
 ctl->lead_frames=ZIZHAN_AUDIO_LEAD_FRAMES;ctl->out_anchor_host=mach_absolute_time();
 mach_timebase_info_data_t tb;clock_timebase_info(&tb);
 ctl->ticks_per_frame_q32=((1000000000ull<<32)/48000)*tb.denom/tb.numer;
 ZizhanAudioService svc={};
 bool ok=zizhan_service_init(&svc,ctl,tagged,o.memory.handle,4,0,now_us())&&
         zizhan_service_map(&svc,1)&&zizhan_service_graph_started(&svc);
 r.ready_before=ok&&(ctl->state&ZizhanAudioOutputReady)?1u:0u;
 if(ok&&!zizhan_service_publish_ready(&svc))r.watermark_gate=!(ctl->state&ZizhanAudioOutputReady)?1u:0u;
 uint8_t expect[ZizhanRingPcmBytes];unsigned slot=0;bool stopped=false;
 while(ok&&slot<4){
  const uint64_t frame=uint64_t(slot)*ZIZHAN_AUDIO_PERIOD_FRAMES;
  for(unsigned i=0;i<ZIZHAN_AUDIO_PERIOD_FRAMES;++i){
   const uint64_t at=frame+i;const unsigned index=unsigned(at%ZIZHAN_AUDIO_RING_FRAMES);
   ring[2*index]=runtime_sample(at);ring[2*index+1]=int16_t(-runtime_sample(at));
  }
  ZizhanRingReceipt receipt={};
  if(!zizhan_ring_pack(ring,ZIZHAN_AUDIO_RING_FRAMES,frame,expect,&receipt)||receipt.packed!=sizeof expect){ok=false;break;}
  if(slot==2){
   // The producer has not covered this period: staging must fail with an
   // underrun, not advance the slot and not fill silence.
   zizhan_audio_publish_written(ctl,frame);
   if(zizhan_service_stage(&svc,ring,slot,const_cast<uint8_t *>(dma)+slot*0x3000u)){ok=false;break;}
   r.underrun_gate=svc.stream.reason==ZizhanStreamUnderrun&&svc.phase==ZizhanServiceReady?1u:0u;
  }
  zizhan_audio_publish_written(ctl,frame+ZIZHAN_AUDIO_PERIOD_FRAMES);
  if(slot==0){
   if(!zizhan_service_publish_ready(&svc)){ok=false;break;}
   r.ready_after=(ctl->state&ZizhanAudioOutputReady)?1u:0u;
  }
  auto dst=const_cast<uint8_t *>(dma)+slot*0x3000u;
  if(!zizhan_service_stage(&svc,ring,slot,dst)){ok=false;break;}
#if defined(__aarch64__) && defined(KERNEL)
  // The slot is device-like write-combining memory: flush before the CPU
  // readback and before the DSP may consume the period.
  __asm__ volatile("dsb sy":::"memory");
#endif
  if(memcmp(dst,expect,sizeof expect)){ok=false;break;}
  ++r.pack_verified;
  uint8_t tx[512];const unsigned n=zizhan_service_commit(&svc,slot,now_us(),tx);
  if(!n||sheng_adsp_send(o.channel,tx,n)){ok=false;break;}
  ++r.packets;
  bool done=false;
  for(unsigned iteration=0;iteration<40&&ok&&!done;++iteration){
   Packet p={};unsigned closed,overflow;
   IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
   if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}
   IOLockUnlock(o.lock);
   if(closed||overflow){ok=false;break;}
   if(p.bytes){
    const int accepted=zizhan_service_receive(&svc,p.data,p.bytes,now_us());
    if(accepted<0){ok=false;break;}
    if(accepted==1)done=true;
   }
   if(!done&&svc.phase==ZizhanServiceFailed){ok=false;break;}
   if(!done)IOSleep(10);
  }
  if(!done){ok=false;break;}
  ++r.acks;
  if(slot==3&&!stopped){
   // Stop with the fourth period in flight; its exact DONE must still
   // arrive before the batch can be considered drained.
   zizhan_service_stop(&svc);stopped=true;
   r.ready_cleared=!(ctl->state&(ZizhanAudioOutputReady|ZizhanAudioOutputRunning))?1u:0u;
  }
  ++slot;
 }
 if(ok&&!stopped){zizhan_service_stop(&svc);stopped=true;r.ready_cleared=!(ctl->state&(ZizhanAudioOutputReady|ZizhanAudioOutputRunning))?1u:0u;}
 if(ok){r.frames=svc.stream.batch.confirmed_frames;r.drained=zizhan_service_done(&svc)?1u:0u;}
 ctl->state&=~(ZizhanAudioOutputReady|ZizhanAudioOutputRunning);
 map->release();control->release();ringmd->release();
 return ok&&r.ready_before==0&&r.ready_after==1&&r.watermark_gate==1&&r.underrun_gate==1&&
        r.pack_verified==4&&r.packets==4&&r.acks==4&&r.drained==1&&r.ready_cleared==1&&r.frames==1920;
}
__attribute__((noinline)) bool graph_round(){
 auto &o=owner;uint8_t b[512];unsigned n;
 n=zizhan_mfc_device_open(b,sizeof b,0x7a300001u);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_OPEN,0x7a300001u,ZizhanMfcPort)){return false;}++o.graph_acks;
 n=zizhan_mfc_device_config(b,sizeof b,0x7a300002u);
 if(!n||!graph_send(b,n,AR_OP_SET_CFG,0x7a300002u,ZizhanMfcPort)){return false;}++o.graph_acks;
 n=zizhan_pcm_open_to_mfc(b,sizeof b,0x7a300003u);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_OPEN,0x7a300003u,ZizhanPcmPort)){return false;}++o.graph_acks;
 n=zizhan_pcm_media_packed48(b,sizeof b,0x7a300004u);
 if(!n||!graph_send(b,n,AR_OP_SET_CFG,0x7a300004u,ZizhanPcmPort)){return false;}++o.graph_acks;
 o.dma_mapped=zizhan_audio_dma_map_begin()?1u:0u;
 if(!o.dma_mapped||!memory_commands(false))return false;
 if(o.mode==37){o.tone48_prepared=zizhan_audio_tone48_prepare()?1u:0u;if(!o.tone48_prepared)return false;}
 n=zizhan_graph_command(b,sizeof b,ZizhanMfcPort,0x7a300005u,AR_OP_GRAPH_PREPARE,ZizhanMfcSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_PREPARE,0x7a300005u,ZizhanMfcPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanMfcPort,0x7a300006u,AR_OP_GRAPH_START,ZizhanMfcSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_START,0x7a300006u,ZizhanMfcPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanPcmPort,0x7a300007u,AR_OP_GRAPH_PREPARE,ZizhanPcmSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_PREPARE,0x7a300007u,ZizhanPcmPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanPcmPort,0x7a300008u,AR_OP_GRAPH_START,ZizhanPcmSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_START,0x7a300008u,ZizhanPcmPort)){return false;}++o.graph_acks;
 IOLog("ZIZHAN_AUDIO_GRAPH: RUNNING device_sg=%x stream_sg=%x mfc=%x tdm=%x data=0 audio_output=0\n",
  ZizhanMfcSg,ZizhanPcmSg,ZizhanMfcIid,ZizhanTdmIid);
 if(o.mode==36||o.mode==37){if(!batch_round(o.mode==36))return false;}
 else if(o.mode==38){if(!runtime_round())return false;}
 else IOSleep(1000);
 n=zizhan_graph_command(b,sizeof b,ZizhanPcmPort,0x7a300009u,AR_OP_GRAPH_STOP,ZizhanPcmSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_STOP,0x7a300009u,ZizhanPcmPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanPcmPort,0x7a30000au,AR_OP_GRAPH_CLOSE,ZizhanPcmSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_CLOSE,0x7a30000au,ZizhanPcmPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanMfcPort,0x7a30000bu,AR_OP_GRAPH_STOP,ZizhanMfcSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_STOP,0x7a30000bu,ZizhanMfcPort)){return false;}++o.graph_acks;
 n=zizhan_graph_command(b,sizeof b,ZizhanMfcPort,0x7a30000cu,AR_OP_GRAPH_CLOSE,ZizhanMfcSg);
 if(!n||!graph_send(b,n,AR_OP_GRAPH_CLOSE,0x7a30000cu,ZizhanMfcPort)){return false;}++o.graph_acks;
 if(o.mode==37){o.tone48_cleared=zizhan_audio_tone48_clear()?1u:0u;if(!o.tone48_cleared)return false;}
 if(!memory_commands(true))return false;
 o.dma_restored=zizhan_audio_dma_map_end()?1u:0u;
 return o.dma_restored!=0;
}
bool write_commands(unsigned sequence=0,bool silence=false,bool again=false){
 auto &o=owner;if(!o.zero_verified||o.memory.phase!=ZizhanMemoryHeld||!o.memory.handle)return false;
 if(o.mode>=33&&!o.tone_prepared)return false;
 // again (PRM34 zizhan_tone_repeat only): resend the same completed buffer with the same sequence.
 if(again){if(o.mode!=34||silence||o.write.phase!=ZizhanWriteDone||o.write.sequence!=sequence)return false;o.write={};}
 else if(sequence){if(o.write.phase!=ZizhanWriteDone||sequence>3||sequence!=o.write.sequence+1)return false;o.write={};}
 o.write.sequence=sequence;
 // zizhan_pre_off=<bytes> (diagnostics): offset of the PRM34 zero pre-roll inside the 64 KiB map,
 // page aligned and below 48 KiB; default 16 KiB. 11-17: the first ADSP read at 16 KiB hangs the SoC.
 unsigned preroll=16384u;if(silence){unsigned value=0;if(PE_parse_boot_argn("zizhan_pre_off",&value,sizeof value)&&value<=49152u&&!(value&4095u))preroll=value;}
 o.write.address=zizhan_audio_dma_map_address()+(silence?preroll:0u);o.write.handle=o.memory.handle;
 for(unsigned iteration=0;iteration<400;++iteration){
  Packet p={};unsigned closed,overflow;IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;
  if(o.count){p=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}IOLockUnlock(o.lock);
  if(closed||overflow){zizhan_write_fail(&o.write,5);return false;}
  if(p.bytes){const int accepted=zizhan_write_receive(&o.write,p.data,p.bytes);if(accepted==1)++o.write_ack;
   IOLog("ZIZHAN_AUDIO_WRITE: rx bytes=%u accepted=%d phase=%u remote=%u ignored=%u\n",p.bytes,accepted,o.write.phase,o.write.remote,o.write.ignored);}
  const uint64_t now=now_us();if(o.mode==34&&o.write.phase==ZizhanWriteWaiting&&now-o.write.began_us>=100000)zizhan_write_fail(&o.write,4);zizhan_write_tick(&o.write,now);
  if(o.write.phase==ZizhanWriteFailed){return false;}if(o.write.phase==ZizhanWriteDone)return true;
  uint8_t bytes[512];const unsigned n=zizhan_write_next(&o.write,now,bytes);
  if(n){++o.write_sent;IOLog("ZIZHAN_AUDIO_WRITE: send bytes=%u token=%x address=%llx handle=%x data_bytes=%u zero_only=%u data_pads=%u audio_output=%u sequence=%u\n",n,o.write.token,o.write.address,o.write.handle,ZizhanWriteBytes,o.mode>=33&&!silence?0u:1u,o.mode>=29?1u:0u,o.mode==34&&!silence?1u:0u,sequence);
   if(sheng_adsp_send(o.channel,bytes,n)){zizhan_write_fail(&o.write,5);return false;}}
  IOSleep(10);
 }
 zizhan_write_fail(&o.write,4);return false;
}
int zero_transfer(void *context,unsigned chip){
 auto &o=*static_cast<Owner *>(context);
 if(o.mode==34){const unsigned order[]={2,0,3,1};if(o.transfer_calls>=4||chip!=order[o.transfer_calls])return 0;
  const unsigned sequence=o.transfer_calls++;if(!write_commands(sequence,chip>=2))return 0;
  // zizhan_tone_repeat=<n> (attended diagnostics, n<=20): queue the finite tone n more times while AMP is on.
  // The pulse keeps its 250 ms enable budget; acks of repeats are not counted as protocol acks.
  if(chip<2){unsigned repeat=0;if(PE_parse_boot_argn("zizhan_tone_repeat",&repeat,sizeof repeat)&&repeat<=20){
   const unsigned acks=o.write_ack;for(unsigned i=0;i<repeat;++i)if(!write_commands(sequence,false,true)){o.write_ack=acks;return 0;}
   o.write_ack=acks;IOLog("ZIZHAN_AUDIO_WRITE: repeated=%u sequence=%u\n",repeat,sequence);}}
  ++o.transfer_acks;return 1;}
 if((o.mode<29||o.mode>33)||chip>1||chip!=o.transfer_calls)return 0;
 ++o.transfer_calls;if(!write_commands(chip))return 0;++o.transfer_acks;return 1;
}
__attribute__((noinline)) bool dma_observe(){
 auto chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 auto data=chosen?OSDynamicCast(OSData,chosen->getProperty("pong-audio-dma")):nullptr;
 uint64_t extent[2]={};if(data&&data->getLength()==sizeof extent)memcpy(extent,data->getBytesNoCopy(),sizeof extent);if(chosen)chosen->release();
 if(!extent[0]||(extent[0]&4095)||extent[1]!=65536||extent[0]>0x100000000ull-extent[1]){
  IOLog("ZIZHAN_AUDIO_DMA: SKIP no-actual-UEFI-extent writes=0 data=0\n");return false;}
 struct Lease{bool held;Lease():held(pong_smmu_lease_lock()){}~Lease(){if(held)pong_smmu_lease_unlock();}} lease;
 if(!lease.held){IOLog("ZIZHAN_AUDIO_DMA: SKIP no-global-owner writes=0 data=0\n");return false;}
 auto memory=IODeviceMemory::withRange(0x15000000ull,0x100000);
 auto map=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly):nullptr;if(memory)memory->release();
 if(!map){IOLog("ZIZHAN_AUDIO_DMA: FAIL readonly-map writes=0 data=0\n");return false;}
 auto registers=reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
 auto read=[&](unsigned at){const uint32_t value=registers[at/4];
#if defined(__aarch64__) && defined(KERNEL)
  __asm__ volatile("dmb oshld":::"memory");
#endif
  return value;};
 const auto capture=zizhan_audio_dma::inspect(extent[0],extent[1],read);map->release();
 IOLog("ZIZHAN_AUDIO_DMA: %s reason=%u base=%llx bytes=%llu reads=%u stable=%u banks=%u groups=%u pages=%u writes=0 data=0\n",
  capture.status==zizhan_audio_dma::Pass?"PASS":"FAIL",capture.reason,extent[0],extent[1],capture.reads,capture.stable,capture.banks,capture.groups,capture.pages);
 IOLog("ZIZHAN_AUDIO_DMA: id0=%x id1=%x id2=%x plan_ok=%u slot=%u bank=%u previous=%u reuse=%u migrate=%u error=%s\n",
  capture.id[0],capture.id[1],capture.id[2],capture.plan.error?0u:1u,capture.plan.slot,capture.plan.bank,capture.plan.previousBank,capture.plan.reuse?1u:0u,capture.plan.migrate?1u:0u,capture.plan.error?capture.plan.error:"none");
 if(capture.stable){for(unsigned i=0;i<capture.groups;++i)IOLog("ZIZHAN_AUDIO_DMA_RAW: slot=%u smr=%x s2cr=%x readonly=1\n",i,capture.smr[i],capture.s2cr[i]);
  for(unsigned i=0;i<capture.banks;++i)IOLog("ZIZHAN_AUDIO_DMA_CB: bank=%u sctlr=%x cbar=%x cba2r=%x readonly=1\n",i,capture.sctlr[i],capture.cbar[i],capture.cba2r[i]);}
 return capture.status==zizhan_audio_dma::Pass;
}
void worker(void *,wait_result_t){
 thread_set_thread_name(current_thread(),"zizhan_audio_prm");auto &o=owner;
 const uint64_t start=now_us();bool linked=false;const uint64_t worker_limit=o.mode>=7?90000000u:40000000u;
 /* Retained static client context never freed while callbacks may arrive.
  * Whole worker cap40s,iteration4000 prevents frozen-clock hangs. */
 for(unsigned iteration=0;iteration<(o.mode>=7?9000u:4000u);++iteration){
  uint64_t now=now_us();Packet packet={};unsigned up,closed,overflow;
  IOLockLock(o.lock);up=o.up;closed=o.closed;overflow=o.overflow;
  if(o.count){packet=o.queue[o.head];o.head=(o.head+1)%16;--o.count;}
  IOLockUnlock(o.lock);
  if(!linked&&up){linked=true;zizhan_prm_init(&o.state,now);
   if(o.mode==2||o.mode==3)o.state.phase=ZizhanPrmRequestReady;
   if(o.mode>=3)o.state.native_wire=1;
   if(o.mode>=4)o.state.native_framework=1;
   if(o.mode>=5)o.state.native_parameter_reply=1;
   if(o.mode>=6)o.state.clock_mode=1;
   IOLog("ZIZHAN_AUDIO_PRM: linked channel=adsp_apps port=%x no-codec-access=%u\n",o.mode>=4?3u:o.mode==3?7u:0x2101u,o.mode>=7?0u:1u);
   IOLog("ZIZHAN_AUDIO_PRM: mode=%u framework_query=%u framework_verified=0\n",o.mode,(o.mode==1||o.mode>=4)?1u:0u);
   if(o.mode>=4)IOLog("ZIZHAN_AUDIO_PRM: settle_us=1000000 native_apm_port=3 token=0 pdr_verified=0\n");}
  if(closed||overflow){zizhan_prm_fail(&o.state,ZizhanPrmTransport);break;}
  if(now<start||now-start>=worker_limit){zizhan_prm_fail(&o.state,ZizhanPrmTimeout);break;}
  if(linked){
   if(packet.bytes){
    if(o.mode>=5){
     GprPacket raw;
     if(gpr_decode(packet.data,packet.bytes,&raw)==GprOk&&raw.size==packet.bytes&&raw.payload_bytes<=8)
      IOLog("ZIZHAN_AUDIO_PRM: raw opcode=%x payload=%u word0=%x word1=%x\n",raw.opcode,raw.payload_bytes,
       raw.payload_bytes>=4?zizhan_prm_le32(raw.payload):0,raw.payload_bytes>=8?zizhan_prm_le32(raw.payload+4):0);
    }
    const int accepted=zizhan_prm_receive(&o.state,packet.data,packet.bytes);
    IOLog("ZIZHAN_AUDIO_PRM: rx bytes=%u accepted=%d phase=%u opcode=%x payload=%u remote=%u ignored=%u\n",
      packet.bytes,accepted,o.state.phase,o.state.last_opcode,o.state.last_payload_bytes,o.state.last_remote_status,o.state.ignored);
   }
   zizhan_prm_tick(&o.state,now);
   if(o.state.phase==ZizhanPrmDone||o.state.phase==ZizhanPrmFailed)break;
   if(o.mode>=6&&o.state.phase==ZizhanPrmClockRequestReady){
    o.clock_isolated=clock_pins_isolated(o.mode>=9?1u:0u)?1u:0u;
    if(!o.clock_isolated){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);break;}
   }
   if(o.mode>=7&&o.state.phase==ZizhanPrmClockReleaseReady&&!o.silent_called){
    if(o.mode==25&&!dma_observe()){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
    o.silent_called=1;const uint64_t hold_begin=now_us();
    IOLog("ZIZHAN_AUDIO_SILENT: RUNNING same-thread=1 core_held=1 clock_held=1 hold_limit_us=60000000 audio_output=0\n");
    if(o.mode==35||o.mode==36||o.mode==37||o.mode==38){
     if(!graph_round()){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
     o.silent.release_safe=1;
    } else {
    if(o.mode>=9&&!tdm_commands(false)){
     zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;
    }
    if(o.mode>=23&&!pcm_commands(false)){
     zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;
    }
    if(o.mode>=26){
     o.dma_mapped=zizhan_audio_dma_map_begin()?1u:0u;
     if(o.mode>=28&&o.dma_mapped)o.zero_verified=zizhan_audio_zero_verify()?1u:0u;
     if(!o.dma_mapped||(o.mode>=28&&!o.zero_verified)||(o.mode>=33&&!(o.tone_prepared=zizhan_audio_tone_prepare()?1u:0u))||(o.mode!=27&&!memory_commands(false))||
       (o.mode==28&&!write_commands())){
      zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
     // The actual graph CLOSE precedes loss of SMMU MMIO access in the
     // rejected mode26 run. Complete this lease while the native graphs
     // are still START and PRM core/clock are held; no DSP handle remains.
     if(o.mode<29){
      if(o.mode!=27&&!memory_commands(true)){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
      o.dma_restored=zizhan_audio_dma_map_end()?1u:0u;
      if(!o.dma_restored){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
     }
    }
    if(o.mode>=29){const ZizhanAudioTransfer transfer={&o,zero_transfer};
     if(o.mode==34)zizhan_amp_native_pulse_run(&o.state,&o.silent,&transfer);
     else if(o.mode>=32)zizhan_amp_native_iv_run(&o.state,&o.silent,&transfer);
     else if(o.mode==31)zizhan_amp_native_mailbox_run(&o.state,&o.silent,&transfer);
     else if(o.mode==30)zizhan_amp_native_output_off_run(&o.state,&o.silent,&transfer);
     else zizhan_amp_native_data_run(&o.state,&o.silent,&transfer);
     if(!o.silent.amp_pass||!o.silent.release_safe||!o.silent.data_connected||!o.silent.data_restored||o.transfer_acks!=(o.mode==34?4u:2u)||o.write_ack!=(o.mode==34?4u:2u)||
       (o.mode>=30&&!o.silent.output_configured)||(o.mode>=31&&!o.silent.mailbox_verified)||(o.mode>=32&&!o.silent.iv_verified)||(o.mode==34&&!o.silent.pulse_verified)||
       (o.mode>=33&&!(o.tone_cleared=zizhan_audio_tone_clear()?1u:0u))||
       !memory_commands(true)){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
     o.dma_restored=zizhan_audio_dma_map_end()?1u:0u;
     if(!o.dma_restored){zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;}
    }
    else if(o.mode>=23)zizhan_amp_native_core_run(&o.state,&o.silent);
    else if(o.mode==22)zizhan_amp_native_core_run(&o.state,&o.silent);
    else if(o.mode==21)zizhan_amp_native_core_capture(&o.state,&o.silent);
    else if(o.mode==20)zizhan_amp_asp_format_run(&o.state,&o.silent);
    else if(o.mode==19)zizhan_amp_asp_rx_run(&o.state,&o.silent);
    else if(o.mode==18)zizhan_amp_native_rates_run(&o.state,&o.silent);
    else if(o.mode==17)zizhan_amp_native_protection_run(&o.state,&o.silent);
    else if(o.mode==16)zizhan_amp_native_debug_run(&o.state,&o.silent);
    else if(o.mode==15)zizhan_amp_native_frame_run(&o.state,&o.silent);
    else if(o.mode==14)zizhan_amp_global_observed_run(&o.state,&o.silent);
    else if(o.mode==13)zizhan_amp_global_mpu_capture(&o.state,&o.silent);
    else if(o.mode==12)zizhan_amp_global_silent_run(&o.state,&o.silent);
    else if(o.mode==11)zizhan_amp_global_clock_capture(&o.state,&o.silent);
    else if(o.mode>=10)zizhan_amp_monitor_clock_capture(&o.state,&o.silent);
    else if(o.mode>=8)zizhan_amp_pll_clock_capture(&o.state,&o.silent);
    else zizhan_amp_silent_clock_run(&o.state,&o.silent);
    if(o.mode>=23&&(!o.silent.release_safe||!pcm_commands(true))){
     zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;
    }
    if(o.mode>=9&&(!o.silent.release_safe||(o.mode<24&&!tdm_commands(true)))){
     zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;
    }
    }
    now=now_us();IOLockLock(o.lock);closed=o.closed;overflow=o.overflow;IOLockUnlock(o.lock);
    IOLog("ZIZHAN_AUDIO_SILENT: hold elapsed_us=%llu invoked=%u amp_pass=%u release_safe=%u\n",
      (unsigned long long)(now>=hold_begin?now-hold_begin:0),o.silent.invoked,o.silent.amp_pass,o.silent.release_safe);
    if(now<hold_begin||now-hold_begin>=60000000||now<start||now-start>=worker_limit||
       !o.silent.release_safe||closed||overflow){
     zizhan_prm_fail(&o.state,ZizhanPrmNotReady);o.state.uncertain=1;break;
    }
    /* No protocol command was in flight during this bounded owned hold.
     * Exclude only its measured duration from the8s protocol budget;
     * real worker90s and amp stage deadlines remain authoritative. */
    o.state.began_us+=now-hold_begin;
   }
   uint8_t bytes[512];const unsigned length=zizhan_prm_next(&o.state,now,bytes);
   if(length){
    IOLog("ZIZHAN_AUDIO_PRM: send phase=%u token=%x bytes=%u block=2 frequency_writes=%u\n",o.state.phase,o.state.token,length,
      o.state.expected_parameter==0x0800102cu?1u:0u);
    if(o.mode>=4)IOLog("ZIZHAN_AUDIO_PRM: gate elapsed_us=%llu source_port=%u framework_ready=%u\n",
      (unsigned long long)(now-o.state.began_us),o.state.expected_port,o.state.framework_ready);
    if(sheng_adsp_send(o.channel,bytes,length)){zizhan_prm_fail(&o.state,ZizhanPrmTransport);break;}
   }
  }
  IOSleep(10);
 }
 if(o.state.phase!=ZizhanPrmDone&&o.state.phase!=ZizhanPrmFailed)zizhan_prm_fail(&o.state,ZizhanPrmTimeout);
 if(o.mode==26){if(!o.dma_restored)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_MEMORY: %s phase=%u sent=%u acknowledged=%u handle=%x uncertain=%u mapped=%u restored=%u data=0 audio_output=0 finished=1\n",
   o.memory.phase==ZizhanMemoryDone&&o.dma_restored&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",o.memory.phase,o.memory.sent,o.memory.ack,o.memory.handle,o.memory.uncertain,o.dma_mapped,o.dma_restored);}
 if(o.mode==27){if(!o.dma_restored)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_DMACONTROL: %s mapped=%u restored=%u DSP_commands=0 data=0 audio_output=0 finished=1\n",o.dma_restored&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",o.dma_mapped,o.dma_restored);}
 if(o.mode==28){if(!o.dma_restored)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_WRITE: %s phase=%u sent=%u acknowledged=%u uncertain=%u reason=%u zero_verified=%u handle=%x memory_phase=%u memory_sent=%u memory_ack=%u mapped=%u retired=%u data_bytes=%u data_pads=0 audio_output=0 finished=1\n",
   o.write.phase==ZizhanWriteDone&&o.memory.phase==ZizhanMemoryDone&&o.dma_restored&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",o.write.phase,o.write.sent,o.write.ack,o.write.uncertain,o.write.reason,o.zero_verified,o.memory.handle,o.memory.phase,o.memory.sent,o.memory.ack,o.dma_mapped,o.dma_restored,ZizhanWriteBytes);}
 if(o.mode==35){if(!o.dma_restored&&o.dma_mapped)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_GRAPH: %s acks=%u mapped=%u restored=%u memory_phase=%u handle=%x data=0 audio_output=0 finished=1\n",
   o.graph_acks==12&&o.dma_restored&&o.state.phase==ZizhanPrmDone&&o.memory.phase==ZizhanMemoryDone?"PASS":"FAIL",
   o.graph_acks,o.dma_mapped,o.dma_restored,o.memory.phase,o.memory.handle);}
 if(o.mode==36){if(!o.dma_restored&&o.dma_mapped)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_GRAPH_DATA: %s acks=%u batch_acks=%u frames=%llu bytes_each=2880 zero_verified=%u mapped=%u restored=%u memory_phase=%u handle=%x data=zero audio_output=0 finished=1\n",
   o.graph_acks==12&&o.batch_acks==4&&o.batch.confirmed_frames==1920&&o.zero_verified&&o.dma_restored&&o.state.phase==ZizhanPrmDone&&o.memory.phase==ZizhanMemoryDone?"PASS":"FAIL",
   o.graph_acks,o.batch_acks,(unsigned long long)o.batch.confirmed_frames,o.zero_verified,o.dma_mapped,o.dma_restored,o.memory.phase,o.memory.handle);}
 if(o.mode==37){if(!o.dma_restored&&o.dma_mapped)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_GRAPH_DATA: %s acks=%u batch_acks=%u frames=%llu bytes_each=2880 tone_prepared=%u tone_cleared=%u mapped=%u restored=%u memory_phase=%u handle=%x data=known48 audio_output=0 finished=1\n",
   o.graph_acks==12&&o.batch_acks==4&&o.batch.confirmed_frames==1920&&o.tone48_prepared&&o.tone48_cleared&&o.dma_restored&&o.state.phase==ZizhanPrmDone&&o.memory.phase==ZizhanMemoryDone?"PASS":"FAIL",
   o.graph_acks,o.batch_acks,(unsigned long long)o.batch.confirmed_frames,o.tone48_prepared,o.tone48_cleared,o.dma_mapped,o.dma_restored,o.memory.phase,o.memory.handle);}
 if(o.mode==38){if(!o.dma_restored&&o.dma_mapped)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_RUNTIME: %s acks=%u ready_before=%u ready_after=%u watermark_gate=%u underrun_gate=%u pack=%u packets=%u done_acks=%u frames=%llu drained=%u ready_cleared=%u mapped=%u restored=%u memory_phase=%u handle=%x data=known48 audio_output=0 finished=1\n",
   o.graph_acks==12&&o.runtime.ready_before==0&&o.runtime.ready_after==1&&o.runtime.watermark_gate==1&&o.runtime.underrun_gate==1&&
   o.runtime.pack_verified==4&&o.runtime.packets==4&&o.runtime.acks==4&&o.runtime.frames==1920&&o.runtime.drained==1&&o.runtime.ready_cleared==1&&
   o.dma_restored&&o.state.phase==ZizhanPrmDone&&o.memory.phase==ZizhanMemoryDone?"PASS":"FAIL",
   o.graph_acks,o.runtime.ready_before,o.runtime.ready_after,o.runtime.watermark_gate,o.runtime.underrun_gate,o.runtime.pack_verified,
   o.runtime.packets,o.runtime.acks,(unsigned long long)o.runtime.frames,o.runtime.drained,o.runtime.ready_cleared,
   o.dma_mapped,o.dma_restored,o.memory.phase,o.memory.handle);}
 if(o.mode>=29&&o.mode!=38){if(!o.dma_restored)zizhan_audio_dma_map_retain();
  IOLog("ZIZHAN_AUDIO_WIRE: %s callbacks=%u completed=%u write_sent=%u write_ack=%u zero_verified=%u connected=%u restored=%u memory_phase=%u retired=%u data_bytes_each=%u outputs_off=%u finished=1\n",
   o.state.phase==ZizhanPrmDone&&o.dma_restored&&o.write_ack==(o.mode==34?4u:2u)&&o.silent.amp_pass?"PASS":"FAIL",o.transfer_calls,o.transfer_acks,o.write_sent,o.write_ack,o.zero_verified,o.silent.data_connected,o.silent.data_restored,o.memory.phase,o.dma_restored,ZizhanWriteBytes,o.mode==34?0u:1u);}
 if(o.mode==33)IOLog("ZIZHAN_AUDIO_TONE: %s prepared=%u cleared=%u callbacks=%u write_ack=%u output_enables=0 finite_frames=960 finished=1\n",o.state.phase==ZizhanPrmDone&&o.tone_prepared&&o.tone_cleared&&o.write_ack==2?"PASS":"FAIL",o.tone_prepared,o.tone_cleared,o.transfer_calls,o.write_ack);
 if(o.mode>=30&&o.mode<=33)IOLog("ZIZHAN_AUDIO_OUTPUT_OFF: %s configured=%u wire_acks=%u output_enables=0 finished=1\n",o.silent.output_configured&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",o.silent.output_configured,o.write_ack);
 IOLog("ZIZHAN_AUDIO_PRM: %s phase=%u reason=%u sent=%u acknowledged=%u request_ack=%u release_ack=%u held=%u uncertain=%u ignored=%u finished=1\n",
  linked&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",o.state.phase,o.state.reason,o.state.sent,o.state.acknowledged,
  o.state.request_ack,o.state.release_ack,o.state.lease_held,o.state.uncertain,o.state.ignored);
 IOLog("ZIZHAN_AUDIO_PRM: scope clock_frequency=%u pins=%u codec=%u graph=%u dma=%u audio_output=0 physical_clock_verified=0\n",o.mode>=6?1u:0u,o.mode>=7?1u:0u,o.mode>=7?1u:0u,o.mode>=9?1u:0u,o.dma_mapped);
 if(o.mode>=6)IOLog("ZIZHAN_AUDIO_PRM: clock id=204 hz=12288000 attribute=4 root=0 request_ack=%u release_ack=%u held=%u uncertain=%u pads_isolated=%u\n",
   o.state.clock_request_ack,o.state.clock_release_ack,o.state.clock_held,o.state.uncertain,o.clock_isolated);
 if(o.mode>=4)IOLog("ZIZHAN_AUDIO_PRM: readiness actual_apm=%u pdr_verified=0 no_retry=1\n",o.state.framework_ready);
 if((o.mode>=8&&o.mode<=11)||o.mode==13||o.mode==21)IOLog("ZIZHAN_AUDIO_PLL: %s captured=%u reset_asserted=%u pin_restored=%u lease_done=%u core_held=%u clock_held=%u uncertain=%u audio_output=0 finished=1\n",
   o.silent.diagnostic_capture&&o.silent.release_safe&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",
   o.silent.diagnostic_capture,o.silent.reset_asserted,o.silent.pin_restored,
   o.state.phase==ZizhanPrmDone?1u:0u,o.state.lease_held,o.state.clock_held,o.state.uncertain);
 if(o.mode>=9&&o.mode!=35&&o.mode!=36&&o.mode!=37&&o.mode!=38)IOLog("ZIZHAN_AUDIO_TDM: %s phase=%u sent=%u acknowledged=%u opened=%u running=%u uncertain=%u reason=%u remote=%u data_pads=0 audio_output=0 finished=1\n",
   o.tdm.phase==ZizhanTdmDone&&o.silent.release_safe&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",
   o.tdm.phase,o.tdm.sent,o.tdm.ack,o.tdm.opened,o.tdm.running,o.tdm.uncertain,o.tdm.reason,o.tdm.status);
 if(o.mode>=23&&o.mode!=35&&o.mode!=36&&o.mode!=37&&o.mode!=38)IOLog("ZIZHAN_AUDIO_PCM: %s phase=%u sent=%u acknowledged=%u opened=%u running=%u uncertain=%u reason=%u remote=%u joint=%u dma=%u data=%u audio_output=0 finished=1\n",
   o.pcm.phase==ZizhanPcmDone&&o.silent.release_safe&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",
   o.pcm.phase,o.pcm.sent,o.pcm.ack,o.pcm.opened,o.pcm.running,o.pcm.uncertain,o.pcm.reason,o.pcm.status,o.pcm.joint,o.dma_mapped,o.write.sent);
 if(o.mode==16)IOLog("ZIZHAN_AUDIO_COREDEBUG: %s captured=%u amp_pass=%u reset_asserted=%u pin_restored=%u lease_done=%u core_held=%u clock_held=%u uncertain=%u audio_output=0 finished=1\n",
   o.silent.diagnostic_capture&&o.silent.release_safe&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",
   o.silent.diagnostic_capture,o.silent.amp_pass,o.silent.reset_asserted,o.silent.pin_restored,
   o.state.phase==ZizhanPrmDone?1u:0u,o.state.lease_held,o.state.clock_held,o.state.uncertain);
 if(o.mode==7||o.mode==12||o.mode==14||o.mode==15||o.mode==17||o.mode==18||o.mode==19||o.mode==20||o.mode==22||(o.mode>=23&&o.mode!=35&&o.mode!=36&&o.mode!=37&&o.mode!=38))IOLog("ZIZHAN_AUDIO_SILENT: %s invoked=%u amp_pass=%u reset_asserted=%u pin_restored=%u lease_done=%u core_held=%u clock_held=%u uncertain=%u audio_output=0 finished=1\n",
   o.silent.amp_pass&&o.silent.release_safe&&o.state.phase==ZizhanPrmDone?"PASS":"FAIL",
   o.silent.invoked,o.silent.amp_pass,o.silent.reset_asserted,o.silent.pin_restored,
   o.state.phase==ZizhanPrmDone?1u:0u,o.state.lease_held,o.state.clock_held,o.state.uncertain);
}
}
extern "C" void zizhan_audio_prm_probe(void){
 unsigned mode=0,audio=0,prepare=0,adsp=0,observe=0;
 if(!PE_parse_boot_argn("zizhan_audio_prm",&mode,sizeof mode)||mode<1||mode>38)return;
 if(mode==34){unsigned attended=0,dry=0;PE_parse_boot_argn("zizhan_pulse_dry",&dry,sizeof dry);if((!PE_parse_boot_argn("zizhan_audio_attended",&attended,sizeof attended)||attended!=1)&&dry!=1){IOLog("ZIZHAN_AUDIO_PULSE: SKIP attended=0 no_owner=1\n");return;}}
 PE_parse_boot_argn("pong_audio",&audio,sizeof audio);PE_parse_boot_argn("zizhan_se15_prepare",&prepare,sizeof prepare);PE_parse_boot_argn("pong_adsp",&adsp,sizeof adsp);
 if(mode>=7)PE_parse_boot_argn("zizhan_se15_observe",&observe,sizeof observe);
 auto node=IORegistryEntry::fromPath("/arm-io/pong-touch",gIODTPlane);auto data=node?OSDynamicCast(OSData,node->getProperty("compatible")):nullptr;
 const char id[]="st,fts-zizhan-pri";const bool board=data&&data->getLength()==sizeof id&&!memcmp(data->getBytesNoCopy(),id,sizeof id);if(node)node->release();
 if(!board||audio||prepare||observe||adsp!=2){IOLog("ZIZHAN_AUDIO_PRM: SKIP board-or-mode-conflict writes=0 finished=1\n");return;}
 static uint32_t started;if(__atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL))return;
 owner.mode=mode;owner.lock=IOLockAlloc();if(!owner.lock){IOLog("ZIZHAN_AUDIO_PRM: FAIL lock finished=1\n");return;}
 const ShengAdspClient client={channel_state,receive};owner.channel=sheng_adsp_channel("adsp_apps",512,16,1,&client,&owner);
 thread_t thread=nullptr;
 if(!owner.channel||kernel_thread_start(worker,nullptr,&thread)!=KERN_SUCCESS){IOLog("ZIZHAN_AUDIO_PRM: FAIL channel-or-thread finished=1\n");return;}
 thread_deallocate(thread);IOLog("ZIZHAN_AUDIO_PRM: RUNNING bounded%us no-codec-output=1\n",mode>=7?90u:40u);
}
