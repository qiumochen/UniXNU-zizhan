/* Production native PCM graph/media owner;no DMA,buffer or device. */
#include <assert.h>
#include <stdio.h>
#include "ZizhanPcm.h"
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static unsigned answer(ZizhanPcmState *s,uint8_t *out,unsigned status){
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=s->expected_port;p.dst_port=ZizhanPcmPort;p.token=s->token;p.opcode=GPR_OP_BASIC_RESULT;
 uint8_t payload[8];put(payload,s->opcode);put(payload+4,status);return gpr_encode(out,512,&p,payload,8);
}
int main(void){
 uint8_t tx[512],rx[512];GprPacket p;unsigned n=zizhan_pcm_open(tx,512,1),at=40,links=0,modules=0;
 assert(n&&n<=512&&!gpr_decode(tx,n,&p)&&p.opcode==AR_OP_GRAPH_OPEN&&p.src_port==ZizhanPcmPort);
 while(at<n){unsigned id=zizhan_tdm_le32(tx+at+4),size=zizhan_tdm_le32(tx+at+8);assert(size<=n-at-16);
  if(id==0x8001002){assert(zizhan_tdm_le32(tx+at+20)==ZizhanPcmSg&&zizhan_tdm_le32(tx+at+24)==ZizhanPcmContainer&&zizhan_tdm_le32(tx+at+28)==1&&zizhan_tdm_le32(tx+at+32)==AR_MOD_WR_SHMEM&&zizhan_tdm_le32(tx+at+36)==ZizhanPcmShmem);++modules;}
  if(id==0x8001004){assert(zizhan_tdm_le32(tx+at+16)==1&&zizhan_tdm_le32(tx+at+20)==ZizhanPcmShmem&&zizhan_tdm_le32(tx+at+24)==1&&zizhan_tdm_le32(tx+at+28)==ZizhanTdmIid&&zizhan_tdm_le32(tx+at+32)==2);++links;}
  at+=16+((size+7)&~7u);
 }assert(at==n&&modules==1&&links==1);
 n=zizhan_pcm_media(tx,512,2);assert(n==88&&!gpr_decode(tx,n,&p)&&p.dst_port==ZizhanPcmShmem&&p.opcode==AR_OP_SET_CFG);
 assert(zizhan_tdm_le32(tx+48)==32&&zizhan_tdm_le32(tx+64)==36);
 /* Independent official packed PCM structure:96k,24-bit,MSB2,interleaved1,
  * 32 storage bits,Q31,little endian1,three channel mapping bytes. */
 const uint8_t pcm[]={0,0x77,1,0,24,0,2,0,32,0,31,0,1,0,3,0,1,2,3};
 assert(!memcmp(tx+68,pcm,sizeof pcm)&&tx[87]==0);
 for(unsigned capacity=0;capacity<88;++capacity)assert(!zizhan_pcm_media(tx,capacity,3));
 for(unsigned fault=0;fault<10;++fault){ZizhanPcmState s={0};unsigned step=0;
  while(s.phase!=ZizhanPcmDone&&s.phase!=ZizhanPcmFailed){
   if(s.phase==ZizhanPcmHold){assert(s.opened&&s.running&&s.ack==4);s.phase=ZizhanPcmStop;}
   assert(zizhan_pcm_next(&s,1000,tx)&&!zizhan_pcm_next(&s,1001,tx));++step;
   unsigned len=answer(&s,rx,fault==step?5:0);
   if(fault>=7){if(fault==7)rx[16]^=1;else if(fault==8)rx[8]^=1;else rx[12]^=1;
    assert(!zizhan_pcm_receive(&s,rx,len));zizhan_pcm_tick(&s,2001000);assert(s.phase==ZizhanPcmFailed&&s.uncertain);break;}
   int accepted=zizhan_pcm_receive(&s,rx,len);if(fault==step){assert(accepted==-1&&s.status==5&&s.uncertain);break;}assert(accepted==1);
  }
  if(!fault)assert(s.sent==6&&s.ack==6&&!s.opened&&!s.running&&!s.uncertain);
  else assert(s.phase==ZizhanPcmFailed&&!zizhan_pcm_next(&s,3000000,tx));
 }
 for(unsigned fault=0;fault<3;++fault){ZizhanPcmState s={0};s.joint=1;s.phase=ZizhanPcmStop;s.opened=s.running=1;
  for(unsigned step=0;step<2;++step){unsigned n=zizhan_pcm_next(&s,1000,tx);assert(n==72&&zizhan_tdm_le32(tx+48)==12&&zizhan_tdm_le32(tx+56)==2&&zizhan_tdm_le32(tx+60)==ZizhanPcmSg&&zizhan_tdm_le32(tx+64)==ZizhanTdmSg);
   unsigned len=answer(&s,rx,fault==step+1?5:0);int rc=zizhan_pcm_receive(&s,rx,len);
   if(fault==step+1){assert(rc==-1&&s.uncertain);break;}assert(rc==1);
  }
  if(!fault)assert(s.phase==ZizhanPcmDone&&!s.running&&!s.opened&&!s.uncertain);else assert(s.phase==ZizhanPcmFailed);
 }
 assert(!zizhan_pcm_joint_command(tx,512,1,AR_OP_GRAPH_START));
 puts("PASS native PCM-to-TDM graph,official exactpacked24/32/MSB/Q31 media,strict6ACK/control failures;no DMA/data/hardware");
}
