#!/usr/bin/env python3
"""Execute the production SLPI inventory adapter with bounded host fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
DRIVERS = ROOT / "drivers/PongPower"
source = (DRIVERS / "PongAdsp.cpp").read_text()
begin = source.index("void slpiTransportObservation() {")
end = source.index("// ---- end zizhan SLPI inventory ----", begin)
adapter = source[begin:end].replace('__asm__ volatile("dmb oshld" ::: "memory");', '')
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include "ZizhanSlpiTransport.h"
extern "C" {
#include "QcomSmem.h"
}
static uint8_t bytes[zizhan_slpi_transport::Bytes];
static QcomSmem gSmem;
static unsigned mode=1,reads=0,locks=0,unlocks=0,releases=0;
static bool has_arg=true,has_node=true,has_property=true,bad_board=false,bad_size=false,lock_ok=true;
static std::vector<std::string> logs;
static int gIODTPlane;
struct OSData {
 unsigned getLength() {return bad_size?3u:unsigned(strlen("st,fts-zizhan-pri")+1);}
 const void *getBytesNoCopy() {return bad_board?"st,fts-zizhan-alt":"st,fts-zizhan-pri";}
};
struct IORegistryEntry {
 static IORegistryEntry *fromPath(const char *p,int) {
  assert(!strcmp(p,"/arm-io/pong-touch"));static IORegistryEntry node;
  return has_node?&node:nullptr;
 }
 OSData *getProperty(const char *p) {
  assert(!strcmp(p,"compatible"));static OSData data;return has_property?&data:nullptr;
 }
 void release() {++releases;}
};
#define OSDynamicCast(type,value) static_cast<type*>(value)
static bool PE_parse_boot_argn(const char *p,void *out,unsigned n) {
 assert(!strcmp(p,"zizhan_slpi_transport")&&n==sizeof mode);
 memcpy(out,&mode,n);return has_arg;
}
static bool lockSmem() {++locks;return lock_ok;}
static void unlockSmem() {++unlocks;}
static uint64_t mach_absolute_time() {return 0;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out) {*out=value;}
static void IOLog(const char *format,...) {
 char text[256];va_list args;va_start(args,format);const int length=vsnprintf(text,sizeof text,format,args);va_end(args);
 assert(length>=0&&unsigned(length)<sizeof text);
 logs.emplace_back(text);
}
static uint32_t observed_read(const QcomSmem *s,uint32_t at) {
 ++reads;assert(locks==1&&!unlocks&&at<sizeof bytes);
 return qcom_smem_read32(s,at);
}
#define qcom_smem_read32 observed_read
/* ACTUAL_ADAPTER */
#undef qcom_smem_read32
static void put(unsigned at,uint32_t value) {memcpy(bytes+at,&value,4);}
int main(int argc,char **argv) {
 assert(argc==2);const std::string test=argv[1];
 gSmem={bytes,sizeof bytes};const unsigned toc=sizeof bytes-4096;
 put(toc,0x434f5424);put(toc+4,1);put(toc+8,1);
 put(toc+32,0x10000);put(toc+36,0x20000);put(toc+44,0x30000);put(toc+48,32);
 put(0x10000,0x54525024);put(0x10004,0x30000);put(0x10008,0x20000);
 put(0x1000c,32);put(0x10010,0x20000);
 if(test=="absent")has_arg=false;
 else if(test=="zero")mode=0;
 else if(test=="unsupported")mode=2;
 else if(test=="node")has_node=false;
 else if(test=="property")has_property=false;
 else if(test=="board")bad_board=true;
 else if(test=="identity-size")bad_size=true;
 else if(test=="region-size")--gSmem.size;
 else if(test=="region-base")gSmem.base=nullptr;
 else if(test=="mutex")lock_ok=false;
 else if(test=="corrupt")put(toc,0);
 else if(test=="foreign")put(toc+44,0x20000);
 else assert(test=="empty");
 std::vector<uint8_t> before(bytes,bytes+sizeof bytes);
 slpiTransportObservation();assert(!memcmp(before.data(),bytes,sizeof bytes));
 if(test=="absent"||test=="zero"||test=="unsupported")
  assert(logs.empty()&&!locks&&!reads&&!releases);
 else if(test=="node"||test=="property"||test=="board"||test=="identity-size"||test=="region-size"||test=="region-base")
  assert(!locks&&!reads&&!unlocks&&logs.size()==1&&logs[0].find("SKIP board-or-region")!=std::string::npos);
 else if(test=="mutex")
  assert(locks==1&&!reads&&!unlocks&&logs.size()==1&&logs[0].find("SKIP shared-mutex-timeout")!=std::string::npos);
 else {
  assert(locks==1&&unlocks==1&&reads&&reads<=8192&&releases==1);
  const auto &last=logs.back();
  assert(last.find("same_adsp_actor=1 shared_mutex=1 finished=1")!=std::string::npos);
  assert(logs[logs.size()-2].find("protocol_writes=0 votes=0 pas=0 slpi_start=0 retained_unqualified=1")!=std::string::npos);
  if(test=="corrupt")assert(logs.size()==2&&last.find("FAIL result=4")!=std::string::npos);
  else if(test=="foreign")assert(logs.size()==2&&last.find("SKIP result=3")!=std::string::npos);
  else assert(logs.size()==7&&last.find("PASS result=0 copies=2")!=std::string::npos&&last.find("present=0")!=std::string::npos);
 }
}
'''

with tempfile.TemporaryDirectory(prefix="zizhan-slpi-platform-") as directory:
    directory = Path(directory)
    common = ["-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
              "-I" + str(DRIVERS)]
    obj = directory / "smem.o"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", *common,
                    "-c", str(DRIVERS / "QcomSmem.c"), "-o", str(obj)],
                   check=True, timeout=30)
    path = directory / "platform.cpp"
    path.write_text(harness.replace("/* ACTUAL_ADAPTER */", adapter))
    binary = directory / "platform"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", *common,
                    str(path), str(obj), "-o", str(binary)], check=True, timeout=30)
    for case in ("absent", "zero", "unsupported", "node", "property", "board",
                 "identity-size", "region-size", "region-base", "mutex",
                 "corrupt", "foreign", "empty"):
        subprocess.run([str(binary), case], check=True, timeout=10)
print("PASS actual SLPI adapter: 13 gate/mutex/readonly/failure controls; no hardware or SLPI startup")
