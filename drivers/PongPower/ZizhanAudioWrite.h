/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_WRITE_H
#define ZIZHAN_AUDIO_WRITE_H
#include "ZizhanPcm.h"
/* Native WR_SHMEM V2: 44 bytes immediately after GPR, no command header.
 * This is a bounded qualification, not a playback service. The caller must
 * own the live mapping/handle and actual staged buffer.
 * No buffer reuse/unmap on timeout or an unconfirmed completion.
 * bytes defaults to the qualified 96kHz/3ch/32-bit 10ms buffer. The HAL
 * frontend may select its own declared size, but must prove the exact format
 * and period to the DSP before enabling output. */
enum {ZizhanWriteBytes=11520}; /* 960 frames, 96kHz, three 32-bit channels. */
enum {ZizhanWriteReady,ZizhanWriteWaiting,ZizhanWriteDone,ZizhanWriteFailed};
typedef struct {
 unsigned phase,sent,ack,ignored,uncertain,reason;uint32_t handle,token,remote,sequence;
 unsigned bytes;uint64_t address,began_us;
} ZizhanWriteState;
static inline void zizhan_write_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static inline void zizhan_write_fail(ZizhanWriteState *s,unsigned reason){s->phase=ZizhanWriteFailed;s->reason=reason;s->uncertain=1;}
static inline unsigned zizhan_write_next(ZizhanWriteState *s,uint64_t now,uint8_t out[512]){
 if(s->phase!=ZizhanWriteReady)return 0;
 if(!s->bytes)s->bytes=ZizhanWriteBytes;
 if(s->sequence>3||!s->bytes||s->bytes>65536u||(s->bytes&3u)||!s->handle||
    (s->address>>32)!=1||(s->address&4095)||!(uint32_t)s->address||(uint32_t)s->address>0xffff0000u){zizhan_write_fail(s,1);return 0;}
 uint8_t body[44]={0};zizhan_write_put32(body,(uint32_t)s->address);zizhan_write_put32(body+4,(uint32_t)(s->address>>32));
 zizhan_write_put32(body+8,s->handle);zizhan_write_put32(body+12,s->bytes);
 GprPacket p;memset(&p,0,sizeof p);p.src_domain=3;p.dst_domain=2;p.src_port=ZizhanPcmPort;p.dst_port=ZizhanPcmShmem;p.token=0x7a1b0001u+s->sequence;p.opcode=AR_OP_WRITE;
 const unsigned n=gpr_encode(out,512,&p,body,sizeof body);if(!n){zizhan_write_fail(s,1);return 0;}
 s->token=p.token;s->phase=ZizhanWriteWaiting;s->uncertain=1;s->began_us=now;++s->sent;return n;
}
static inline int zizhan_write_receive(ZizhanWriteState *s,const uint8_t *bytes,unsigned length){
 GprPacket p;if(s->phase!=ZizhanWriteWaiting||gpr_decode(bytes,length,&p)!=GprOk||p.size!=length||p.src_domain!=2||p.dst_domain!=3||p.src_port!=ZizhanPcmShmem||p.dst_port!=ZizhanPcmPort||p.token!=s->token){++s->ignored;return 0;}
 if(p.opcode==GPR_OP_BASIC_RESULT){
  if(p.payload_bytes!=8||zizhan_tdm_le32(p.payload)!=AR_OP_WRITE){zizhan_write_fail(s,2);return -1;}
  s->remote=zizhan_tdm_le32(p.payload+4);if(s->remote){zizhan_write_fail(s,3);return -1;}
  ++s->ignored;return 0; /* Basic success does not return buffer ownership. */
 }
 if(p.opcode!=AR_OP_WRITE_DONE){++s->ignored;return 0;}
 // V2 completion returns address/handle/status and four metadata words.
 // It contains no length: the exact token binds our single finite request.
 if(p.payload_bytes!=32||zizhan_tdm_le32(p.payload)!=(uint32_t)s->address||zizhan_tdm_le32(p.payload+4)!=(uint32_t)(s->address>>32)||zizhan_tdm_le32(p.payload+8)!=s->handle){zizhan_write_fail(s,2);return -1;}
 s->remote=zizhan_tdm_le32(p.payload+12);
 if(s->remote||zizhan_tdm_le32(p.payload+16)||zizhan_tdm_le32(p.payload+20)||zizhan_tdm_le32(p.payload+24)||zizhan_tdm_le32(p.payload+28)){zizhan_write_fail(s,3);return -1;}
 s->phase=ZizhanWriteDone;s->uncertain=0;++s->ack;return 1;
}
static inline void zizhan_write_tick(ZizhanWriteState *s,uint64_t now){if(s->phase==ZizhanWriteWaiting&&(now<s->began_us||now-s->began_us>=2000000))zizhan_write_fail(s,4);}
#endif
