/* Production endpoint encoder/owner;synthetic responses,never hardware. */
#include <assert.h>
#include <stdio.h>
#include "ZizhanTdm.h"
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static unsigned answer(ZizhanTdmState *s,uint8_t *out,unsigned status){
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=1;p.dst_port=ZizhanTdmPort;p.token=s->token;p.opcode=GPR_OP_BASIC_RESULT;
 uint8_t payload[8];put(payload,s->opcode);put(payload+4,status);return gpr_encode(out,512,&p,payload,8);
}
int main(void){
 uint8_t tx[512],rx[512];GprPacket p;
 unsigned n=zizhan_tdm_open(tx,512,1);assert(n&&n<=512&&!gpr_decode(tx,n,&p)&&p.opcode==AR_OP_GRAPH_OPEN&&p.src_port==ZizhanTdmPort);
 unsigned modules=0,at=40;
 while(at<n){unsigned iid=zizhan_tdm_le32(tx+at),id=zizhan_tdm_le32(tx+at+4),size=zizhan_tdm_le32(tx+at+8);assert(iid==1&&size<=n-at-16);
  if(id==0x8001002){assert(zizhan_tdm_le32(tx+at+16)==1&&zizhan_tdm_le32(tx+at+24)==ZizhanTdmContainer&&zizhan_tdm_le32(tx+at+32)==0x700100e&&zizhan_tdm_le32(tx+at+36)==ZizhanTdmIid);++modules;}
  at+=16+((size+7)&~7u);
 }
 assert(at==n&&modules==1);
 n=zizhan_tdm_config(tx,512,2);assert(!gpr_decode(tx,n,&p)&&p.opcode==AR_OP_SET_CFG&&p.src_port==ZizhanTdmPort);
 const unsigned ids[]={0x800101b,0x8001017,0x8001018,0x800106a,0x800113c},sizes[]={28,12,4,4,16};
 /* Original Forte tagged channel3 body;not generated from encoder. */
 const uint8_t tdm[]={0,0,0,0,2,0,0,0,1,0,1,0,7,0,0,0,4,0,32,0,0,0,0,0,1,0,0,0};
 at=40;for(unsigned i=0;i<5;++i){assert(zizhan_tdm_le32(tx+at)==ZizhanTdmIid&&zizhan_tdm_le32(tx+at+4)==ids[i]&&zizhan_tdm_le32(tx+at+8)==sizes[i]&&!zizhan_tdm_le32(tx+at+12));
  if(!i)assert(!memcmp(tx+at+16,tdm,sizeof tdm));
  if(i==1)assert(zizhan_tdm_le32(tx+at+16)==96000&&tx[at+20]==24&&tx[at+22]==3&&zizhan_tdm_le32(tx+at+24)==1);
  if(i==2){assert(zizhan_tdm_le32(tx+at+16)==1);}if(i==3)assert(zizhan_tdm_le32(tx+at+16)==2);
  if(i==4)assert(!zizhan_tdm_le32(tx+at+16)&&!zizhan_tdm_le32(tx+at+20)&&zizhan_tdm_le32(tx+at+24)==4&&!zizhan_tdm_le32(tx+at+28));
  at+=16+((sizes[i]+7)&~7u);
 }assert(at==n&&n==200);
 for(unsigned fault=0;fault<8;++fault){ZizhanTdmState s={0};unsigned step=0;
  while(s.phase!=ZizhanTdmDone&&s.phase!=ZizhanTdmFailed){
   if(s.phase==ZizhanTdmHold){assert(s.opened&&s.running&&s.ack==4);s.phase=ZizhanTdmStop;}
   assert(zizhan_tdm_next(&s,1000,tx));assert(!zizhan_tdm_next(&s,1001,tx));++step;
   unsigned len=answer(&s,rx,fault==step?5:0);
   if(fault==7){rx[16]^=1;assert(!zizhan_tdm_receive(&s,rx,len));zizhan_tdm_tick(&s,2001000);assert(s.phase==ZizhanTdmFailed&&s.uncertain);break;}
   int accepted=zizhan_tdm_receive(&s,rx,len);
   if(fault==step){assert(accepted==-1&&s.status==5&&s.uncertain);break;}assert(accepted==1);
  }
  if(!fault)assert(s.phase==ZizhanTdmDone&&s.sent==6&&s.ack==6&&!s.opened&&!s.running&&!s.uncertain);
  else assert(s.phase==ZizhanTdmFailed&&!zizhan_tdm_next(&s,3000000,tx));
 }
 puts("PASS native TDM endpoint/ACDB golden fields,exact padding,module-only graph,strict six-command owner/failures;no hardware");
}
