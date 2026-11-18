#include "ZizhanDisplayPower.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond,name) do{ \
 if(cond)printf("PASS %s\n",name); \
 else{printf("FAIL %s\n",name);++failures;} \
}while(0)

static ZizhanDisplayReceipt req(ZizhanDisplayPower *p,uint32_t count,const uint64_t *in,
                                size_t bytes,uint64_t t){
 return zizhan_display_request(p,count,in,bytes,t);
}

static int done(ZizhanDisplayPower *p,uint32_t seq,int ok,ZizhanDisplayReceipt *r){
 return zizhan_display_complete(p,seq,ok,r);
}

static void test_malformed(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOff);
 ZizhanDisplayReceipt r=req(&p,0,0,0,1);
 CHECK(r.result==ZizhanDisplayRejectedMalformed,"malformed zero count");
 CHECK(p.queued==0&&p.state==ZizhanDisplayStateOff,"malformed does not queue");
 CHECK(p.rejected_malformed==1&&p.last_failure==ZizhanDisplayFailureMalformed,"malformed counted");
 r=req(&p,2,0,0,2);
 CHECK(r.result==ZizhanDisplayRejectedMalformed,"malformed null scalars");
 uint64_t many[9]={0};
 r=req(&p,9,many,0,3);
 CHECK(r.result==ZizhanDisplayRejectedMalformed,"malformed too many scalars");
 CHECK(p.rejected_malformed==3,"malformed count total");
 CHECK(p.rejected==3,"malformed rejected total");
}

static void test_classify(void){
 ZizhanDisplayPower p;
 zizhan_display_init(&p,ZizhanDisplayStateOff);
 uint64_t one=1,zero=0,two=2;
 ZizhanDisplayReceipt r=req(&p,1,&one,0,1);
 CHECK(r.result==ZizhanDisplayAccepted&&r.target==ZizhanDisplayTargetOn,"classify 1 on");
 r=req(&p,1,&one,0,2);
 CHECK(r.result==ZizhanDisplayNoOp,"duplicate on no-op");
 r=req(&p,1,&two,0,3);
 CHECK(r.result==ZizhanDisplayRejectedUnknownTarget,"classify 2 unknown");
 CHECK(p.rejected_unknown==1,"unknown counted");
 zizhan_display_init(&p,ZizhanDisplayStateOff);
 r=req(&p,1,&zero,0,4);
 CHECK(r.result==ZizhanDisplayNoOp&&r.target==ZizhanDisplayTargetOff,"classify 0 off no-op");
}

static void test_noop_and_accept(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOn);
 uint64_t one=1,zero=0;
 ZizhanDisplayReceipt r=req(&p,1,&one,0,1);
 CHECK(r.result==ZizhanDisplayNoOp&&!r.has_sequence,"already on no-op");
 CHECK(p.queued==0&&p.state==ZizhanDisplayStateOn,"no-op keeps state");
 r=req(&p,1,&zero,16,2);
 CHECK(r.result==ZizhanDisplayAccepted&&r.has_sequence,"off accepted");
 CHECK(r.before==ZizhanDisplayStateOn&&r.after==ZizhanDisplayStateTransitioning,"receipt before/after");
 CHECK(r.queue_depth==1&&r.structure_bytes==16,"receipt queue/size");
 CHECK(p.state==ZizhanDisplayStateTransitioning&&p.stable==ZizhanDisplayStateOn,"transitioning from on");
 r=req(&p,1,&zero,0,3);
 CHECK(r.result==ZizhanDisplayNoOp&&p.queued==1,"duplicate pending off no-op");
}

static void test_queue_full(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOff);
 uint64_t vals[2]={1,0};
 for(unsigned i=0;i<4;++i){
  ZizhanDisplayReceipt r=req(&p,1,&vals[i&1u],0,100+i);
  CHECK(r.result==ZizhanDisplayAccepted,"queue accept");
 }
 CHECK(p.queued==4,"queue holds four");
 ZizhanDisplayReceipt r=req(&p,1,&vals[0],0,200);
 CHECK(r.result==ZizhanDisplayRejectedQueueFull,"queue full rejects");
 CHECK(p.rejected_queue_full==1&&p.last_failure==ZizhanDisplayFailureQueueFull,"queue full counted");
 CHECK(p.queued==4&&p.queue[0].target==ZizhanDisplayTargetOn&&
       p.queue[3].target==ZizhanDisplayTargetOff,"queue not overwritten");
}

static void test_complete_order(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOff);
 uint64_t one=1,zero=0;
 uint32_t s[4];
 s[0]=req(&p,1,&one,0,1).sequence;
 s[1]=req(&p,1,&zero,0,2).sequence;
 s[2]=req(&p,1,&one,0,3).sequence;
 s[3]=req(&p,1,&zero,0,4).sequence;
 CHECK(p.queued==4,"four sequences queued");
 ZizhanDisplayReceipt r;
 CHECK(done(&p,s[1],1,&r)==1,"out-of-order complete found");
 CHECK(r.result==ZizhanDisplayAccepted&&r.target==ZizhanDisplayTargetOff,"complete receipt target");
 CHECK(r.before==ZizhanDisplayStateOff&&r.after==ZizhanDisplayStateTransitioning,"complete receipt state");
 CHECK(p.queued==3&&p.stable==ZizhanDisplayStateOff,"out-of-order keeps stable off");
 CHECK(done(&p,s[1],1,&r)==0,"duplicate complete rejected");
 CHECK(p.completion_rejected==1&&p.last_failure==ZizhanDisplayFailureCompletionRejected,"duplicate complete counted");
 CHECK(p.state==ZizhanDisplayStateTransitioning&&p.queued==3,"duplicate complete keeps state");
 CHECK(done(&p,0xdeadbeefu,1,&r)==0,"unknown complete rejected");
 CHECK(p.completion_rejected==2,"unknown complete counted");
 CHECK(done(&p,s[0],1,&r)==1&&p.stable==ZizhanDisplayStateOn,"first complete sets on");
 CHECK(done(&p,s[2],1,&r)==1&&p.stable==ZizhanDisplayStateOn,"third complete keeps on");
 CHECK(done(&p,s[3],1,&r)==1&&p.stable==ZizhanDisplayStateOff&&p.state==ZizhanDisplayStateOff,"last complete drains to off");
 CHECK(p.queued==0&&p.completed_ok==4,"all complete ok");
}

static void test_failure_holds(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOn);
 uint64_t zero=0;
 uint32_t s=req(&p,1,&zero,0,1).sequence;
 ZizhanDisplayReceipt r;
 CHECK(done(&p,s,0,&r)==1,"failed completion found");
 CHECK(p.state==ZizhanDisplayStateOn&&p.stable==ZizhanDisplayStateOn,"failure keeps stable on");
 CHECK(p.queued==0&&p.completed_fail==1,"failure drains without retry");
 CHECK(r.ok==0&&r.failure==ZizhanDisplayFailureCompletionFailed,"failure receipt");
 CHECK(p.last_failure==ZizhanDisplayFailureCompletionFailed&&p.last_failure_sequence==s,"failure evidence kept");
 CHECK(p.accepted==1&&p.completed_ok==0,"failure does not fabricate success");
}

static void test_failure_with_followup(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOn);
 uint64_t zero=0,one=1;
 uint32_t a=req(&p,1,&zero,0,1).sequence;
 uint32_t b=req(&p,1,&one,0,2).sequence;
 ZizhanDisplayReceipt r;
 CHECK(done(&p,a,0,&r)==1,"followup first fail");
 CHECK(p.state==ZizhanDisplayStateTransitioning&&p.stable==ZizhanDisplayStateOn,"followup remains transitioning");
 CHECK(done(&p,b,1,&r)==1,"followup second ok");
 CHECK(p.state==ZizhanDisplayStateOn&&p.stable==ZizhanDisplayStateOn,"followup drains to on");
 CHECK(p.completed_fail==1&&p.completed_ok==1,"followup counters");
}

static void test_sequence_wrap(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOn);
 uint64_t zero=0,one=1;
 p.next_sequence=UINT32_MAX;
 ZizhanDisplayReceipt r=req(&p,1,&zero,0,1);
 CHECK(r.has_sequence&&r.sequence==0,"sequence wraps to zero");
 ZizhanDisplayReceipt r2=req(&p,1,&one,0,2);
 CHECK(r2.has_sequence&&r2.sequence==1&&r2.sequence!=r.sequence,"sequence continues after wrap");
 ZizhanDisplayReceipt c;
 CHECK(done(&p,r.sequence,1,&c)==1,"complete wrapped zero");
 CHECK(done(&p,r2.sequence,1,&c)==1,"complete wrapped one");
 CHECK(p.queued==0&&p.state==ZizhanDisplayStateOn,"wrap completes cleanly");
}

static void test_sequence_collision(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOff);
 uint64_t one=1,zero=0;
 ZizhanDisplayReceipt a=req(&p,1,&one,0,1);
 uint32_t held=a.sequence;
 p.next_sequence=held-1u; /* next ++ would collide with the active request */
 ZizhanDisplayReceipt b=req(&p,1,&zero,0,2);
 CHECK(b.has_sequence&&b.sequence!=held,"sequence skips active value");
 ZizhanDisplayReceipt r;
 CHECK(done(&p,a.sequence,1,&r)==1,"complete first after collision skip");
 CHECK(done(&p,b.sequence,1,&r)==1,"complete second after collision skip");
}

static void test_events_bounded(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOff);
 uint64_t one=1;
 for(unsigned i=0;i<12;++i){
  ZizhanDisplayReceipt r=req(&p,1,&one,0,100+i);
  if(r.result==ZizhanDisplayAccepted)done(&p,r.sequence,1,0);
 }
 CHECK(p.event_count==ZizhanDisplayEvents,"event ring bounded");
 CHECK(zizhan_display_last_event(&p,0)!=0,"latest event exists");
 CHECK(zizhan_display_last_event(&p,ZizhanDisplayEvents)==0,"event back out of range null");
 CHECK(p.requests==12,"request count includes all calls");
}

static void test_busy(void){
 ZizhanDisplayPower p;zizhan_display_init(&p,ZizhanDisplayStateOn);
 uint64_t zero=0;
 zizhan_display_set_busy(&p,1);
 ZizhanDisplayReceipt r=req(&p,1,&zero,0,1);
 CHECK(r.result==ZizhanDisplayRejectedBusy,"busy rejects");
 CHECK(p.queued==0&&p.rejected_busy==1&&p.last_failure==ZizhanDisplayFailureBusy,"busy counted");
 zizhan_display_set_busy(&p,0);
 r=req(&p,1,&zero,0,2);
 CHECK(r.result==ZizhanDisplayAccepted&&p.queued==1,"busy clear accepts");
}

int main(void){
 test_malformed();
 test_classify();
 test_noop_and_accept();
 test_queue_full();
 test_complete_order();
 test_failure_holds();
 test_failure_with_followup();
 test_sequence_wrap();
 test_sequence_collision();
 test_events_bounded();
 test_busy();
 printf("%s: %d failure(s)\n",failures?"FAIL":"PASS",failures);
 return failures?1:0;
}
