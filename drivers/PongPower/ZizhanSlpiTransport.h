/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SLPI_TRANSPORT_H
#define ZIZHAN_SLPI_TRANSPORT_H
#include <stdint.h>
#include <string.h>
namespace zizhan_slpi_transport {
// Saved zizhan DT: SMEM0x80900000/2MiB, APSS0/SLPI3,
// inbound481/outbound430, GLINK descriptor478/TX479/RX480.
// Inventory only. No allocation,outbound initialization,head/tail changes,
// doorbell,SSR ACK,PAS or firmware start. Retained bits never mean ready.
constexpr uint32_t Bytes=0x200000,ItemIds[5]={481,430,478,479,480};
enum Result {Ok,Off,Argument,NoPartition,Corrupt,ReadFailed,Deadline,Changed,Budget};
struct Item{uint32_t offset=0,bytes=0,present=0;};
struct Snapshot{
 uint32_t offset=0,bytes=0,cacheline=0,free_uncached=0,free_cached=0,present=0;
 Item items[5]={};uint32_t smp2p[2][85]={},descriptor[8]={};
 unsigned smp2p_valid[2]={},slave_found=0;uint32_t slave_value=0;
};
struct Receipt{unsigned result=Off,reads=0,copies=0;Snapshot snapshot;};
template<class Read,class Clock> Result capture(unsigned enabled,unsigned board,Read read,Clock clock,Receipt &out){
 out={};if(enabled!=1)return Off;if(board!=1){out.result=Argument;return Argument;}
 const uint64_t begin=clock();Result error=Ok;
 auto get=[&](uint32_t at,uint32_t &value){
  if(error!=Ok)return false;
  if((at&3)||at>Bytes-4){error=Corrupt;return false;}
  if(out.reads>=8192){error=Budget;return false;}
  if(clock()-begin>=250000){error=Deadline;return false;}
  ++out.reads;if(!read(at,&value)){error=ReadFailed;return false;}return true;
 };
 Snapshot passes[2];
 for(unsigned pass=0;pass<2;++pass){Snapshot &s=passes[pass];uint32_t magic=0,version=0,count=0;
  const uint32_t toc=Bytes-0x1000;
  if(!get(toc,magic)||!get(toc+4,version)||!get(toc+8,count))break;
  if(magic!=0x434f5424||version!=1||count>84){error=Corrupt;break;}
  unsigned matches=0;
  for(unsigned i=0;i<count;++i){const uint32_t e=toc+32+i*48;uint32_t at=0,bytes=0,hosts=0,line=0;
   if(!get(e,at)||!get(e+4,bytes)||!get(e+12,hosts)||!get(e+16,line))break;
   if(!at||!bytes){continue;}if(hosts!=0x00030000&&hosts!=3)continue;
   if(++matches>1||at&7||at>toc||bytes>toc-at||bytes<64||line>4096||(line&(line-1))){error=Corrupt;break;}
   uint32_t pmagic=0,phosts=0,pbytes=0;
   if(!get(at,pmagic)||!get(at+4,phosts)||!get(at+8,pbytes)||!get(at+12,s.free_uncached)||!get(at+16,s.free_cached))break;
   if(pmagic!=0x54525024||phosts!=hosts||pbytes!=bytes||s.free_uncached<32||s.free_uncached>s.free_cached||s.free_cached>bytes){error=Corrupt;break;}
   s.offset=at;s.bytes=bytes;s.cacheline=line?line:8u;
  }
  if(error!=Ok){break;}if(!matches){error=NoPartition;break;}
  auto item=[&](uint32_t id,uint32_t at,uint32_t bytes){
   if(!bytes||at<s.offset+32||at>s.offset+s.bytes||bytes>s.offset+s.bytes-at){error=Corrupt;return false;}
   for(unsigned n=0;n<5;++n)if(id==ItemIds[n]){
    if(s.items[n].present){error=Corrupt;return false;}
    s.items[n]={at,bytes,1};s.present|=1u<<n;
   }
   return true;
  };
  uint32_t at=32;unsigned entries=0;
  while(at<s.free_uncached&&error==Ok){
   if(entries++>=256||s.free_uncached-at<16){error=Corrupt;break;}
   uint32_t key=0,size=0,pads=0;
   if(!get(s.offset+at,key)||!get(s.offset+at+4,size)||!get(s.offset+at+8,pads))break;
   const uint32_t header=pads>>16,data=pads&0xffff;
   const uint64_t next=uint64_t(at)+16+header+size;
   if((key&0xffff)!=0xa5a5||next>s.free_uncached||data>size){error=Corrupt;break;}
   if(!item(key>>16,s.offset+at+16+header,size-data))break;
   at=uint32_t(next);
  }
  if(error!=Ok)break;
  const uint32_t header=(16+s.cacheline-1)&~(s.cacheline-1);
  if(header>s.bytes){error=Corrupt;break;}at=s.bytes-header;entries=0;
  while(at>s.free_cached&&error==Ok){
   if(entries++>=256||s.bytes-at<16){error=Corrupt;break;}
   uint32_t key=0,size=0,pads=0;
   if(!get(s.offset+at,key)||!get(s.offset+at+4,size)||!get(s.offset+at+8,pads))break;
   const uint32_t padding=pads&0xffff;
   if((key&0xffff)!=0xa5a5||size>at||padding>size||at-size<s.free_cached){error=Corrupt;break;}
   if(!item(key>>16,s.offset+at-size,size-padding))break;
   if(uint64_t(size)+header>at){error=Corrupt;break;}
   at-=size+header;
  }
  if(error!=Ok)break;
  for(unsigned n=0;n<2&&error==Ok;++n)if(s.items[n].present){
   if(s.items[n].bytes<340){error=Corrupt;break;}
   for(unsigned i=0;i<85;++i)if(!get(s.items[n].offset+i*4,s.smp2p[n][i]))break;
   if(error!=Ok){break;}const uint32_t *words=s.smp2p[n];const unsigned total=words[3]&0xffff,valid=words[3]>>16;
   s.smp2p_valid[n]=words[0]==0x504d5324&&(words[1]&0xff)==1&&words[2]==(n?0x30000u:3u)&&total<=16&&valid<=total;
   if(!n&&s.smp2p_valid[0])for(unsigned i=0;i<valid;++i){
    const char expected[16]="slave-kernel";uint32_t name[4]={};memcpy(name,expected,16);
    if(!memcmp(words+5+i*5,name,16)){if(s.slave_found){error=Corrupt;break;}s.slave_found=1;s.slave_value=words[9+i*5];}
   }
  }
  if(error!=Ok)break;
  if(s.items[2].present){
   if(s.items[2].bytes!=32){error=Corrupt;break;}
   for(unsigned i=0;i<8;++i)if(!get(s.items[2].offset+i*4,s.descriptor[i]))break;
   for(unsigned n=0;n<2&&error==Ok;++n)if(s.items[3+n].present){
    const uint32_t length=s.items[3+n].bytes;
    if(length<16||(length&7)||s.descriptor[n*2]>=length||s.descriptor[n*2+1]>=length||
       (s.descriptor[n*2]&7)||(s.descriptor[n*2+1]&7)){error=Corrupt;break;}
   }
  }
  if(error!=Ok){break;}++out.copies;
 }
 if(error==Ok&&memcmp(&passes[0],&passes[1],sizeof(Snapshot)))error=Changed;
 out.result=error;
 if(error==Ok)out.snapshot=passes[0]; // Partial/untrusted data never published as a complete inventory.
 return error;
}
}
#endif
