/* Host-only ZizhanAudio I1 owner decisions: default-off gate, control page, memory types, requests. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioServiceGate.h"
int main(void){
 /* Default off: missing or 0 never starts; any other value is rejected, not coerced. */
 assert(zizhan_service_gate(0,1,0,0,0,0)==ZizhanGateOff);
 assert(zizhan_service_gate(1,0,0,0,0,0)==ZizhanGateOff);
 assert(zizhan_service_gate(1,2,0,0,0,0)==ZizhanGateBadValue);
 assert(zizhan_service_gate(1,0xffffffffu,0,0,0,0)==ZizhanGateBadValue);
 assert(zizhan_service_gate(1,1,0,0,0,0)==ZizhanGateStart);
 /* The Pong TFA path and the PRM diagnostics own the same graph; an explicit 0 does not conflict. */
 assert(zizhan_service_gate(1,1,1,1,0,0)==ZizhanGateConflictPong);
 assert(zizhan_service_gate(1,1,1,0,0,0)==ZizhanGateStart);
 assert(zizhan_service_gate(1,1,0,0,1,34)==ZizhanGateConflictPrm);
 assert(zizhan_service_gate(1,1,0,0,1,0)==ZizhanGateStart);
 assert(zizhan_service_gate(1,1,1,1,1,34)==ZizhanGateConflictPong);
 /* Control page: valid v2 header at the real 19.2 MHz counter, idle state, no Ready. */
 static uint8_t page[ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES];memset(page,0xa5,sizeof page);
 volatile ZizhanAudioControl *c=(volatile ZizhanAudioControl *)page;
 assert(zizhan_service_control_init(c,sizeof page,19200000ull,1234));
 assert(zizhan_audio_control_valid(c)&&!zizhan_audio_ready(c)&&c->state==0&&c->out_written==0&&c->written_seq==0);
 assert(c->ticks_per_frame_q32==(400ull<<32)&&c->out_anchor_host==1234&&c->lead_frames==30u*480u);
 for(size_t i=sizeof(ZizhanAudioControl);i<sizeof page;++i)assert(page[i]==0);
 assert(zizhan_service_control_init(c,sizeof page,24000000ull,0)&&c->ticks_per_frame_q32==(500ull<<32));
 /* Rejects: null, short page, zero or overflowing frequency. */
 assert(!zizhan_service_control_init(NULL,sizeof page,19200000ull,0));
 assert(!zizhan_service_control_init(c,sizeof(ZizhanAudioControl)-1,19200000ull,0));
 assert(!zizhan_service_control_init(c,sizeof page,0,0));
 assert(!zizhan_service_control_init(c,sizeof page,(UINT64_MAX>>32)+1,0));
 assert(ZIZHAN_AUDIO_SERVICE_RING_BYTES==262144u&&sizeof(ZizhanAudioControl)<=ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES);
 /* Memory: control and output ring only; no capture ring. */
 assert(zizhan_service_memory_type(0)&&zizhan_service_memory_type(1));
 assert(!zizhan_service_memory_type(2)&&!zizhan_service_memory_type(0xffffffffu));
 /* Requests: Start refused until a graph owner exists; Stop always accepted; arguments rejected. */
 assert(zizhan_service_request(ZizhanAudioStartOutput,0,0,0,0,0)==ZizhanRequestNotReady);
 assert(zizhan_service_request(ZizhanAudioStartOutput,0,0,0,0,1)==ZizhanRequestOk);
 assert(zizhan_service_request(ZizhanAudioStopOutput,0,0,0,0,0)==ZizhanRequestOk);
 assert(zizhan_service_request(2,0,0,0,0,1)==ZizhanRequestUnsupported);
 assert(zizhan_service_request(4,0,0,0,0,1)==ZizhanRequestUnsupported);
 assert(zizhan_service_request(ZizhanAudioStartOutput,1,0,0,0,1)==ZizhanRequestBadArgument);
 assert(zizhan_service_request(ZizhanAudioStopOutput,0,8,0,0,1)==ZizhanRequestBadArgument);
 assert(zizhan_service_request(ZizhanAudioStopOutput,0,0,1,0,1)==ZizhanRequestBadArgument);
 assert(zizhan_service_request(ZizhanAudioStopOutput,0,0,0,4,1)==ZizhanRequestBadArgument);
 puts("cs35l41-service-gate: PASS");return 0;
}
