/* Native one-buffer protocol, synthetic replies only; no DMA or hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioBatch.h"
static unsigned reply(ZizhanWriteState *s,uint8_t out[512],unsigned test){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)s->address);zizhan_write_put32(body+4,(uint32_t)(s->address>>32));zizhan_write_put32(body+8,s->handle);
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;p.token=s->token;p.opcode=AR_OP_WRITE_DONE;
 if(test==1){p.token++;}if(test==2)p.src_port++;if(test==3)p.dst_port++;if(test==4)p.src_domain++;
 if(test==5){body[0]^=64;}if(test==6)body[4]^=1;if(test==7)body[8]^=1;if(test==8)body[12]=5;
 if(test==9){body[16]=1;}if(test==10)body[20]=1;if(test==11)body[24]=1;if(test==12)body[28]=5;
 if(test==14||test==15){p.opcode=GPR_OP_BASIC_RESULT;memset(body,0,sizeof body);zizhan_write_put32(body,AR_OP_WRITE);body[4]=test==15?5:0;}
 return gpr_encode(out,512,&p,body,test==13?16:test==14||test==15?8:32);
}
static ZizhanAudioBatch fresh(unsigned count){
 ZizhanAudioBatch s={0};assert(zizhan_batch_init(&s,0x1f0000000ull,7,65536,count,100));return s;
}
int main(void){
 uint8_t tx[512],rx[512];ZizhanAudioBatch s=fresh(4);
 assert(!zizhan_batch_next(&s,101,tx));
 assert(!zizhan_batch_prepared(&s,1,ZizhanWriteBytes));
 assert(!zizhan_batch_prepared(&s,0,ZizhanWriteBytes-1));
 for(unsigned i=0;i<4;++i){
  assert(zizhan_batch_prepared(&s,i,ZizhanWriteBytes));
  assert(zizhan_batch_next(&s,200+i*1000,tx)==68);
  GprPacket p;assert(!gpr_decode(tx,68,&p));
  assert(p.token==0x7a1b0001u+i&&zizhan_tdm_le32(p.payload)==0xf0000000u+i*0x3000u);
  assert(s.write.address+ZizhanWriteBytes<=s.base+65536);
  ZizhanAudioBatch before=s;
  assert(!zizhan_batch_init(&s,0x1e0000000ull,8,65536,1,100));assert(!memcmp(&before,&s,sizeof s));
  assert(!zizhan_batch_next(&s,201+i*1000,tx));assert(!zizhan_batch_prepared(&s,i,ZizhanWriteBytes));
  unsigned n=reply(&s.write,rx,14);assert(!zizhan_batch_receive(&s,rx,n,202+i*1000));
  assert(s.completed==i&&s.confirmed_frames==i*960&&s.uncertain);
  n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,203+i*1000)==1);
  assert(s.completed==i+1&&s.confirmed_frames==(i+1)*960&&!s.uncertain);
  assert(!zizhan_batch_receive(&s,rx,n,204+i*1000));
 }
 assert(s.phase==ZizhanBatchDone&&s.confirmed_frames==3840&&zizhan_batch_buffers_returned(&s));
 assert(!zizhan_batch_next(&s,5000,tx)&&!zizhan_batch_prepared(&s,4,ZizhanWriteBytes));
 for(unsigned test=1;test<=15;++test){
  s=fresh(2);assert(zizhan_batch_prepared(&s,0,ZizhanWriteBytes));assert(zizhan_batch_next(&s,200,tx));
  unsigned n=reply(&s.write,rx,test);int accepted=zizhan_batch_receive(&s,rx,n,201);
  assert(!s.completed&&!s.confirmed_frames&&!zizhan_batch_buffers_returned(&s));
  if(test<=4||test==14){assert(!accepted&&s.phase==ZizhanBatchWaiting);zizhan_batch_tick(&s,100200);}
  else assert(accepted==-1);
  assert(s.phase==ZizhanBatchFailed&&s.uncertain);
  n=reply(&s.write,rx,0);assert(!zizhan_batch_receive(&s,rx,n,100201)&&!s.completed);
 }
 s=fresh(4);zizhan_batch_stop(&s);assert(zizhan_batch_buffers_returned(&s)&&!s.submitted);
 s=fresh(4);assert(zizhan_batch_prepared(&s,0,ZizhanWriteBytes));assert(zizhan_batch_next(&s,200,tx));
 zizhan_batch_stop(&s);assert(s.phase==ZizhanBatchWaiting&&!zizhan_batch_buffers_returned(&s));
 unsigned n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,201)==1);
 assert(s.completed==1&&s.phase==ZizhanBatchDone&&zizhan_batch_buffers_returned(&s));
 s=fresh(4);zizhan_batch_tick(&s,500100);assert(s.phase==ZizhanBatchFailed&&!s.uncertain&&!s.completed);
 s=fresh(4);assert(zizhan_batch_prepared(&s,0,ZizhanWriteBytes));assert(zizhan_batch_next(&s,200,tx));
 zizhan_batch_tick(&s,199);assert(s.phase==ZizhanBatchFailed&&s.uncertain);
 s=fresh(4);assert(zizhan_batch_prepared(&s,0,ZizhanWriteBytes));assert(zizhan_batch_next(&s,200,tx));
 zizhan_batch_send_failed(&s);assert(s.phase==ZizhanBatchFailed&&s.uncertain&&!zizhan_batch_buffers_returned(&s));
 s=fresh(2);assert(zizhan_batch_prepared(&s,0,ZizhanWriteBytes));assert(zizhan_batch_next(&s,200,tx));
 n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,201)==1);uint8_t old[512];memcpy(old,rx,n);
 assert(zizhan_batch_prepared(&s,1,ZizhanWriteBytes));assert(zizhan_batch_next(&s,202,tx));
 assert(!zizhan_batch_receive(&s,old,n,203)&&s.completed==1&&s.uncertain);
 n=reply(&s.write,rx,0);assert(zizhan_batch_receive(&s,rx,n,204)==1&&s.completed==2);
 for(unsigned test=0;test<8;++test){
  memset(&s,0,sizeof s);uint64_t base=0x1f0000000ull;unsigned bytes=65536,count=4,handle=7;
  if(test==0){handle=0;}if(test==1)bytes--;if(test==2)count=0;if(test==3)count=5;
  if(test==4){base=0xf0000000;}if(test==5)base++;if(test==6)base=0x100000000ull;if(test==7)base=0x1ffff1000ull;
  assert(!zizhan_batch_init(&s,base,handle,bytes,count,100));assert(!s.submitted&&!s.uncertain);
  assert(!zizhan_batch_next(&s,101,tx));
 }
 puts("PASS bounded four-buffer production WRITE/DONE batch, distinct slots/tokens, exact completion-only progress, no reinit, stop/drain, stale/duplicate/error/timeout retention; no DMA/HAL/hardware");
}
