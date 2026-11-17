/* Finite HAL-ring -> packed slot -> WRITE/DONE bridge; synthetic only. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioStream.h"
static unsigned reply(ZizhanWriteState *s,uint8_t out[512]){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)s->address);
 zizhan_write_put32(body+4,(uint32_t)(s->address>>32));zizhan_write_put32(body+8,s->handle);
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;
 p.token=s->token;p.opcode=AR_OP_WRITE_DONE;return gpr_encode(out,512,&p,body,32);
}
int main(void){
 static int16_t ring[ZizhanRingFrames*2];uint8_t slot[ZizhanRingPcmBytes],tx[512],rx[512];
 for(unsigned i=0;i<ZizhanRingFrames;++i){ring[2*i]=(int16_t)(i&0x7fff);ring[2*i+1]=(int16_t)(-(int)(i&0x7fff));}
 ZizhanAudioStream s={0};assert(zizhan_stream_init(&s,0x1f0000000ull,7,4,ZizhanRingFrames,0,100));
 assert(!zizhan_stream_stage(&s,ring,479,0,slot)&&s.underruns==1&&s.reason==ZizhanStreamUnderrun);
 for(unsigned i=0;i<4;++i){
  assert(zizhan_stream_stage(&s,ring,(i+1)*480,i,slot));
  assert(s.phase==ZizhanStreamStaged&&s.next_frame==i*480);
  const unsigned n=zizhan_stream_commit(&s,i,200+i*1000,tx);assert(n==68);
  GprPacket p;assert(!gpr_decode(tx,n,&p)&&zizhan_tdm_le32(p.payload+12)==2880);
  const unsigned r=reply(&s.batch.write,rx);assert(zizhan_stream_receive(&s,rx,r,201+i*1000)==1);
  assert(s.batch.confirmed_frames==(i+1)*480&&s.next_frame==(i+1)*480);
 }
 assert(s.batch.phase==ZizhanBatchDone&&zizhan_stream_done(&s));
 ZizhanAudioStream under={0};assert(zizhan_stream_init(&under,0x1f0000000ull,8,2,ZizhanRingFrames,0,100));
 assert(!zizhan_stream_stage(&under,ring,479,0,slot));
 assert(zizhan_stream_stage(&under,ring,960,0,slot));
 assert(zizhan_stream_commit(&under,0,200,tx)==68);
 zizhan_stream_stop(&under);assert(under.phase==ZizhanStreamDone&&!zizhan_stream_done(&under));
 const unsigned r=reply(&under.batch.write,rx);assert(zizhan_stream_receive(&under,rx,r,201)==1);
 assert(under.batch.completed==1&&zizhan_stream_done(&under));
 assert(!zizhan_stream_init(&under,0x1f0000000ull,8,2,ZizhanRingFrames,0,100));
 puts("PASS finite 48k HAL ring -> 2880-byte slots -> four WRITE/DONE, underrun gate and stop/drain; synthetic only");
}
