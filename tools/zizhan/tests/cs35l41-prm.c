/* Real GPR/AudioReach encoder/owner,synthetic DSP responses only. */
#include <assert.h>
#include <stdio.h>
#include "ZizhanAudioPrm.h"
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static unsigned reply(ZizhanPrmState *s,uint8_t *out,uint32_t opcode,unsigned status,unsigned variant){
 uint8_t payload[8]={0};GprPacket p={0};p.src_domain=GPR_DOMAIN_ADSP;p.dst_domain=GPR_DOMAIN_APPS;
 p.src_port=s->expected_source;p.dst_port=s->expected_port;p.token=s->token;p.opcode=opcode;
 if(variant){put(payload,s->expected_request);put(payload+4,status);}
 else put(payload,status);
 return gpr_encode(out,512,&p,payload,variant?8:4);
}
static void ready(ZizhanPrmState *s){uint8_t out[512],rx[512];zizhan_prm_init(s,100);
 assert(zizhan_prm_next(s,200,out)==24);assert(!zizhan_prm_next(s,201,out));
 unsigned length=reply(s,rx,GPR_OP_APM_SPF_STATE,1,0);assert(zizhan_prm_receive(s,rx,length)==1&&s->phase==ZizhanPrmRequestReady);
}
int main(void){
 ZizhanPrmState s;uint8_t out[512],rx[512],reference[512];
 for(unsigned variant=0;variant<2;++variant){
  ready(&s);unsigned n=zizhan_prm_next(&s,300,out);assert(n==60&&s.sent==2);
  assert(ar_prm_vote(reference,512,ZizhanPrmPort,ZizhanPrmTokenBase+2,2,1)==n&&!memcmp(out,reference,n));
  unsigned len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,variant);assert(zizhan_prm_receive(&s,rx,len)==1&&s.lease_held&&s.request_ack);
  n=zizhan_prm_next(&s,400,out);assert(n==60&&s.sent==3);
  assert(ar_prm_vote(reference,512,ZizhanPrmPort,ZizhanPrmTokenBase+3,2,0)==n&&!memcmp(out,reference,n));
  len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,variant);assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&!s.lease_held&&s.release_ack&&s.acknowledged==3&&!s.uncertain);
  assert(!zizhan_prm_next(&s,401,out));assert(!zizhan_prm_receive(&s,rx,len));
 }
 for(unsigned corrupt=0;corrupt<8;++corrupt){
  ready(&s);zizhan_prm_next(&s,300,out);unsigned length=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,0);
  if(corrupt==0){rx[4]=9;}if(corrupt==1)rx[5]=9;if(corrupt==2)rx[8]^=1;if(corrupt==3)rx[12]^=1;
  if(corrupt==4){rx[16]^=1;}if(corrupt==5)rx[20]^=1;if(corrupt==6)length--;if(corrupt==7)rx[length++]=0;
  assert(!zizhan_prm_receive(&s,rx,length)&&s.phase==ZizhanPrmRequestWait&&!s.lease_held);
  zizhan_prm_tick(&s,2000300);assert(s.phase==ZizhanPrmFailed&&s.uncertain&&!zizhan_prm_next(&s,2000301,out));
 }
 ready(&s);zizhan_prm_next(&s,300,out);unsigned len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,5,0);
 assert(zizhan_prm_receive(&s,rx,len)==-1&&s.phase==ZizhanPrmFailed&&s.last_remote_status==5);
 ready(&s);zizhan_prm_next(&s,300,out);len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,1);put(rx+24,0x08001032);
 assert(zizhan_prm_receive(&s,rx,len)==-1&&s.reason==ZizhanPrmMalformed);
 ready(&s);zizhan_prm_next(&s,300,out);len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,0);zizhan_prm_receive(&s,rx,len);zizhan_prm_next(&s,400,out);
 len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,9,0);assert(zizhan_prm_receive(&s,rx,len)==-1&&s.lease_held&&s.uncertain&&!s.release_ack);
 ready(&s);zizhan_prm_next(&s,300,out);zizhan_prm_fail(&s,ZizhanPrmTransport);assert(s.uncertain&&!zizhan_prm_next(&s,400,out));
 zizhan_prm_init(&s,100);zizhan_prm_next(&s,200,out);len=reply(&s,rx,GPR_OP_APM_SPF_STATE,0,0);
 assert(zizhan_prm_receive(&s,rx,len)==-1&&s.reason==ZizhanPrmNotReady&&!s.uncertain);
 ready(&s);zizhan_prm_next(&s,300,out);len=reply(&s,rx,GPR_OP_BASIC_RESULT,0,1);
 assert(!zizhan_prm_receive(&s,rx,len)&&s.phase==ZizhanPrmRequestWait&&!s.lease_held);
 zizhan_prm_init(&s,100);s.phase=ZizhanPrmRequestReady;
 assert(zizhan_prm_next(&s,200,out)==60&&s.sent==1&&!s.acknowledged);
 len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,1);assert(zizhan_prm_receive(&s,rx,len)==1&&s.lease_held);
 assert(zizhan_prm_next(&s,300,out)==60);len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,1);
 assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&s.sent==2&&s.acknowledged==2&&!s.lease_held);
 zizhan_prm_init(&s,100);s.phase=ZizhanPrmRequestReady;s.native_wire=1;
 assert(zizhan_prm_next(&s,200,out)==60&&s.token==0&&zizhan_prm_le32(out+8)==7);
 assert(zizhan_prm_le32(out)==0x3c60&&zizhan_prm_le32(out+4)==0x302&&zizhan_prm_le32(out+12)==2);
 assert(ar_prm_vote(reference,512,7,0,2,1)==60&&!memcmp(out,reference,60));
 /* Independent words reconstructed from actual native module's packet
  * stores (source7/token0,60B,one block2 parameter),not second encoder. */
 const uint32_t native_words[]={0x3c60,0x302,7,2,0,0x0100100f,0,0,0,20,2,0x08001032,4,0,2};
 for(unsigned i=0;i<15;++i)assert(zizhan_prm_le32(out+i*4)==native_words[i]);
 len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,1);assert(zizhan_prm_receive(&s,rx,len)==1);
 assert(zizhan_prm_next(&s,300,out)==60&&s.token==0);
 for(unsigned i=0;i<15;++i)assert(zizhan_prm_le32(out+i*4)==(i==5?0x01001010:native_words[i]));
 len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,1);assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&!s.lease_held);
 /* Independent native SPF module stores: source3,destination1,token0.
  * Settle cannot stand in for readiness; a rejection sends no resource. */
 zizhan_prm_init(&s,100);s.native_wire=1;s.native_framework=1;
 assert(!zizhan_prm_next(&s,1000099,out)&&!s.sent&&s.phase==ZizhanPrmIdle);
 assert(zizhan_prm_next(&s,1000100,out)==24&&!s.framework_ready);
 const uint32_t apm_words[]={0x1860,0x302,3,1,0,0x01001021};
 for(unsigned i=0;i<6;++i)assert(zizhan_prm_le32(out+4*i)==apm_words[i]);
 len=reply(&s,rx,GPR_OP_APM_SPF_STATE,1,0);rx[12]=7;
 assert(!zizhan_prm_receive(&s,rx,len)&&!s.framework_ready);
 rx[12]=3;assert(zizhan_prm_receive(&s,rx,len)==1&&s.framework_ready);
 assert(zizhan_prm_next(&s,1000200,out)==60&&s.expected_port==7&&s.token==0);
 len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,1);assert(zizhan_prm_receive(&s,rx,len)==1);
 assert(zizhan_prm_next(&s,1000300,out)==60);
 len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,1);assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&s.acknowledged==3&&!s.uncertain);
 zizhan_prm_init(&s,100);s.native_wire=1;s.native_framework=1;assert(zizhan_prm_next(&s,1000100,out)==24);
 len=reply(&s,rx,GPR_OP_APM_SPF_STATE,0,0);assert(zizhan_prm_receive(&s,rx,len)==-1&&!s.framework_ready&&s.sent==1&&!s.uncertain);
 assert(!zizhan_prm_next(&s,1000200,out));
 for(unsigned bad=0;bad<4;++bad){
  zizhan_prm_init(&s,100);s.native_wire=1;s.native_framework=1;s.native_parameter_reply=1;
  assert(zizhan_prm_next(&s,1000100,out)==24);len=reply(&s,rx,GPR_OP_APM_SPF_STATE,1,0);
  assert(zizhan_prm_receive(&s,rx,len)==1);assert(zizhan_prm_next(&s,1000200,out)==60);
  len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,bad==2?5:0,1);put(rx+24,bad==1?0x0800102c:0x08001032);
  if(bad==3)put(rx+20,AR_OP_PRM_RELEASE_DONE);
  int result=zizhan_prm_receive(&s,rx,len);
  if(bad==1){assert(result==-1&&s.reason==ZizhanPrmMalformed&&!s.request_ack);continue;}
  if(bad==2){assert(result==-1&&s.reason==ZizhanPrmRemoteError&&s.last_remote_status==5&&!s.request_ack);continue;}
  if(bad==3){assert(result==0&&s.phase==ZizhanPrmRequestWait&&!s.request_ack);continue;}
  assert(result==1&&s.lease_held&&s.request_ack);assert(zizhan_prm_next(&s,1000300,out)==60);
  len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,1);put(rx+24,0x08001032);
  assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&!s.lease_held&&!s.uncertain);
 }
 const uint32_t clock_request[]={0x4c60,0x302,7,2,0,0x0100100f,0,0,0,36,2,0x0800102c,20,0,1,0x204,12288000,4,0};
 const uint32_t clock_release[]={0x4060,0x302,7,2,0,0x01001010,0,0,0,24,2,0x0800102c,8,0,1,0x204};
 for(unsigned fault=0;fault<6;++fault){
  zizhan_prm_init(&s,100);s.native_wire=s.native_framework=s.native_parameter_reply=s.clock_mode=1;
  assert(zizhan_prm_next(&s,1000100,out)==24);len=reply(&s,rx,GPR_OP_APM_SPF_STATE,1,0);assert(zizhan_prm_receive(&s,rx,len)==1);
  assert(zizhan_prm_next(&s,1000200,out)==60);len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,1);put(rx+24,0x08001032);
  assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmClockRequestReady&&s.lease_held);
  assert(zizhan_prm_next(&s,1000300,out)==76&&s.phase==ZizhanPrmClockRequestWait);
  for(unsigned i=0;i<19;++i)assert(zizhan_prm_le32(out+i*4)==clock_request[i]);
  len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,fault==2?5:0,1);put(rx+24,fault==1?0x08001032:0x0800102c);
  if(fault==3){zizhan_prm_tick(&s,3000300);assert(s.phase==ZizhanPrmFailed&&s.lease_held&&s.uncertain&&!s.clock_request_ack);continue;}
  if(fault==4)len=reply(&s,rx,AR_OP_PRM_REQUEST_DONE,0,0);
  int result=zizhan_prm_receive(&s,rx,len);
  if(fault==1||fault==2||fault==4){assert(result==-1&&s.lease_held&&s.uncertain&&!s.clock_request_ack&&!zizhan_prm_next(&s,1000400,out));continue;}
  assert(result==1&&s.clock_held&&s.clock_request_ack&&s.phase==ZizhanPrmClockReleaseReady);
  assert(zizhan_prm_next(&s,1000400,out)==64&&s.phase==ZizhanPrmClockReleaseWait);
  for(unsigned i=0;i<16;++i)assert(zizhan_prm_le32(out+i*4)==clock_release[i]);
  len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,fault==5?9:0,1);put(rx+24,0x0800102c);result=zizhan_prm_receive(&s,rx,len);
  if(fault==5){assert(result==-1&&s.clock_held&&s.lease_held&&s.uncertain&&!s.clock_release_ack&&!zizhan_prm_next(&s,1000500,out));continue;}
  assert(result==1&&!s.clock_held&&s.clock_release_ack&&s.lease_held&&s.phase==ZizhanPrmReleaseReady);
  assert(zizhan_prm_next(&s,1000500,out)==60);len=reply(&s,rx,AR_OP_PRM_RELEASE_DONE,0,1);put(rx+24,0x08001032);
  assert(zizhan_prm_receive(&s,rx,len)==1&&s.phase==ZizhanPrmDone&&!s.lease_held&&!s.clock_held&&s.sent==5&&s.acknowledged==5&&!s.uncertain);
 }
 puts("PASS actual GPR/PRM one-owner request/release,strict peer/token/response,status-only/echo variants,basicACK not completion,timeout/transport/retention;no hardware");return 0;
}
