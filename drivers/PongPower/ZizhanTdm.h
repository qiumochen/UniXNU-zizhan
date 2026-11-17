/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_TDM_H
#define ZIZHAN_TDM_H
#include "ShengGpr.h"
#include "ShengAudioReach.h"
#include <string.h>
/* Client-owned identifiers in public APM ranges;not board resources.
 * Hardware parameters come from this phone's actual Forte ACDB/AGM.
 * Only a TDM endpoint: no stream,shared memory,DMA or sample generator. */
enum {ZizhanTdmPort=0x2102,ZizhanTdmSg=0x4105,ZizhanTdmContainer=0x5105,ZizhanTdmIid=0x6281};
enum {ZizhanTdmOpen,ZizhanTdmConfig,ZizhanTdmPrepare,ZizhanTdmStart,ZizhanTdmHold,
      ZizhanTdmStop,ZizhanTdmClose,ZizhanTdmDone,ZizhanTdmFailed};
#ifdef __cplusplus
extern "C" {
#endif
uint32_t zizhan_tdm_open(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_tdm_config(uint8_t *,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
typedef struct {unsigned phase,waiting,sent,ack,ignored,reason,opened,running,uncertain;uint32_t opcode,token,status;uint64_t began_us;} ZizhanTdmState;
static inline uint32_t zizhan_tdm_le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void zizhan_tdm_fail(ZizhanTdmState *s,unsigned reason){s->reason=reason;s->uncertain=1;s->phase=ZizhanTdmFailed;}
static inline unsigned zizhan_tdm_next(ZizhanTdmState *s,uint64_t now,uint8_t out[512]){
 if(s->waiting||s->phase==ZizhanTdmHold||s->phase>=ZizhanTdmDone)return 0;
 unsigned n=0;s->token=0x7a180000u+s->sent+1;
 if(s->phase==ZizhanTdmOpen){s->opcode=AR_OP_GRAPH_OPEN;n=zizhan_tdm_open(out,512,s->token);}
 else if(s->phase==ZizhanTdmConfig){s->opcode=AR_OP_SET_CFG;n=zizhan_tdm_config(out,512,s->token);}
 else{s->opcode=s->phase==ZizhanTdmPrepare?AR_OP_GRAPH_PREPARE:s->phase==ZizhanTdmStart?AR_OP_GRAPH_START:s->phase==ZizhanTdmStop?AR_OP_GRAPH_STOP:AR_OP_GRAPH_CLOSE;
  n=ar_graph_command(out,512,ZizhanTdmPort,s->token,s->opcode,ZizhanTdmSg);}
 if(!n){zizhan_tdm_fail(s,1);return 0;}s->waiting=1;s->began_us=now;++s->sent;return n;
}
static inline int zizhan_tdm_receive(ZizhanTdmState *s,const uint8_t *data,unsigned length){
 GprPacket p;if(!s->waiting||gpr_decode(data,length,&p)!=GprOk||p.size!=length||p.src_domain!=2||p.dst_domain!=3||
   p.src_port!=1||p.dst_port!=ZizhanTdmPort||p.token!=s->token||p.opcode!=GPR_OP_BASIC_RESULT){++s->ignored;return 0;}
 if(p.payload_bytes!=8||zizhan_tdm_le32(p.payload)!=s->opcode){zizhan_tdm_fail(s,2);return -1;}
 s->status=zizhan_tdm_le32(p.payload+4);if(s->status){zizhan_tdm_fail(s,3);return -1;}
 s->waiting=0;++s->ack;if(s->phase==ZizhanTdmOpen)s->opened=1;
 if(s->phase==ZizhanTdmStart){s->running=1;}if(s->phase==ZizhanTdmStop)s->running=0;
 if(s->phase==ZizhanTdmClose){s->opened=0;}++s->phase;return 1;
}
static inline void zizhan_tdm_tick(ZizhanTdmState *s,uint64_t now){
 if(s->waiting&&(now<s->began_us||now-s->began_us>=2000000))zizhan_tdm_fail(s,4);
}
#endif
