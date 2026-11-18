// SPDX-License-Identifier: BSD-2-Clause-Patent
// PongAudioPlugIn opens this service after the physical speaker graph is ready.
// PongAudio.cpp owns the control/output mappings and consumes start/stop
// requests. No capture memory or input endpoint is implemented on Pong.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include "ShengAudioAbi.h"

extern "C" IOBufferMemoryDescriptor *sheng_audio_memory(uint32_t type);   // retained; null when absent
extern "C" int sheng_audio_request(uint32_t selector);                     // 0 accepted
// [0] service state (1 published; negative: the step that failed), [1] opens, [2] closes, [3] requests, [4] refused requests,
// [5] memory maps
extern "C" {volatile int32_t native_audio_service[8];}
#include "ShengAudioHostClock.h"

class PongAudio : public IOService {
    OSDeclareDefaultStructors(PongAudio)
public:
    bool startAudio() {
        // iOS's audio server is sandboxed against unknown user clients; the kernel lets these processes open this service.
        setProperty("NativeSandboxGrantProcess", "audiomxd,mediaserverd");
        setProperty("IOUserClientClass", "PongAudioUserClient");
        registerService();
        return true;
    }
};
OSDefineMetaClassAndStructors(PongAudio, IOService)

class PongAudioUserClient : public IOUserClient {
    OSDeclareDefaultStructors(PongAudioUserClient)
    task_t owner = nullptr;
public:
    bool initWithTask(task_t task, void *security, UInt32 type, OSDictionary *properties) override {
        if (!IOUserClient::initWithTask(task, security, type, properties)) return false;
        owner = task;
        return true;
    }
    bool start(IOService *provider) override {
        if (!OSDynamicCast(PongAudio, provider) || !IOUserClient::start(provider)) return false;
        native_audio_service[1] = native_audio_service[1] + 1;
        return true;
    }
    IOReturn clientClose() override {
        // The plug-in's process went away or closed: nothing plays from its rings any more.
        sheng_audio_request(ShengAudioStopOutput);
        native_audio_service[2] = native_audio_service[2] + 1;
        terminate();
        return kIOReturnSuccess;
    }
    IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) override {
        IOBufferMemoryDescriptor *m = sheng_audio_memory(type);
        if (!m) return kIOReturnBadArgument;
        *options = 0;
        *memory = m;   // the caller consumes the reference
        native_audio_service[5] = native_audio_service[5] + 1;
        return kIOReturnSuccess;
    }
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *dispatch, OSObject *target,
                            void *reference) override {
        if(selector==ShengAudioSetHostClock) {
            if(!args || args->scalarInputCount!=1 || args->structureInputSize ||
               args->scalarOutputCount || args->structureOutputSize)return kIOReturnBadArgument;
            return setHostClock(owner,args->scalarInput[0]);
        }
        if (!args || args->scalarInputCount || args->structureInputSize ||
            args->scalarOutputCount || args->structureOutputSize) return kIOReturnBadArgument;
        if (selector > ShengAudioStopOutput) return kIOReturnUnsupported;
        native_audio_service[3] = native_audio_service[3] + 1;
        if (sheng_audio_request(selector)) {native_audio_service[4] = native_audio_service[4] + 1; return kIOReturnNotReady;}
        return kIOReturnSuccess;
    }
};
OSDefineMetaClassAndStructors(PongAudioUserClient, IOUserClient)

// Called by AudioProbe.cpp once the speaker path is ready and this file's classes are registered.
extern "C" void sheng_audio_service_start(void) {
    PongAudio *audio = OSTypeAlloc(PongAudio);
    if (!audio || !audio->init() || !audio->attach(IOService::getPlatform())) {
        if (audio) audio->release();
        native_audio_service[0] = -1;
        return;
    }
    if (!audio->start(IOService::getPlatform()) || !audio->startAudio()) {
        native_audio_service[0] = -2;
        audio->detach(IOService::getPlatform());
        audio->release();
        return;
    }
    native_audio_service[0] = 1;
    audio->release();   // the platform's attachment keeps it
}
