/* HAL-ring fetch and 24-bit packing plus finite 48kHz/2ch WRITE/DONE batches.
 * Synthetic ring/DONE only; no IOKit, DMA, DSP graph or hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioBatch.h"
#include "ZizhanAudioRing.h"
static unsigned reply(ZizhanWriteState *s,uint8_t out[512],unsigned test){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)s->address);
 zizhan_write_put32(body+4,(uint32_t)(s->address>>32));zizhan_write_put32(body+8,s->handle);
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;
 p.dst_port=ZizhanPcmPort;p.token=s->token;p.opcode=AR_OP_WRITE_DONE;
 if(test==1){p.token++;}if(test==2)body[10]^=1;if(test==3)body[12]=1;
 return gpr_encode(out,512,&p,body,test==4?16:32);
}
static void put16(int16_t *ring,unsigned frame,unsigned channel,int16_t value){
 ring[2*frame+channel]=value;
}
static void check24(const uint8_t *p,int32_t value){
 assert(p[0]==(uint8_t)value&&p[1]==(uint8_t)(value>>8)&&p[2]==(uint8_t)(value>>16));
}
int main(void){
 static int16_t ring[ZizhanRingFrames*2];uint8_t pcm[ZizhanRingPcmBytes];
 ZizhanRingReceipt rr;
 memset(ring,0,sizeof ring);
 put16(ring,65534,0,0x1234);put16(ring,65534,1,-1);
 put16(ring,65535,0,-32768);put16(ring,65535,1,32767);
 put16(ring,0,0,1);put16(ring,0,1,-2);
 put16(ring,1,0,32767);put16(ring,1,1,0);
 assert(zizhan_ring_pack(ring,ZizhanRingFrames,65534,pcm,&rr));
 assert(rr.packed==2880&&rr.reason==0&&!rr.underruns);
 check24(pcm+0,0x1234*256);check24(pcm+3,-256);
 check24(pcm+6,-8388608);check24(pcm+9,0x7fff*256);
 check24(pcm+12,256);check24(pcm+15,-512);
 check24(pcm+18,0x7fff*256);check24(pcm+21,0);
 for(unsigned i=24;i<sizeof pcm;++i)assert(pcm[i]==0);
 assert(zizhan_ring_ready(65534+480,65534,&rr));
 assert(!zizhan_ring_ready(65534+479,65534,&rr)&&rr.underruns==1&&rr.reason==2);
 assert(!zizhan_ring_ready(1,65534,&rr)&&rr.underruns==1);
 assert(!zizhan_ring_pack(NULL,ZizhanRingFrames,0,pcm,&rr)&&rr.reason==1);
 assert(!zizhan_ring_pack(ring,ZizhanRingFrames-1,0,pcm,&rr)&&rr.reason==1);
 assert(!zizhan_ring_pack(ring,ZizhanRingFrames,0,NULL,&rr));
 assert(!zizhan_ring_pack(ring,ZizhanRingFrames,UINT64_MAX-479,pcm,&rr)&&rr.reason==1);
 assert(!zizhan_ring_ready(0,0,NULL));

 ZizhanAudioBatch s={0};uint8_t tx[512],rx[512];
 assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,4,100));
 uint64_t frame=0,produced=480*4;
 for(unsigned i=0;i<4;++i){
  assert(zizhan_ring_ready(produced,frame,&rr));
  assert(zizhan_ring_pack(ring,ZizhanRingFrames,frame,pcm,&rr));
  assert(zizhan_batch_prepared(&s,i,2880));
  assert(zizhan_batch_next(&s,200+i*1000,tx)==68);
  GprPacket p;assert(!gpr_decode(tx,68,&p));
  assert(p.token==0x7a1b0001u+i&&zizhan_tdm_le32(p.payload)==0xf0000000u+i*0x3000u);
  assert(zizhan_tdm_le32(p.payload+4)==1&&zizhan_tdm_le32(p.payload+8)==7);
  assert(zizhan_tdm_le32(p.payload+12)==2880);
  frame+=480;
  unsigned n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,201+i*1000)==1);
  assert(s.confirmed_frames==(i+1)*480);
 }
 assert(s.phase==ZizhanBatchDone&&s.confirmed_frames==1920&&zizhan_batch_buffers_returned(&s));

 s=(ZizhanAudioBatch){0};assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,4,100));
 produced=480;frame=0;
 for(unsigned i=0;i<2;++i){
  assert(zizhan_ring_ready(produced,frame,&rr));
  assert(zizhan_ring_pack(ring,ZizhanRingFrames,frame,pcm,&rr));
  assert(zizhan_batch_prepared(&s,i,2880));
  assert(zizhan_batch_next(&s,200+i*1000,tx)==68);
  frame+=480;produced+=480;
  unsigned n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,201+i*1000)==1);
 }
 assert(!zizhan_ring_ready(produced-1,frame,&rr)&&rr.reason==2);
 assert(!zizhan_batch_next(&s,300,tx));
 assert(s.submitted==2&&s.confirmed_frames==960);

 struct Bad {unsigned extent,bytes,frames,count;uint64_t base;uint32_t handle;};
 static const struct Bad bad[]={
  {65536,0,480,4,0x1f0000000ull,7},{65536,2881,480,4,0x1f0000000ull,7},
  {65536,2880,0,4,0x1f0000000ull,7},{65536,2880,481,4,0x1f0000000ull,7},
  {65536,2880,480,5,0x1f0000000ull,7},{65536,2880,480,0,0x1f0000000ull,7},
  {65535,2880,480,4,0x1f0000000ull,7},{65536,2880,480,4,0x200000000ull,7},
  {65536,2880,480,4,0x1f0000000ull,0},{65536,2880,480,4,0x1f0000001ull,7},
  {65536,2880,480,4,0x1ffff0000ull,7}};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;++i)
  {ZizhanAudioBatch b={0};const int ok=zizhan_batch_init_ex(&b,bad[i].base,bad[i].handle,
   bad[i].extent,bad[i].bytes,bad[i].frames,bad[i].count,100);
   assert(!ok&&!b.submitted&&!b.uncertain);}
 s=(ZizhanAudioBatch){0};assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,2,100));
 assert(zizhan_batch_prepared(&s,0,2880));assert(zizhan_batch_next(&s,200,tx)==68);
 unsigned n=reply(&s.write,rx,1);assert(!zizhan_batch_receive(&s,rx,n,201)&&s.phase==ZizhanBatchWaiting);
 zizhan_batch_stop(&s);assert(!zizhan_batch_buffers_returned(&s));
 s=(ZizhanAudioBatch){0};assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,2,100));
 assert(zizhan_batch_prepared(&s,0,2880));assert(zizhan_batch_next(&s,200,tx)==68);
 n=reply(&s.write,rx,2);assert(zizhan_batch_receive(&s,rx,n,201)==-1&&s.phase==ZizhanBatchFailed&&s.uncertain);
 s=(ZizhanAudioBatch){0};assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,2,100));
 assert(zizhan_batch_prepared(&s,0,2880));assert(zizhan_batch_next(&s,200,tx)==68);
 n=reply(&s.write,rx,3);assert(zizhan_batch_receive(&s,rx,n,201)==-1&&s.phase==ZizhanBatchFailed);
 s=(ZizhanAudioBatch){0};assert(zizhan_batch_init_ex(&s,0x1f0000000ull,7,65536,2880,480,2,100));
 assert(zizhan_batch_prepared(&s,0,2880));assert(zizhan_batch_next(&s,200,tx)==68);
 n=reply(&s.write,rx,1);assert(!zizhan_batch_receive(&s,rx,n,201)&&s.phase==ZizhanBatchWaiting);
 zizhan_batch_tick(&s,100200);assert(s.phase==ZizhanBatchFailed&&s.uncertain);
 puts("PASS 48kHz stereo HAL ring -> 24-bit packed 10ms periods, underrun gate, four finite WRITE/DONE slots and exact 480-frame progress; no IOKit/DMA/DSP/hardware");
}
