/* Host-only continuous WR_SHMEM flow: slot reuse, in-flight cap, unique tokens, drain; synthetic replies. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioFlow.h"
static unsigned done_for(uint32_t token,uint64_t address,uint32_t handle,uint32_t status,uint8_t out[512]){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)address);zizhan_write_put32(body+4,(uint32_t)(address>>32));
 zizhan_write_put32(body+8,handle);zizhan_write_put32(body+12,status);
 GprPacket p;memset(&p,0,sizeof p);p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;
 p.token=token;p.opcode=AR_OP_WRITE_DONE;return gpr_encode(out,512,&p,body,32);
}
static uint32_t sent_token(const uint8_t *pkt,unsigned n){GprPacket p;assert(gpr_decode(pkt,n,&p)==GprOk);assert(p.opcode==AR_OP_WRITE&&p.payload_bytes==44);return p.token;}
static int done(ZizhanAudioFlow *f,unsigned slot,uint64_t now){
 uint8_t rx[512];const unsigned n=done_for(f->slot[slot].write.token,zizhan_flow_slot_address(f,slot),f->handle,0,rx);
 return zizhan_flow_receive(f,rx,n,now);
}
int main(void){
 const uint64_t base=0x1f0000000ull;ZizhanAudioFlow f,bad;uint8_t tx[512],rx[512];uint64_t frame=0;
 /* init rejects */
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,7,0,2880,480,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,7,4,2880,480,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,0,2,2880,480,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,0x0f0000000ull,7,2,2880,480,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base+8,7,2,2880,480,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,7,2,2880,480,0,0,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,7,2,2880,480,0,30000001,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,base,7,2,0x3004,4,0,1000000,0));
 memset(&bad,0,sizeof bad);assert(!zizhan_flow_init(&bad,0x1ffffd000ull,7,2,2880,480,0,1000000,0));
 /* cap 2, out-of-order DONE, wrap to a busy slot */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,2,2880,480,16320,5000000,0));
 assert(!zizhan_flow_init(&f,base,7,2,2880,480,0,1000000,0));
 assert(zizhan_flow_claim(&f,&frame)==0&&frame==16320);
 assert(!zizhan_flow_submit(&f,1,10,tx));
 unsigned n=zizhan_flow_submit(&f,0,10,tx);assert(n);const uint32_t t0=sent_token(tx,n);
 {GprPacket p;gpr_decode(tx,n,&p);assert(zizhan_tdm_le32(p.payload)==(uint32_t)base&&zizhan_tdm_le32(p.payload+12)==2880);}
 assert(zizhan_flow_claim(&f,&frame)==1&&frame==16800);n=zizhan_flow_submit(&f,1,11,tx);assert(n&&sent_token(tx,n)!=t0);
 {GprPacket p;gpr_decode(tx,n,&p);assert(zizhan_tdm_le32(p.payload)==(uint32_t)(base+0x3000));}
 assert(zizhan_flow_claim(&f,&frame)==-1&&f.inflight==2&&f.peak_inflight==2);
 assert(done(&f,1,20)==1&&f.inflight==1&&f.completed==1);          /* slot 1 returns first */
 assert(zizhan_flow_claim(&f,&frame)==2);assert(zizhan_flow_submit(&f,2,21,tx));
 assert(done(&f,2,30)==1);assert(zizhan_flow_claim(&f,&frame)==3);assert(zizhan_flow_submit(&f,3,31,tx));
 assert(done(&f,3,40)==1);assert(zizhan_flow_claim(&f,&frame)==-1);  /* next is slot 0, still busy */
 /* a DONE for slot 0 with a wrong token is ignored */
 n=done_for(t0+0x100,base,7,0,rx);assert(zizhan_flow_receive(&f,rx,n,41)==0&&f.slot[0].busy);
 assert(done(&f,0,50)==1&&f.inflight==0&&f.completed==4&&f.confirmed_frames==1920);
 /* reuse slot 0: the replayed DONE of its previous use is not taken as the new one */
 assert(zizhan_flow_claim(&f,&frame)==0&&frame==16320+4*480);n=zizhan_flow_submit(&f,0,60,tx);const uint32_t t4=sent_token(tx,n);assert(t4!=t0);
 n=done_for(t0,base,7,0,rx);assert(zizhan_flow_receive(&f,rx,n,61)==0&&f.slot[0].busy&&f.completed==4);
 assert(done(&f,0,70)==1&&f.completed==5);
 /* stop: drain then Done; nothing more submitted */
 assert(zizhan_flow_claim(&f,&frame)==1);assert(zizhan_flow_submit(&f,1,80,tx));
 zizhan_flow_stop(&f);assert(f.phase==ZizhanFlowDraining&&zizhan_flow_claim(&f,&frame)==-1&&!zizhan_flow_submit(&f,2,81,tx));
 assert(!zizhan_flow_returned(&f));assert(done(&f,1,90)==1&&f.phase==ZizhanFlowDone&&zizhan_flow_returned(&f));
 assert(f.peak_us==40);  /* slot 0: sent at 10, returned at 50 */
 /* bad completion payload (wrong address) fails and stays uncertain */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,3,2880,480,0,5000000,0));
 assert(zizhan_flow_claim(&f,NULL)==0);assert(zizhan_flow_submit(&f,0,0,tx));
 n=done_for(f.slot[0].write.token,base+0x3000,7,0,rx);assert(zizhan_flow_receive(&f,rx,n,5)==-1&&f.phase==ZizhanFlowFailed&&f.uncertain&&!zizhan_flow_returned(&f));
 /* remote error status fails */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,3,2880,480,0,5000000,0));
 assert(zizhan_flow_submit(&f,0,0,tx));n=done_for(f.slot[0].write.token,base,7,9,rx);assert(zizhan_flow_receive(&f,rx,n,5)==-1&&f.phase==ZizhanFlowFailed);
 /* per-WRITE 100 ms timeout */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,3,2880,480,0,5000000,1000));
 assert(zizhan_flow_submit(&f,0,1000,tx));zizhan_flow_tick(&f,100999);assert(f.phase==ZizhanFlowRunning);
 zizhan_flow_tick(&f,101000);assert(f.phase==ZizhanFlowFailed&&f.reason==ZizhanFlowTimeout&&f.uncertain);
 /* stream limit: Running -> Draining -> Done */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,2,2880,480,0,50000,0));
 assert(zizhan_flow_submit(&f,0,0,tx));zizhan_flow_tick(&f,50000);assert(f.phase==ZizhanFlowDraining&&zizhan_flow_claim(&f,NULL)==-1);
 assert(done(&f,0,50010)==1&&zizhan_flow_returned(&f));
 /* 1000 continuous periods with three in flight; tokens wrap only in the low 16 bits */
 memset(&f,0,sizeof f);assert(zizhan_flow_init(&f,base,7,3,2880,480,0,30000000,0));f.next_token=0xfff0;
 uint64_t now=0,expect=0;unsigned oldest=0;
 for(unsigned i=0;i<1000;++i){
  int s;while((s=zizhan_flow_claim(&f,&frame))>=0){assert(frame==expect);expect+=480;n=zizhan_flow_submit(&f,(unsigned)s,now,tx);assert(n);assert((sent_token(tx,n)&0xffff0000u)==ZizhanFlowTokenBase);}
  assert(f.inflight==3);now+=10000;assert(done(&f,oldest,now)==1);oldest=(oldest+1)%4;zizhan_flow_tick(&f,now);assert(f.phase==ZizhanFlowRunning);
 }
 zizhan_flow_stop(&f);while(f.inflight){assert(done(&f,oldest,now)==1);oldest=(oldest+1)%4;}
 assert(zizhan_flow_returned(&f)&&f.completed==f.submitted&&f.completed==1002&&f.confirmed_frames==1002ull*480);
 puts("PASS zizhan flow: in-flight cap, out-of-order DONE, stale-token reuse guard, bad payload/status, 100 ms timeout, limit drain, 1002 periods x3 in flight");
 return 0;
}
