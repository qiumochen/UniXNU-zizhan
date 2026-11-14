#!/usr/bin/env python3
"""Replay actual native framebuffer handoff control with bounded mock owners.

Extracts the actual generated pre-composition block, not a parallel state
model. No DPU/DSI/GPU hardware is simulated as completed; this checks that
real owner calls precede permitted CPU writes and non-completions refuse.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define XNU_PONG_ADRENO_PLATFORM 1
using IOReturn=int;
using IOLock=int;
constexpr int kIOReturnNotReady=-1;
#define SWAP_SURFACE(layer) (4*(layer))
#define SWAP_SOURCE(layer) (16+16*(layer))
#define SWAP_DEST(layer) (80+16*(layer))
struct IOMemoryDescriptor {};
struct Info {uint32_t width=4,height=4,bytesPerRow=16;};
static unsigned char memory[256];
struct MiniIOSurface {
 Info info;IOMemoryDescriptor *client=nullptr,*memory=nullptr;
 uint8_t *kpixels=::memory,*kbase=::memory;
 uint8_t *pixels(){return ::memory;}
};
static int frame_status,quarantine_at,queries,restores,frames,writes;
static char events[32];static unsigned event_count;
static void event(char value){assert(event_count<sizeof events);events[event_count++]=value;}
static void IOLockUnlock(IOLock *){event('U');}
static bool pong_scanout_quarantined(){event('Q');return ++queries==quarantine_at;}
static void pong_scanout_cpu_frame(){restores++;event('C');}
static int pong_scanout_frame(IOMemoryDescriptor *,size_t,uint32_t,uint32_t,uint32_t){frames++;event('F');return frame_status;}
static void beginGraphics(){event('B');}
static bool canDrawLinearBGRA(MiniIOSurface *){return true;}
static void IOLog(const char *,...){ }
static uint32_t swapWord(const uint8_t *swap,unsigned offset){uint32_t value;memcpy(&value,swap+offset,4);return value;}
static IOReturn actual_control(bool surfacesReady,bool fGpuOnly,uint8_t *swap) {
 IOLock *fLock=nullptr;unsigned fPanelWidth=4,fPanelHeight=4;
 MiniIOSurface surface;MiniIOSurface *surfaces[4]={&surface,nullptr,nullptr,nullptr};
'''

SUFFIX = r'''
 event(gpuFrame?'G':'W');if(!gpuFrame)writes++;return 0;
}
int main() {
 const struct {int status,quarantine;bool ready,gpu_only;int result,restore,frame,write;const char *events;} cases[]={
 {0,0,true,false,0,1,1,1,"QBFQCW"},
 {1,0,true,false,0,0,1,0,"QBFQG"},
 {2,0,true,false,-1,0,1,0,"QBFU"},
 {3,0,true,false,-1,0,1,0,"QBFU"},
 {99,0,true,false,-1,0,1,0,"QBFU"},
 {0,1,true,false,-1,0,0,0,"QU"},
 {0,2,true,false,-1,0,1,0,"QBFQU"},
 {0,0,false,false,0,1,0,1,"QQCW"},
 {0,0,true,true,-1,0,1,0,"QBFQU"},
 {1,0,true,true,0,0,1,0,"QBFQG"},
 };
 for(unsigned index=0;index<sizeof cases/sizeof cases[0];index++) {
  auto c=cases[index];frame_status=c.status;quarantine_at=c.quarantine;
  queries=restores=frames=writes=event_count=0;memset(events,0,sizeof events);
  uint8_t swap[160]={};uint32_t rectangle[4]={0,0,4,4};
  memcpy(swap+SWAP_SOURCE(0),rectangle,sizeof rectangle);memcpy(swap+SWAP_DEST(0),rectangle,sizeof rectangle);
  int result=actual_control(c.ready,c.gpu_only,swap);
  if(result!=c.result||restores!=c.restore||frames!=c.frame||writes!=c.write||strcmp(events,c.events)) {
   fprintf(stderr,"case %u mismatch result%d restore%d frames%d write%d events%s\n",index,result,restores,frames,writes,events);return 1;
  }
 }
 puts("PASS: actual framebuffer handoff block,10 completion/quarantine/fallback/unknown cases");
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'control-source', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old controls; choose a new output')
    args.out.mkdir(parents=True)
    results = []
    for label, source, expected in (('adapted', args.source, 0), ('original-control', args.control_source, 1)):
        text = source.read_text()
        first = text.index('\tbool gpuFrame=false;')
        last = text.index('\n\tfor (unsigned layer = 0; layer < SWAP_LAYERS; layer++) {', first)
        actual = text[first:last]
        directory = args.out / label
        directory.mkdir()
        unit = directory / 'actual-handoff.cpp'
        unit.write_text(PREFIX + actual + SUFFIX)
        binary = directory / 'actual-handoff'
        subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', str(unit), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary.resolve())], capture_output=True, text=True)
        (directory / 'run.log').write_text(result.stdout + result.stderr)
        results.append({'name': label, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                        'actual_block_sha256': hashlib.sha256(actual.encode()).hexdigest(), 'returncode': result.returncode,
                        'expected': expected, 'pass': result.returncode == expected})
    (args.out / 'results.json').write_text(json.dumps({'result': 'PASS' if all(row['pass'] for row in results) else 'FAIL',
                                                      'cases': results, 'scope': 'actual pre-composition owner-call order on host only; no hardware/latch/visual qualification'}, indent=2) + '\n')
    if not all(row['pass'] for row in results):
        raise SystemExit('Actual native handoff replay failed; inspect retained logs')
    print('PASS actual native framebuffer handoff block and original negative control under ASan/UBSan')


if __name__ == '__main__':
    main()
