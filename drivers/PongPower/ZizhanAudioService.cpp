// SPDX-License-Identifier: BSD-2-Clause-Patent
// I1 (2026-11-18): default-off ZizhanAudio IOKit service and user client for the future zizhan
// CoreAudio HAL bundle (docs/plan/2026-11-17-zizhan-audio-service.md, plan A). It does not define
// or touch the Pong classes/symbols. zizhan_audio_service=1 publishes the service with the v2
// control page and the 48k/2ch int16 ring only: no graph, no Ready, no AMP/BST/TX/data pin.
// StartOutput is refused (kIOReturnNotReady) until the graph wiring lands.
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
public:
 bool start(IOService *provider) override {
  if(!OSDynamicCast(ZizhanAudio,provider)||!IOUserClient::start(provider))return false;
  native_zizhan_audio_service[1]=native_zizhan_audio_service[1]+1;return true;
 }
 IOReturn clientClose() override {
  // Nothing streams in I1. Once Start is wired, the stream must stop here before the client goes away.
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
  native_zizhan_audio_service[3]=native_zizhan_audio_service[3]+1;
  switch(zizhan_service_request(selector,args->scalarInputCount,args->structureInputSize,args->scalarOutputCount,args->structureOutputSize,0)){
  case ZizhanRequestOk:return kIOReturnSuccess;
  case ZizhanRequestNotReady:
   native_zizhan_audio_service[4]=native_zizhan_audio_service[4]+1;
   IOLog("ZIZHAN_AUDIO_SERVICE: start refused graph=0 ready=0 output=0\n");return kIOReturnNotReady;
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
}
