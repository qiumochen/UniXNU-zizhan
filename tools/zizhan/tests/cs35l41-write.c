/* Native one-buffer protocol, synthetic replies only; no DMA or hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioWrite.h"
static unsigned reply(ZizhanWriteState *s,uint8_t out[512],unsigned test){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)s->address);zizhan_write_put32(body+4,(uint32_t)(s->address>>32));zizhan_write_put32(body+8,s->handle);
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;p.token=s->token;p.opcode=AR_OP_WRITE_DONE;
 if(test==1){p.token++;}if(test==2)p.src_port++;if(test==3)p.dst_port++;if(test==4)p.src_domain++;
 if(test==5){body[0]^=64;}if(test==6)body[4]^=1;if(test==7)body[8]^=1;if(test==8)body[12]=5;
 if(test==9){body[16]=1;}if(test==10)body[20]=1;if(test==11)body[24]=1;if(test==12)body[28]=5;
 if(test==14||test==15){p.opcode=GPR_OP_BASIC_RESULT;memset(body,0,sizeof body);zizhan_write_put32(body,AR_OP_WRITE);body[4]=test==15?5:0;}
 return gpr_encode(out,512,&p,body,test==13?16:test==14||test==15?8:32);
}
int main(void){
 uint8_t tx[512],rx[512];
 for(unsigned test=0;test<18;++test){ZizhanWriteState s={0};s.address=0x1f0000000ull;s.handle=0x1234;
  if(test==16){s.handle=0;}if(test==17)s.address++;
  unsigned n=zizhan_write_next(&s,100,tx);
  if(test>=16){assert(!n&&!s.sent&&s.phase==ZizhanWriteFailed);continue;}
  GprPacket p;assert(n==68&&!gpr_decode(tx,n,&p)&&p.src_port==0x2103&&p.dst_port==0x6181&&p.opcode==AR_OP_WRITE&&p.payload_bytes==44);
  assert(zizhan_tdm_le32(p.payload)==0xf0000000&&zizhan_tdm_le32(p.payload+4)==1&&zizhan_tdm_le32(p.payload+8)==0x1234&&zizhan_tdm_le32(p.payload+12)==11520);
  for(unsigned i=16;i<44;++i){assert(!p.payload[i]);}assert(!zizhan_write_next(&s,101,tx));
  unsigned length=reply(&s,rx,test);int accepted=zizhan_write_receive(&s,rx,length);
  if(!test){assert(accepted==1&&s.phase==ZizhanWriteDone&&s.sent==1&&s.ack==1&&!s.uncertain);assert(!zizhan_write_next(&s,102,tx));assert(!zizhan_write_receive(&s,rx,length)&&s.ack==1);}
  else if(test<=4||test==14){assert(!accepted&&s.phase==ZizhanWriteWaiting&&!s.ack&&s.uncertain);zizhan_write_tick(&s,2000100);assert(s.phase==ZizhanWriteFailed&&s.uncertain);}
  else assert(accepted==-1&&s.phase==ZizhanWriteFailed&&!s.ack&&s.uncertain);
 }
 ZizhanWriteState s={0};s.address=0x1f0000000ull;s.handle=7;assert(zizhan_write_next(&s,100,tx));zizhan_write_tick(&s,99);assert(s.phase==ZizhanWriteFailed&&s.uncertain);
 ZizhanWriteState second={0};second.address=0x1f0000000ull;second.handle=7;second.sequence=1;assert(zizhan_write_next(&second,100,tx)&&second.token==0x7a1b0002);
 unsigned n=reply(&second,rx,0);rx[16]^=3;assert(!zizhan_write_receive(&second,rx,n)&&!second.ack&&second.uncertain);n=reply(&second,rx,0);assert(zizhan_write_receive(&second,rx,n)==1);
 ZizhanWriteState extra={0};extra.address=0x1f0000000ull;extra.handle=7;extra.sequence=4;assert(!zizhan_write_next(&extra,100,tx)&&extra.phase==ZizhanWriteFailed&&!extra.sent);
 puts("PASS native68B onebuffer WRITE/32B V2 completion,exact board endpoint/address/handle/token/status,metadata0,basic-not-completion,timeout and retention;no DMA/hardware");
}
