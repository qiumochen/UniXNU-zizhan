/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_MEMORY_H
#define ZIZHAN_AUDIO_MEMORY_H
#include "ZizhanPcm.h"
#ifdef __cplusplus
extern "C" {
#endif
uint32_t zizhan_memory_map_packet(uint8_t *,uint32_t,uint32_t,uint64_t);
uint32_t zizhan_memory_unmap_packet(uint8_t *,uint32_t,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
enum {ZizhanMemoryPort=0x2104};
enum {ZizhanMemoryMap,ZizhanMemoryHeld,ZizhanMemoryUnmap,ZizhanMemoryDone,ZizhanMemoryFailed};
typedef struct {unsigned phase,waiting,sent,ack,ignored,reason,uncertain;uint32_t opcode,token,handle,remote;uint64_t address,began_us;} ZizhanMemoryState;
static inline void zizhan_memory_fail(ZizhanMemoryState *s,unsigned reason){s->phase=ZizhanMemoryFailed;s->reason=reason;s->uncertain=1;}
static inline unsigned zizhan_memory_next(ZizhanMemoryState *s,uint64_t now,uint8_t out[512]){
 if(s->waiting||s->phase==ZizhanMemoryHeld||s->phase>=ZizhanMemoryDone)return 0;
 if((s->address>>32)!=1||(s->address&4095)||!((uint32_t)s->address)||((uint32_t)s->address)>0xffff0000u){zizhan_memory_fail(s,1);return 0;}
 s->token=0x7a1a0000u+s->sent+1;unsigned n=0;
 if(s->phase==ZizhanMemoryMap){s->opcode=AR_OP_MEM_MAP;n=zizhan_memory_map_packet(out,512,s->token,s->address);}
 else{if(!s->handle){zizhan_memory_fail(s,1);return 0;}s->opcode=AR_OP_MEM_UNMAP;n=zizhan_memory_unmap_packet(out,512,s->token,s->handle);}
 if(!n){zizhan_memory_fail(s,1);return 0;}s->waiting=1;s->uncertain=1;s->began_us=now;++s->sent;return n;
}
static inline int zizhan_memory_receive(ZizhanMemoryState *s,const uint8_t *bytes,unsigned length){
 GprPacket p;if(!s->waiting||gpr_decode(bytes,length,&p)!=GprOk||p.size!=length||p.src_domain!=2||p.dst_domain!=3||p.src_port!=1||p.dst_port!=ZizhanMemoryPort||p.token!=s->token){++s->ignored;return 0;}
 if(s->phase==ZizhanMemoryMap&&p.opcode==AR_OP_MEM_MAP_DONE){
  if(p.payload_bytes!=4||!zizhan_tdm_le32(p.payload)){zizhan_memory_fail(s,2);return -1;}
  s->handle=zizhan_tdm_le32(p.payload);s->waiting=0;s->uncertain=0;++s->ack;s->phase=ZizhanMemoryHeld;return 1;
 }
 if(p.opcode!=GPR_OP_BASIC_RESULT){++s->ignored;return 0;}
 if(p.payload_bytes!=8||zizhan_tdm_le32(p.payload)!=s->opcode){zizhan_memory_fail(s,2);return -1;}
 s->remote=zizhan_tdm_le32(p.payload+4);if(s->remote){zizhan_memory_fail(s,3);return -1;}
 // A basic success for MAP is not an allocated handle.
 if(s->phase==ZizhanMemoryMap){++s->ignored;return 0;}
 s->waiting=0;s->uncertain=0;++s->ack;s->phase=ZizhanMemoryDone;return 1;
}
static inline void zizhan_memory_tick(ZizhanMemoryState *s,uint64_t now){if(s->waiting&&(now<s->began_us||now-s->began_us>=2000000))zizhan_memory_fail(s,4);}
#endif
