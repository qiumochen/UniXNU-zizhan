/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_RPMH_RECEIPT_H
#define ZIZHAN_RPMH_RECEIPT_H
#include <stdint.h>
namespace zizhan_rpmh {
/* Software provenance only: last request with a real completion seen by
 * this boot's shared active owner. This is NOT an inherited APSS baseline,
 * AOP aggregate vote,hardware readback or permission to start SLPI. All
 * access stays under powerLock; failed/unknown operations invalidate the
 * journal until reset. Exhaustion refuses observations without eviction. */
struct CompletedRequest {uint32_t address,value;uint64_t sequence;};
struct CompletionJournal {
 CompletedRequest entries[64]={};unsigned count=0;uint64_t sequence=0;bool invalid=false;
 void fail(){invalid=true;}
 bool record(const uint32_t *addresses,const uint32_t *values,unsigned n){
  if(invalid||!addresses||!values||!n||n>16||sequence==~uint64_t(0)){invalid=true;return false;}
  unsigned fresh=0;
  for(unsigned i=0;i<n;++i){
   if(!addresses[i]||(addresses[i]&3)){invalid=true;return false;}
   for(unsigned j=0;j<i;++j)if(addresses[i]==addresses[j]){invalid=true;return false;}
   bool found=false;for(unsigned j=0;j<count;++j)found|=entries[j].address==addresses[i];
   if(!found)++fresh;
  }
  if(fresh>64-count){invalid=true;return false;}
  const uint64_t next=++sequence;
  for(unsigned i=0;i<n;++i){unsigned at=0;while(at<count&&entries[at].address!=addresses[i])++at;
   if(at==count){++count;}entries[at]={addresses[i],values[i],next};}
  return true;
 }
 bool get(uint32_t address,CompletedRequest *out)const{
  if(!out){return false;}*out={};if(invalid||!address||(address&3))return false;
  for(unsigned i=0;i<count;++i)if(entries[i].address==address){*out=entries[i];return true;}
  return false;
 }
};
}
// Read-only software observation; unknown is false with a zeroed output.
// Never use as ZizhanPowerOwner.baseline without a separately qualified
// resource-ownership handoff that accounts for pre-kernel firmware votes.
extern "C" bool pong_rpmh_completed_request(uint32_t,zizhan_rpmh::CompletedRequest *);
#endif
