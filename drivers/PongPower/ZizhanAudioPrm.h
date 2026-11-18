/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_PRM_H
#define ZIZHAN_AUDIO_PRM_H
#include "ShengGpr.h"
#include "ShengAudioReach.h"
#include <string.h>
/* Bounded one-owner APM readiness + own digital-core PRM lease. Mode6 adds
 * an isolated-pad internal TDM clock request/release;no graph,DMA,pin write
 * or codec operation. A queued command is not ACK.
 * No retries;missing request ACK leaves uncertainty until platform reset. */
enum {ZizhanPrmIdle,ZizhanPrmFrameworkWait,ZizhanPrmRequestReady,ZizhanPrmRequestWait,
      ZizhanPrmReleaseReady,ZizhanPrmReleaseWait,ZizhanPrmDone,ZizhanPrmFailed,
      ZizhanPrmClockRequestReady,ZizhanPrmClockRequestWait,
      ZizhanPrmClockReleaseReady,ZizhanPrmClockReleaseWait};
enum {ZizhanPrmReasonNone,ZizhanPrmTimeout,ZizhanPrmRemoteError,ZizhanPrmTransport,
      ZizhanPrmMalformed,ZizhanPrmNotReady,ZizhanPrmArguments};
enum {ZizhanPrmPort=0x2101,ZizhanPrmTokenBase=0x7a170000};
typedef struct {
 unsigned phase,reason,sent,acknowledged,ignored,lease_held,uncertain,last_remote_status;
 unsigned last_opcode,last_payload_bytes,request_ack,release_ack;
 uint64_t began_us,command_us;
 uint32_t token,expected_opcode,expected_source;
 unsigned native_wire,native_framework,expected_port,framework_ready,native_parameter_reply;
 unsigned clock_mode,clock_held,clock_request_ack,clock_release_ack,expected_parameter,expected_request;
 unsigned skip_clock;
} ZizhanPrmState;
static inline uint32_t zizhan_prm_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void zizhan_prm_fail(ZizhanPrmState *s,unsigned reason){
 if(s->phase==ZizhanPrmRequestWait||s->phase==ZizhanPrmReleaseWait||
    s->phase==ZizhanPrmClockRequestWait||s->phase==ZizhanPrmClockReleaseWait)s->uncertain=1;
 s->reason=reason;s->phase=ZizhanPrmFailed;
}
static inline void zizhan_prm_init(ZizhanPrmState *s,uint64_t now){memset(s,0,sizeof *s);s->began_us=now;}
/* Owner calls next at most once per ready phase. Return bytes only after
 * state reserves this unique token;send failure cannot cause a retry. */
static inline unsigned zizhan_prm_next(ZizhanPrmState *s,uint64_t now,uint8_t out[512]){
 if(!s||!out)return 0;
 unsigned length=0;
 if(s->phase==ZizhanPrmIdle){
  /* zizhan mode4: Android's real audio_pd UP follows GLINK link. A
   * one-second settle is an experiment control, not a PDR notification.
   * Only the subsequent actual APM ready response unlocks resource work. */
  if(s->native_framework&&(now<s->began_us||now-s->began_us<1000000))return 0;
  GprPacket packet;memset(&packet,0,sizeof packet);packet.dst_domain=GPR_DOMAIN_ADSP;packet.src_domain=GPR_DOMAIN_APPS;
  packet.src_port=s->native_framework?3:ZizhanPrmPort;packet.dst_port=GPR_PORT_APM;packet.token=s->native_framework?0:ZizhanPrmTokenBase+1;
  packet.opcode=GPR_OP_APM_GET_SPF_STATE;
  length=gpr_encode(out,512,&packet,0,0);s->phase=ZizhanPrmFrameworkWait;
  s->expected_opcode=GPR_OP_APM_SPF_STATE;s->expected_source=GPR_PORT_APM;
  s->expected_request=GPR_OP_APM_GET_SPF_STATE;s->expected_parameter=0;
 }else if(s->phase==ZizhanPrmRequestReady||s->phase==ZizhanPrmReleaseReady){
  const int request=s->phase==ZizhanPrmRequestReady;
  length=ar_prm_vote(out,512,s->native_wire?7:ZizhanPrmPort,s->native_wire?0:ZizhanPrmTokenBase+(request?2:3),AR_HW_DCODEC,request);
  s->phase=request?ZizhanPrmRequestWait:ZizhanPrmReleaseWait;
  s->expected_opcode=request?AR_OP_PRM_REQUEST_DONE:AR_OP_PRM_RELEASE_DONE;s->expected_source=AR_PRM_IID;
  s->expected_request=request?AR_OP_PRM_REQUEST:AR_OP_PRM_RELEASE;s->expected_parameter=0x08001032u;
 }else if(s->clock_mode&&s->skip_clock&&(s->phase==ZizhanPrmClockRequestReady||s->phase==ZizhanPrmClockReleaseReady)){
  /* zizhan_audio_noclk=1 (diagnostic): no TERT_TDM_IBIT vote of our own; the TDM endpoint auto
   * clock config (0x0800113c) is the only clock source. Phases advance without a command. */
  s->phase=s->phase==ZizhanPrmClockRequestReady?ZizhanPrmClockReleaseReady:ZizhanPrmReleaseReady;return 0;
 }else if(s->clock_mode&&(s->phase==ZizhanPrmClockRequestReady||s->phase==ZizhanPrmClockReleaseReady)){
  const int request=s->phase==ZizhanPrmClockRequestReady;
  /* Exact actual machine/module contract:96k*4slots*32=12.288M,
   * TERT_TDM_IBIT204,attribute4,root0;no pin or endpoint operation. */
  length=ar_prm_clock_attr(out,512,7,0,0x204,request?12288000u:0,4);
  s->phase=request?ZizhanPrmClockRequestWait:ZizhanPrmClockReleaseWait;
  s->expected_opcode=request?AR_OP_PRM_REQUEST_DONE:AR_OP_PRM_RELEASE_DONE;
  s->expected_request=request?AR_OP_PRM_REQUEST:AR_OP_PRM_RELEASE;
  s->expected_parameter=0x0800102cu;s->expected_source=AR_PRM_IID;
 }else return 0;
 if(!length){zizhan_prm_fail(s,ZizhanPrmArguments);return 0;}
 s->token=zizhan_prm_le32(out+16);s->expected_port=zizhan_prm_le32(out+8);s->command_us=now;++s->sent;return length;
}
/* Strict peer/domain/port/token/opcode and complete payload matching. A
 * successful BASIC_RESULT is not the independent PRM completion reply. */
static inline int zizhan_prm_receive(ZizhanPrmState *s,const uint8_t *bytes,unsigned length){
 if(s->phase!=ZizhanPrmFrameworkWait&&s->phase!=ZizhanPrmRequestWait&&s->phase!=ZizhanPrmReleaseWait&&
    s->phase!=ZizhanPrmClockRequestWait&&s->phase!=ZizhanPrmClockReleaseWait){++s->ignored;return 0;}
 GprPacket packet;
 if(gpr_decode(bytes,length,&packet)!=GprOk||packet.size!=length){++s->ignored;return 0;}
 if(packet.dst_domain!=GPR_DOMAIN_APPS||packet.src_domain!=GPR_DOMAIN_ADSP||packet.dst_port!=s->expected_port||
    packet.src_port!=s->expected_source||packet.token!=s->token){++s->ignored;return 0;}
 s->last_opcode=packet.opcode;s->last_payload_bytes=packet.payload_bytes;
 if(packet.opcode==GPR_OP_BASIC_RESULT){
  if(packet.payload_bytes!=8){zizhan_prm_fail(s,ZizhanPrmMalformed);return -1;}
  const unsigned wanted=s->expected_request;
  if(zizhan_prm_le32(packet.payload)!=wanted){++s->ignored;return 0;}
  const unsigned status=zizhan_prm_le32(packet.payload+4);s->last_remote_status=status;
  if(status){zizhan_prm_fail(s,ZizhanPrmRemoteError);return -1;}++s->ignored;return 0;
 }
 if(packet.opcode!=s->expected_opcode){++s->ignored;return 0;}
 unsigned status=0;
 if(s->phase==ZizhanPrmFrameworkWait){
  if(packet.payload_bytes!=4){zizhan_prm_fail(s,ZizhanPrmMalformed);return -1;}
  status=zizhan_prm_le32(packet.payload);s->last_remote_status=status;
  if(status!=1){zizhan_prm_fail(s,ZizhanPrmNotReady);return -1;}
  s->framework_ready=1;s->phase=ZizhanPrmRequestReady;
 }else{
  /* Fixed public response:status only or{request_opcode,status}.
   * zizhan native audio_prm callback at0x88c checks the parameter id;
   * independent mode5 permits only our exact HW_CORE id,not an arbitrary
   * first word. Opcode/peer/port/token/length and status remain mandatory. */
  if(s->clock_mode){
   /* Native token0 is shared across adjacent resources. Parameter id is
    * mandatory here,so a delayed core ACK cannot become a clock ACK. */
   if(packet.payload_bytes!=8||zizhan_prm_le32(packet.payload)!=s->expected_parameter){zizhan_prm_fail(s,ZizhanPrmMalformed);return -1;}
   status=zizhan_prm_le32(packet.payload+4);
  }else if(packet.payload_bytes==4)status=zizhan_prm_le32(packet.payload);
  else if(packet.payload_bytes==8&&
          (zizhan_prm_le32(packet.payload)==(s->phase==ZizhanPrmRequestWait?AR_OP_PRM_REQUEST:AR_OP_PRM_RELEASE)||
           (s->native_parameter_reply&&zizhan_prm_le32(packet.payload)==0x08001032u)))status=zizhan_prm_le32(packet.payload+4);
  else{zizhan_prm_fail(s,ZizhanPrmMalformed);return -1;}
  s->last_remote_status=status;
  if(status){zizhan_prm_fail(s,ZizhanPrmRemoteError);return -1;}
  if(s->phase==ZizhanPrmRequestWait){s->lease_held=1;s->request_ack=1;s->phase=s->clock_mode?ZizhanPrmClockRequestReady:ZizhanPrmReleaseReady;}
  else if(s->phase==ZizhanPrmClockRequestWait){s->clock_held=1;s->clock_request_ack=1;s->phase=ZizhanPrmClockReleaseReady;}
  else if(s->phase==ZizhanPrmClockReleaseWait){s->clock_held=0;s->clock_release_ack=1;s->phase=ZizhanPrmReleaseReady;}
  else{s->lease_held=0;s->release_ack=1;s->phase=ZizhanPrmDone;}
 }
 ++s->acknowledged;return 1;
}
static inline void zizhan_prm_tick(ZizhanPrmState *s,uint64_t now){
 if(s->phase==ZizhanPrmDone||s->phase==ZizhanPrmFailed)return;
 if(now<s->began_us||now-s->began_us>=8000000){zizhan_prm_fail(s,ZizhanPrmTimeout);return;}
 if((s->phase==ZizhanPrmFrameworkWait||s->phase==ZizhanPrmRequestWait||s->phase==ZizhanPrmReleaseWait||
     s->phase==ZizhanPrmClockRequestWait||s->phase==ZizhanPrmClockReleaseWait)&&
    (now<s->command_us||now-s->command_us>=2000000))zizhan_prm_fail(s,ZizhanPrmTimeout);
}
#endif
