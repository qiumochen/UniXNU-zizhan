#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ZizhanSlpiTransport.h"
extern "C" {
#include "QcomSmem.h"
#include "QcomSmp2p.h"
}
using namespace zizhan_slpi_transport;
static std::vector<uint8_t> memory(Bytes);
static QcomSmem smem;static QcomSmemPartition partition;
static void put(unsigned at,uint32_t v){memcpy(memory.data()+at,&v,4);}
static uint32_t val(unsigned at){uint32_t v;memcpy(&v,memory.data()+at,4);return v;}
static unsigned offset(unsigned id){uint32_t at=0,n=0;assert(!qcom_smem_find(&smem,&partition,id,&at,&n));return at;}
static void setup(bool items=true){
 memory.assign(Bytes,0);smem={memory.data(),Bytes};const unsigned toc=Bytes-4096;
 put(toc,0x434f5424);put(toc+4,1);put(toc+8,1);
 put(toc+32,0x10000);put(toc+36,0x20000);put(toc+44,0x30000);put(toc+48,32);
 put(0x10000,0x54525024);put(0x10004,0x30000);put(0x10008,0x20000);put(0x1000c,32);put(0x10010,0x20000);
 assert(!qcom_smem_partition(&smem,0,3,&partition));if(!items)return;
 for(unsigned i=0;i<5;++i)assert(!qcom_smem_alloc(&smem,&partition,ItemIds[i],i<2?340u:i==2?32u:16384u));
 QcomSmp2p in,out;qcom_smp2p_init_out(&in,reinterpret_cast<volatile uint32_t *>(memory.data()+offset(481)),3,0);
 int n=qcom_smp2p_add_entry(&in,"slave-kernel");assert(n==0);qcom_smp2p_update(&in,n,~0u,6);
 qcom_smp2p_init_out(&out,reinterpret_cast<volatile uint32_t *>(memory.data()+offset(430)),0,3);assert(qcom_smp2p_add_entry(&out,"master-kernel")==0);
 // Payload must never be read by the snapshot; only FIFO metadata is used.
 memset(memory.data()+offset(479),0xff,16384);memset(memory.data()+offset(480),0xee,16384);
}
int main(){
 unsigned good_reads=0;
 for(unsigned test=0;test<18;++test){setup(test!=3);Receipt r;uint64_t time=0;unsigned reads=0,watched=0;const unsigned in=test==3?0:offset(481),tx=test==3?0:offset(479),rx=test==3?0:offset(480);
  if(test==4){put(Bytes-4096,0);}if(test==5)put(Bytes-4096+8,85);
  if(test==6)put(Bytes-4096+44,0x20000); // ADSP partition is not SLPI.
  if(test==7){put(Bytes-4096+8,2);memcpy(memory.data()+Bytes-4096+80,memory.data()+Bytes-4096+32,48);}
  if(test==8)put(0x10020,0); // Item canary.
  if(test==9)put(0x10024,0xffffffff);
  if(test==10)put(in-8,0xffff); // padding_data greater than size.
  if(test==11)put(in-12,4); // Truncated SMP2P item, preserve allocation extent via padding.
  if(test==12)put(in+8,2); // Captured,invalid header;not remote3 readiness.
  if(test==13)put(offset(478),16384); // Invalid FIFO tail.
  auto original=memory;
  auto result=capture(test==1?0u:1u,test==2?0u:1u,[&](uint32_t at,uint32_t *v){
   assert(at%4==0&&at<Bytes);++reads;if(test==14&&reads==37)return false;
   // Reading FIFO payload is forbidden, even in successful tests.
   if(test!=3)for(unsigned begin:{tx,rx})assert(at<begin||at>=begin+16384);
   *v=val(at);if(test==15&&at==in+36&&++watched==2)*v^=1;return true;
  },[&](){time+=test==16?1000u:0u;return time;},r);
  assert(memory==original&&reads==r.reads&&reads<=8192);
  if(test==1)assert(result==Off&&!reads&&!r.copies);
  else if(test==2)assert(result==Argument&&!reads&&!r.copies);
  else if(test==6)assert(result==NoPartition&&!r.copies);
  else if(test==14)assert(result==ReadFailed&&!r.copies);
  else if(test==15)assert(result==Changed&&r.copies==2&&!r.snapshot.offset);
  else if(test==16)assert(result==Deadline&&reads<250);
  else if((test>=4&&test<=11)||test==13)assert(result==Corrupt&&!r.snapshot.offset);
  else{assert(result==Ok&&r.copies==2&&r.snapshot.offset==0x10000&&r.snapshot.bytes==0x20000);
   if(test==3)assert(!r.snapshot.present&&!r.snapshot.slave_found);
   else if(test==12)assert(!r.snapshot.smp2p_valid[0]&&!r.snapshot.slave_found);
   else{good_reads=reads;assert(r.snapshot.present==31&&r.snapshot.smp2p_valid[0]&&r.snapshot.smp2p_valid[1]&&r.snapshot.slave_found&&r.snapshot.slave_value==6);
    for(unsigned i=0;i<5;++i){uint32_t at=0,bytes=0;assert(!qcom_smem_find(&smem,&partition,ItemIds[i],&at,&bytes));assert(r.snapshot.items[i].offset==at&&r.snapshot.items[i].bytes==bytes);}}
  }
 }
 assert(good_reads>300&&good_reads<1024);
 puts("PASS production readonlySLPI snapshot18controls/nativeSMEMallocation+SMP2P crosscheck/stabletwo-pass/foreignpartition/partial/time/readfailure/noFIFOpayload/no mutating handshakes;no hardware");
}
