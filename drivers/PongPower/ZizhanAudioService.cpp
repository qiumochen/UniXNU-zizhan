// SPDX-License-Identifier: BSD-2-Clause-Patent
// I1 (2026-11-18): default-off ZizhanAudio IOKit service and user client for the future zizhan
// CoreAudio HAL bundle (docs/plan/2026-11-17-zizhan-audio-service.md, plan A). It does not define
// or touch the Pong classes/symbols. zizhan_audio_service=1 publishes the service with the v2
// control page and the 48k/2ch int16 ring only: no graph, no Ready, no AMP/BST/TX/data pin.
// StartOutput is refused (kIOReturnNotReady) unless PRM mode 40 registered as the graph owner;
// then one Start per boot is handed to it (ZizhanAudioHandoff) and it runs the finite stream.
// zizhan_audio_selftest=1 (silent device rounds, no HAL installed): a kernel thread plays the HAL's
// part - Start, then a -36 dBFS pattern written ahead with the producer watermark, then Stop.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/OSKextLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "ShengAudioHostClock.h"
extern "C" {
#include "ZizhanAudioServiceGate.h"
void ZizhanAudioServiceStaticInit(void);
// [0] state (1 published, 2 self-check passed; negative: failed step), [1] opens, [2] closes,
// [3] requests, [4] refused starts, [5] memory maps, [6] gate result
volatile int32_t native_zizhan_audio_service[8];
}
extern kmod_info_t g_kernel_kmod_info;
namespace {
IOBufferMemoryDescriptor *gControl,*gRing;
ZizhanAudioHandoff gHandoff;
volatile ZizhanAudioControl *control(){return gControl?static_cast<volatile ZizhanAudioControl *>(gControl->getBytesNoCopy()):nullptr;}
void fail(int step){native_zizhan_audio_service[0]=-step;IOLog("ZIZHAN_AUDIO_SERVICE: failed step=%d; service not published\n",step);}
// Silent-round evidence: a few seconds later the registry finds the class and the page is still idle.
void check(void *,wait_result_t){
 IOSleep(5000);auto matching=IOService::serviceMatching(ZIZHAN_AUDIO_SERVICE_CLASS);
 auto found=matching?IOService::waitForMatchingService(matching,5000000000ull):nullptr;if(matching)matching->release();
 auto c=control();const unsigned valid=zizhan_audio_control_valid(c)?1u:0u,state=c?c->state:~0u;
 IOLog("ZIZHAN_AUDIO_SERVICE: check registry=%u control_valid=%u state=%u opens=%d maps=%d requests=%d refused=%d\n",
  found?1u:0u,valid,state,native_zizhan_audio_service[1],native_zizhan_audio_service[5],native_zizhan_audio_service[3],native_zizhan_audio_service[4]);
 if(found){found->release();if(valid&&!state)native_zizhan_audio_service[0]=2;}
 thread_terminate(current_thread());
}
// Silent self-test producer (zizhan_audio_selftest=1 with PRM mode 40). It writes like the HAL's
// DoIOOperation: from lead + 2 periods after the anchor, one period every 10 ms, watermark after the copy.
// zizhan_tone_amp=64..8192 (attended loudness checks only); default 512 = -36 dBFS.
int16_t gSelftestAmp=512;bool gSelftestInPhase=false;
int16_t selftest_sample(uint64_t frame){return ((frame/24u)&1u)?gSelftestAmp:int16_t(-gSelftestAmp);}
void selftest(void *,wait_result_t){
 {unsigned amp=0;if(PE_parse_boot_argn("zizhan_tone_amp",&amp,sizeof amp)&&amp>=64&&amp<=8192){gSelftestAmp=int16_t(amp);}}
 /* zizhan_amp_lvl=1 also plays both channels in phase (R=-L made the two speakers push against each other). */
 {unsigned lvl=0;gSelftestInPhase=PE_parse_boot_argn("zizhan_amp_lvl",&lvl,sizeof lvl)&&lvl==1;}
 IOLog("ZIZHAN_AUDIO_SELFTEST: amp=%d in_phase=%d\n",int(gSelftestAmp),gSelftestInPhase?1:0);
 IOSleep(30000);auto c=control();auto ring=gRing?static_cast<int16_t *>(gRing->getBytesNoCopy()):nullptr;
 unsigned produced=0,waited=0;const int started=c&&ring&&zizhan_handoff_request_start(&gHandoff);
 IOLog("ZIZHAN_AUDIO_SELFTEST: start accepted=%d owner=%u\n",started,gHandoff.owner);
 if(started){
  for(;waited<1500&&!(c->state&ZizhanAudioOutputRunning)&&gHandoff.phase<ZizhanHandoffDone;++waited)IOSleep(10);
  if(c->state&ZizhanAudioOutputRunning){
   // Paced by the clock, not by sleeps: keep the watermark lead + 2 periods ahead of "now" in sample time.
   const uint64_t anchor=c->out_anchor_host,q32=c->ticks_per_frame_q32;
   auto sample_now=[&]{const uint64_t now=mach_absolute_time();return now>anchor&&q32?((now-anchor)<<16)/(q32>>16):0;};
   uint64_t t=sample_now()+ZIZHAN_AUDIO_LEAD_FRAMES+2*ZIZHAN_AUDIO_PERIOD_FRAMES;
   // Up to 60 s while Running: PRM 41 brings both amps up (RAM load over I2C, ~13 s) before it streams.
   for(unsigned i=0;i<60000&&(c->state&ZizhanAudioOutputRunning);++i){
    const uint64_t target=sample_now()+ZIZHAN_AUDIO_LEAD_FRAMES+2*ZIZHAN_AUDIO_PERIOD_FRAMES;
    while(t+ZIZHAN_AUDIO_PERIOD_FRAMES<=target){
     for(unsigned f=0;f<ZIZHAN_AUDIO_PERIOD_FRAMES;++f,++t){const unsigned at=unsigned(t%ZIZHAN_AUDIO_RING_FRAMES);
      ring[2*at]=selftest_sample(t);ring[2*at+1]=gSelftestInPhase?selftest_sample(t):int16_t(-selftest_sample(t));}
     zizhan_audio_publish_written(c,t);++produced;
    }
    IOSleep(1);
   }
  }
  zizhan_handoff_request_stop(&gHandoff);
 }
 IOLog("ZIZHAN_AUDIO_SELFTEST: done produced_periods=%u waited_ms=%u written=%llu phase=%u state=%u\n",
  produced,waited*10,c?(unsigned long long)c->out_written:0ull,gHandoff.phase,c?c->state:~0u);
 thread_terminate(current_thread());
}
}
extern "C" {
// For PRM mode 40, the graph owner.
volatile ZizhanAudioControl *zizhan_audio_service_control(void){return native_zizhan_audio_service[0]>=1?control():nullptr;}
int16_t *zizhan_audio_service_ring(void){return native_zizhan_audio_service[0]>=1&&gRing?static_cast<int16_t *>(gRing->getBytesNoCopy()):nullptr;}
ZizhanAudioHandoff *zizhan_audio_service_handoff(void){return &gHandoff;}
}
class ZizhanAudio : public IOService {
 OSDeclareDefaultStructors(ZizhanAudio)
public:
 bool startAudio(){
  // The iOS audio server is sandboxed against unknown user clients; the kernel lets these processes open this service.
  setProperty("NativeSandboxGrantProcess","audiomxd,mediaserverd");
  setProperty("IOUserClientClass","ZizhanAudioUserClient");
  registerService();return true;
 }
};
OSDefineMetaClassAndStructors(ZizhanAudio,IOService)
class ZizhanAudioUserClient : public IOUserClient {
 OSDeclareDefaultStructors(ZizhanAudioUserClient)
 task_t owner=nullptr;
public:
 bool initWithTask(task_t task,void *security,UInt32 type,OSDictionary *properties) override {
  if(!IOUserClient::initWithTask(task,security,type,properties))return false;
  owner=task;return true;
 }
 bool start(IOService *provider) override {
  if(!OSDynamicCast(ZizhanAudio,provider)||!IOUserClient::start(provider))return false;
  native_zizhan_audio_service[1]=native_zizhan_audio_service[1]+1;return true;
 }
 IOReturn clientClose() override {
  // The plug-in's process went away or closed: nothing may keep playing from its ring.
  zizhan_handoff_request_stop(&gHandoff);
  native_zizhan_audio_service[2]=native_zizhan_audio_service[2]+1;terminate();return kIOReturnSuccess;
 }
 IOReturn clientMemoryForType(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory) override {
  IOBufferMemoryDescriptor *m=!zizhan_service_memory_type(type)?nullptr:type==ZizhanAudioMemoryControl?gControl:gRing;
  if(!m||!options||!memory)return kIOReturnBadArgument;
  m->retain();*options=0;*memory=m;   // the caller consumes the reference
  native_zizhan_audio_service[5]=native_zizhan_audio_service[5]+1;return kIOReturnSuccess;
 }
 IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *args,IOExternalMethodDispatch *,OSObject *,void *) override {
  if(!args)return kIOReturnBadArgument;
  // The HAL bundle adapts CoreAudio's 24 MHz host clock constant to the 19.2 MHz counter (same as Pong).
  if(selector==ZizhanAudioSetHostClock){
   if(args->scalarInputCount!=1||args->structureInputSize||args->scalarOutputCount||args->structureOutputSize)return kIOReturnBadArgument;
   return setHostClock(owner,args->scalarInput[0]);
  }
  native_zizhan_audio_service[3]=native_zizhan_audio_service[3]+1;
  const int owner=__atomic_load_n(&gHandoff.owner,__ATOMIC_ACQUIRE)?1:0;
  switch(zizhan_service_request(selector,args->scalarInputCount,args->structureInputSize,args->scalarOutputCount,args->structureOutputSize,owner)){
  case ZizhanRequestOk:
   if(selector==ZizhanAudioStopOutput){zizhan_handoff_request_stop(&gHandoff);return kIOReturnSuccess;}
   if(zizhan_handoff_request_start(&gHandoff)){IOLog("ZIZHAN_AUDIO_SERVICE: start accepted owner=prm40\n");return kIOReturnSuccess;}
   [[fallthrough]];
  case ZizhanRequestNotReady:
   native_zizhan_audio_service[4]=native_zizhan_audio_service[4]+1;
   IOLog("ZIZHAN_AUDIO_SERVICE: start refused owner=%d phase=%u ready=0 output=0\n",owner,gHandoff.phase);return kIOReturnNotReady;
  case ZizhanRequestUnsupported:return kIOReturnUnsupported;
  default:return kIOReturnBadArgument;
  }
 }
};
OSDefineMetaClassAndStructors(ZizhanAudioUserClient,IOUserClient)

extern "C" void zizhan_audio_service_probe(void){
 unsigned service=0,pong=0,prm=0;
 const int has_service=PE_parse_boot_argn("zizhan_audio_service",&service,sizeof service);
 const int has_pong=PE_parse_boot_argn("pong_audio",&pong,sizeof pong);
 const int has_prm=PE_parse_boot_argn("zizhan_audio_prm",&prm,sizeof prm);
 const unsigned gate=zizhan_service_gate(has_service,service,has_pong,pong,has_prm,prm);
 native_zizhan_audio_service[6]=(int32_t)gate;
 if(gate==ZizhanGateOff)return;
 if(gate!=ZizhanGateStart){native_zizhan_audio_service[0]=-1;
  IOLog("ZIZHAN_AUDIO_SERVICE: SKIP gate=%u service=%u pong_audio=%u prm=%u\n",gate,service,pong,prm);return;}
 const IOOptionBits flags=IOOptionBits(kIODirectionInOut)|IOOptionBits(kIOMemoryKernelUserShared);
 gControl=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,flags,ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES,4096);
 gRing=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,flags,ZIZHAN_AUDIO_SERVICE_RING_BYTES,4096);
 if(!gControl||!gRing){fail(2);return;}
 bzero(gRing->getBytesNoCopy(),ZIZHAN_AUDIO_SERVICE_RING_BYTES);
 mach_timebase_info_data_t tb;clock_timebase_info(&tb);
 const uint64_t hz=tb.numer?1000000000ull*tb.denom/tb.numer:0;
 if(!zizhan_service_control_init(control(),ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES,hz,mach_absolute_time())){fail(3);return;}
 void *h=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!h){fail(4);return;}
 ZizhanAudioServiceStaticInit();if(OSMetaClass::postModLoad(h)){fail(5);return;}
 ZizhanAudio *audio=OSTypeAlloc(ZizhanAudio);
 if(!audio||!audio->init()||!audio->attach(IOService::getPlatform())){if(audio)audio->release();fail(6);return;}
 if(!audio->start(IOService::getPlatform())||!audio->startAudio()){audio->detach(IOService::getPlatform());audio->release();fail(7);return;}
 audio->release();   // the platform's attachment keeps it
 native_zizhan_audio_service[0]=1;
 IOLog("ZIZHAN_AUDIO_SERVICE: published class=%s control=%u ring=%u hz=%llu ticks_q32=%llu graph=0 ready=0 output=0\n",
  ZIZHAN_AUDIO_SERVICE_CLASS,ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES,ZIZHAN_AUDIO_SERVICE_RING_BYTES,(unsigned long long)hz,(unsigned long long)control()->ticks_per_frame_q32);
 thread_t t=nullptr;if(kernel_thread_start(check,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
 unsigned self=0;
 if(PE_parse_boot_argn("zizhan_audio_selftest",&self,sizeof self)&&self==1){
  if(has_prm&&zizhan_service_owner_mode(prm)){t=nullptr;if(kernel_thread_start(selftest,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);}
  else IOLog("ZIZHAN_AUDIO_SELFTEST: SKIP prm=%u needs=40\n",prm);
 }
}
