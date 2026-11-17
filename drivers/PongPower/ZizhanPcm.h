/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_PCM_H
#define ZIZHAN_PCM_H
#include "ZizhanTdm.h"
/* Client-owned stream identifiers. Only graph/media qualification here:
 * no memory map,buffer,DMA,write-data or data-pad/output operation. */
enum {ZizhanPcmPort=0x2103,ZizhanPcmSg=0x4101,ZizhanPcmContainer=0x5101,
      ZizhanPcmShmem=0x6181};
enum {ZizhanPcmOpen,ZizhanPcmMedia,ZizhanPcmPrepare,
      ZizhanPcmStart,ZizhanPcmHold,ZizhanPcmStop,ZizhanPcmClose,ZizhanPcmDone,ZizhanPcmFailed};
#ifdef __cplusplus
extern "C" {
#endif
uint32_t zizhan_pcm_open(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_joint_command(uint8_t *,uint32_t,uint32_t,uint32_t);
uint32_t zizhan_pcm_media(uint8_t *,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
typedef struct {unsigned joint,phase,waiting,sent,ack,ignored,reason,opened,running,uncertain;
 uint32_t opcode,token,status,expected_port;uint64_t began_us;} ZizhanPcmState;
static inline void zizhan_pcm_fail(ZizhanPcmState *s,unsigned reason){s->reason=reason;s->uncertain=1;s->phase=ZizhanPcmFailed;}
static inline unsigned zizhan_pcm_next(ZizhanPcmState *s,uint64_t now,uint8_t out[512]){
 if(s->waiting||s->phase==ZizhanPcmHold||s->phase>=ZizhanPcmDone)return 0;
 unsigned n=0;s->token=0x7a190000u+s->sent+1;s->expected_port=AR_APM_IID;
 if(s->phase==ZizhanPcmOpen){s->opcode=AR_OP_GRAPH_OPEN;n=zizhan_pcm_open(out,512,s->token);}
 else if(s->phase==ZizhanPcmMedia){s->opcode=AR_OP_SET_CFG;s->expected_port=ZizhanPcmShmem;n=zizhan_pcm_media(out,512,s->token);}
 else{s->opcode=s->phase==ZizhanPcmPrepare?AR_OP_GRAPH_PREPARE:s->phase==ZizhanPcmStart?AR_OP_GRAPH_START:s->phase==ZizhanPcmStop?AR_OP_GRAPH_STOP:AR_OP_GRAPH_CLOSE;
  n=s->joint&&(s->phase==ZizhanPcmStop||s->phase==ZizhanPcmClose)?zizhan_pcm_joint_command(out,512,s->token,s->opcode):ar_graph_command(out,512,ZizhanPcmPort,s->token,s->opcode,ZizhanPcmSg);}
 if(!n){zizhan_pcm_fail(s,1);return 0;}s->waiting=1;s->began_us=now;++s->sent;return n;
}
static inline int zizhan_pcm_receive(ZizhanPcmState *s,const uint8_t *data,unsigned length){
 GprPacket p;if(!s->waiting||gpr_decode(data,length,&p)!=GprOk||p.size!=length||p.src_domain!=2||p.dst_domain!=3||
  p.src_port!=s->expected_port||p.dst_port!=ZizhanPcmPort||p.token!=s->token||p.opcode!=GPR_OP_BASIC_RESULT){++s->ignored;return 0;}
 if(p.payload_bytes!=8||zizhan_tdm_le32(p.payload)!=s->opcode){zizhan_pcm_fail(s,2);return -1;}
 s->status=zizhan_tdm_le32(p.payload+4);if(s->status){zizhan_pcm_fail(s,3);return -1;}
 s->waiting=0;++s->ack;if(s->phase==ZizhanPcmOpen)s->opened=1;
 if(s->phase==ZizhanPcmStart){s->running=1;}if(s->phase==ZizhanPcmStop)s->running=0;
 if(s->phase==ZizhanPcmClose){s->opened=0;}++s->phase;return 1;
}
static inline void zizhan_pcm_tick(ZizhanPcmState *s,uint64_t now){
 if(s->waiting&&(now<s->began_us||now-s->began_us>=2000000))zizhan_pcm_fail(s,4);
}
#endif
