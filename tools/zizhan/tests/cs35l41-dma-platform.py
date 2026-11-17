#!/usr/bin/env python3
"""Actual DMA platform owner+shared programmer against modeled MMIO/kernel API."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
source=(R/'drivers/PongPower/ZizhanAudioDmaMap.cpp').read_text()
source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
# Replace only the volatile MMIO write seam with modeled W1C/failure semantics.
needle='*reinterpret_cast<volatile uint32_t *>(o.registers+at)=value;return barrier(p);'
assert source.count(needle)==1
source=source.replace(needle,'(void)o;if(model_write32(at,value))return 1;return barrier(p);')
needle64='*value=*reinterpret_cast<volatile uint64_t *>(o.registers+at);'
assert source.count(needle64)==1
source=source.replace(needle64,'(void)o;*value=model_read64(at);')
harness=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include "ZizhanAudioDmaMap.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4KAudit.h"
static uint32_t regs[0x100000/4];alignas(4096) static uint64_t table_words[8192];
static bool missing,lock_fail,map_fail,alloc_fail,table_bad,program_fail,restore_fail,sctlr_rao,cbar_bad,tcr2_bad,namespace_unstable;
static unsigned locks,unlocks,map_releases,table_releases,writes,chosen;
static uint64_t extent[2]={0xf0000000,65536};
static std::vector<std::string> logs;
constexpr unsigned kIOMapAnywhere=1,kIOMapInhibitCache=2;
constexpr unsigned kIODirectionInOut=1,kIOMemoryPhysicallyContiguous=2,kIOMemoryMapperNone=4;
using IOOptionBits=unsigned;using IOByteCount=uint64_t;static int kernel_task,gIODTPlane;
extern "C" bool pong_smmu_lease_lock(){++locks;return !lock_fail;}
extern "C" void pong_smmu_lease_unlock(){++unlocks;}
static int model_write32(size_t at,uint32_t value){++writes;if(program_fail||restore_fail)return 1;if((at&4095)==0x58)regs[at/4]=(regs[at/4]&~(value&0xc00001feu))|0x400;else regs[at/4]=value;if(!(at&4095)&&at>=0x80000)regs[at/4]&=~0x1000u;if((at&4095)==0x10)regs[at/4]|=0x60;if(tcr2_bad&&at==(128+74)*4096+0x10)regs[at/4]^=0x100;if(sctlr_rao&&at==(128+74)*4096)regs[at/4]|=0x20000000;if(cbar_bad&&at==0x1000+74*4)regs[at/4]^=0x100;return 0;}
static uint64_t model_read64(size_t at){uint64_t value=*reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(regs)+at);if(namespace_unstable&&at==(128+1)*4096+0x20)regs[(128+1)*4096/4]^=1;return value;}
static void IOLog(const char *fmt,...){char b[512];va_list a;va_start(a,fmt);vsnprintf(b,sizeof b,fmt,a);va_end(a);logs.emplace_back(b);}
using wait_result_t=int;using thread_t=void *;using thread_continue_t=void (*)(void *,wait_result_t);enum{KERN_SUCCESS=0};
static void IOSleep(unsigned){}static int kernel_thread_start(thread_continue_t,void *,thread_t *){return 1;}static void thread_deallocate(thread_t){}
static bool PE_parse_boot_argn(const char *,void *,size_t){return false;}
#ifdef __APPLE__
extern "C" __attribute__((weak)) int pong_klog_flush(int){return -1;} /* ld64 rejects undefined weak refs */
#else
extern "C" int pong_klog_flush(int) __attribute__((weak));
#endif
struct OSData {size_t getLength(){return missing?0:16;}const void *getBytesNoCopy(){return extent;}};
struct IORegistryEntry {static IORegistryEntry *fromPath(const char *p,int){assert(!strcmp(p,"/chosen"));++chosen;static IORegistryEntry e;return &e;}OSData *getProperty(const char *p){assert(!strcmp(p,"pong-audio-dma"));static OSData d;return &d;}void release(){}};
#define OSDynamicCast(type,v) static_cast<type *>(v)
struct IOMemoryMap {uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(regs);}void release(){++map_releases;}};
struct IODeviceMemory {static IODeviceMemory *withRange(uint64_t base,uint64_t bytes){assert(base==0x15000000&&bytes==0x100000&&locks==1&&!unlocks);static IODeviceMemory m;return &m;}IOMemoryMap *map(unsigned flags){assert(flags==(kIOMapAnywhere|kIOMapInhibitCache));static IOMemoryMap m;return map_fail?nullptr:&m;}void release(){}};
struct IOBufferMemoryDescriptor {
 static IOBufferMemoryDescriptor *inTaskWithOptions(int,unsigned flags,size_t bytes,size_t align){assert(flags==7&&bytes==65536&&align==4096);static IOBufferMemoryDescriptor m;return alloc_fail?nullptr:&m;}
 uint64_t getPhysicalSegment(unsigned at,IOByteCount *span,unsigned flags){assert(!at&&flags==4);*span=65536;return table_bad?extent[0]:0x700000000;}
 void *getBytesNoCopy(){return table_words;}void release(){++table_releases;}
};
/* SOURCE */
int main(int argc,char **argv){assert(argc==2);std::string test=argv[1];
 regs[0x20/4]=0x4c017e71;regs[0x24/4]=0x6000004b;regs[0x28/4]=0x5111;
 for(unsigned i=0;i<113;++i)regs[(0xc00+i*4)/4]=0x20000;
 regs[(128+1)*4096/4]=1;*reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(regs)+(128+1)*4096+0x20)=uint64_t(0x23)<<48;
 regs[(128+74)*4096/4]=0x2000230a;regs[((128+74)*4096+0x10)/4]=0x60;
 if(test=="missing")missing=true;else if(test=="lock")lock_fail=true;else if(test=="map")map_fail=true;else if(test=="alloc")alloc_fail=true;else if(test=="table")table_bad=true;else if(test=="program")program_fail=true;else if(test=="sctlr-rao")sctlr_rao=true;else if(test=="cbar-readback")cbar_bad=true;else if(test=="tcr2-readback")tcr2_bad=true;else if(test=="asid-lower"||test=="asid-upper")*reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(regs)+(128+1)*4096+(test=="asid-lower"?0x20:0x28))=uint64_t(0x1801)<<48;else if(test=="asid-unstable")namespace_unstable=true;else assert(test=="pass"||test=="restore");
 bool result=zizhan_audio_dma_map_begin();
 if(test=="pass"||test=="restore"){
  assert(owner.config.ignore_top_byte==1&&owner.config.shared_asid==1&&owner.active_banks==1&&owner.namespace_reads>148&&owner.state.programmed.readback.sctlr==0x27&&owner.state.programmed.fault_status==0x400&&owner.config.lower->input_bits==32&&!owner.config.upper&&owner.state.programmed.readback.tcr2==0x38071);
  assert(result&&owner.state.installed&&owner.audited.complete&&owner.audited.lower.leaf_pages==16&&locks==1&&!unlocks&&!map_releases&&!table_releases&&zizhan_audio_dma_map_address()==0x1f0000000);
  if(test=="restore")restore_fail=true;
  bool ended=zizhan_audio_dma_map_end();
  if(test=="pass")assert(ended&&owner.state.restored&&!owner.state.uncertain&&map_releases==1&&table_releases==1&&unlocks==1);
  else assert(!ended&&!owner.state.restored&&owner.state.uncertain&&!map_releases&&!table_releases&&unlocks==1);
 }else{
  assert(!result&&!zizhan_audio_dma_map_address());
  if(test=="sctlr-rao"||test=="cbar-readback"||test=="tcr2-readback"){assert(owner.state.program_status==ArmSmmu4KStateMismatch&&owner.state.attempted&&owner.state.uncertain&&!map_releases&&!table_releases&&owner.trace_count>10&&!owner.trace_overflow);assert(owner.state.programmed.readback.sctlr==(sctlr_rao?0x20000000u:0u));if(cbar_bad)assert(owner.state.programmed.readback.cbar==0x1f200);if(tcr2_bad)assert(owner.state.programmed.readback.tcr2==0x38171);bool prior=false,expected=false,actual=false,diag=false,trace=false;for(const auto &l:logs){prior|=l.find("kind=prior")!=std::string::npos;expected|=l.find("kind=expected")!=std::string::npos;actual|=l.find("kind=program-readback")!=std::string::npos;diag|=l.find("strict_policy=1")!=std::string::npos;trace|=l.find("DMAMAP_WRITE: index=0")!=std::string::npos;}assert(prior&&expected&&actual&&diag&&trace);}
  else if(test=="program")assert(owner.state.attempted&&owner.state.uncertain&&!map_releases&&!table_releases);
  else assert(!writes&&!owner.state.attempted);
  assert(unlocks==(test=="missing"||test=="lock"?0u:1u));
 }
 unsigned old_chosen=chosen,old_writes=writes;assert(!zizhan_audio_dma_map_begin()&&chosen==old_chosen&&writes==old_writes);
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-dma-platform-') as temp:
    d=Path(temp);cpp=d/'platform.cpp';cpp.write_text(harness.replace('/* SOURCE */',source));objects=[]
    for name in ('ArmSmmu4K','ArmSmmu4KContext','ArmSmmu4KProgram','ArmSmmu4KAudit'):
        obj=d/(name+'.o');subprocess.run([os.environ.get('CC','cc'),'-std=c11','-fsanitize=address,undefined','-c',str(R/'drivers/PongAdreno/ArmSmmu4K'/(name+'.c')),'-o',str(obj)],check=True,timeout=30);objects.append(str(obj))
    binary=d/'platform';subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(cpp),*objects,'-o',str(binary)],check=True,timeout=30)
    for case in ('pass','missing','lock','map','alloc','table','program','restore','sctlr-rao','cbar-readback','tcr2-readback','asid-lower','asid-upper','asid-unstable'):
        subprocess.run([str(binary),case],check=True,timeout=10)
print('PASS actual DMA platform/14controls/UEFIextent/globalowner/native4Kphysicalaudit/lifetimes/singleuse/uncertaintyretain;no hardware')
