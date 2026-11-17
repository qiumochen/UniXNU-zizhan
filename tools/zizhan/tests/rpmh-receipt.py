#!/usr/bin/env python3
"""Actual completion journal; no MMIO or inherited-baseline claim."""
from pathlib import Path
import subprocess,tempfile,os
R=Path(__file__).resolve().parents[3]
source=r'''
#include <cassert>
#include "ZizhanRpmhReceipt.h"
using namespace zizhan_rpmh;
int main(){
 CompletionJournal j;CompletedRequest out={1,2,3};
 assert(!j.get(0x100,&out)&&!out.address&&!out.value&&!out.sequence);
 uint32_t a[]={0x100,0x104},v[]={0,0xffffffff};
 assert(j.record(a,v,2)&&j.count==2&&j.sequence==1);
 assert(j.get(a[0],&out)&&out.value==0&&out.sequence==1); // Real ACK0 distinct from unknown.
 assert(j.get(a[1],&out)&&out.value==0xffffffff&&out.sequence==1);
 v[0]=7;assert(j.record(a,v,1)&&j.count==2&&j.get(a[0],&out)&&out.value==7&&out.sequence==2);
 assert(j.get(a[1],&out)&&out.sequence==1);
 assert(!j.get(0x108,&out)&&!out.address&&!out.sequence&&!out.value);
 assert(!j.get(0x101,&out)&&!j.get(0x100,nullptr));
 j.fail();assert(!j.get(0x100,&out)&&!out.address&&!j.record(a,v,1));
 for(unsigned bad=0;bad<5;++bad){CompletionJournal k;uint32_t x[]={0x100,0x104},y[]={1,2};
  if(bad==0){x[0]=0;}if(bad==1)x[0]=0x101;if(bad==2)x[1]=x[0];
  assert(!k.record(bad==3?nullptr:x,bad==4?nullptr:y,2)&&k.invalid&&!k.count&&!k.sequence);
 }
 CompletionJournal full;
 for(unsigned i=0;i<64;++i){uint32_t address=0x100+i*4,value=i;assert(full.record(&address,&value,1));}
 assert(full.count==64);uint32_t replace=0x100,value=999;assert(full.record(&replace,&value,1)&&full.count==64);
 uint32_t overflow=0x300;assert(!full.record(&overflow,&value,1)&&full.invalid&&full.count==64&&!full.get(replace,&out)&&!out.address);
 CompletionJournal atomic;for(unsigned i=0;i<63;++i){uint32_t address=0x100+i*4;assert(atomic.record(&address,&i,1));}
 uint32_t x[]={0x300,0x304};assert(!atomic.record(x,v,2)&&atomic.count==63&&atomic.sequence==63);
 CompletionJournal exhausted;exhausted.sequence=~uint64_t(0);assert(!exhausted.record(a,v,1)&&exhausted.invalid);
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-rpmh-receipt-') as temp:
 p=Path(temp)/'journal.cpp';p.write_text(source);binary=Path(temp)/'journal'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(p),'-o',str(binary)],check=True,timeout=30)
 subprocess.run([str(binary)],check=True,timeout=10)
print('PASS actual completed-request journal/ACKzero-vs-unknown/atomicgroup/update/capacity/noeviction/overflow/failurepermanent-refusal;not inheritedbaseline/hardware')
