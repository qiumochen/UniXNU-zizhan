/* SPDX-License-Identifier: MIT */
#include "NativeAdrenoSubmit.h"
#include "NativeAdrenoABI.h"
#include "AdrenoPM4.h"
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>

/* CP_INDIRECT_BUFFER is opcode 0x3f with a 3-word payload: 64-bit GPU address
 * and IB_SIZE in dwords (bits 0..19), exactly as the pinned adreno_pm4.xml
 * domain states and Linux msm_gem_submit.c converts the userspace byte size. */
enum : uint32_t {SubmitIndirectBuffer=0x3f,SubmitIbWords=3,SubmitMaxJobs=8,
    SubmitMaxBuffers=128,SubmitMaxIbBytes=0xfffffu*4u,
    SubmitAuditBytes=256u*1024u};   /* IB bytes the write-combine audit compares per submission */
extern "C" {volatile uint32_t native_adreno_coherence[8];}
/* The submission (1-based count) at which the write-combine audit found a dirty IB line or failed; 0 = never. */
extern "C" {volatile uint32_t native_adreno_wc_distrusted;}
/* The write-combine audit before each submission while trusted: [0] submissions audited, [1] IB lines compared, [2] dirty lines,
 * [3] stale clean lines, [4] the last audit failure (any failure also ends the trust), [5] pages found back at the default attribute
 * and write-combined again. */
extern "C" {volatile uint32_t native_adreno_wc_audit[6];}
/* Fence waits: [0] calls, [1] timeouts, [2] the fence of the last timed-out wait, [3] the fence value the GPU had written then, [4] the newest fence handed out,
 * [5] the newest completed, [6] waits for a fence no job holds (observeFence path), [7] the last fence refused as never submitted. */
extern "C" {volatile uint32_t native_adreno_fence_wait[8];}
/* The IBs of the last submissions: [slot] fence, iova low/high, bytes, then the first 28 dwords of the first IB (read as the kernel maps it). */
extern "C" {volatile uint32_t native_adreno_ib_log[4][4+28];volatile uint32_t native_adreno_ib_log_count;}
extern "C" {volatile uint32_t native_adreno_last_submit_fence;volatile uint64_t native_adreno_last_submit_us;}
/* ... and the submitting process of each slot (zizhan: which client's job a stuck ring is on, PongAdrenoScanout.cpp fenceReport). */
extern "C" {char native_adreno_ib_log_who[4][16];}
extern "C" void proc_selfname(char *buf, int size);
/* [0] submit-core poisonings, [1] the caller's return address (low 32 bits); with native_adreno_ring_poison (NativeAdrenoRing.cpp). */
extern "C" {volatile uint32_t native_adreno_submit_poison[2];}
/* zizhan GPU timeline (docs/plan/2026-10-31-frame-timeline.md): boot-arg pong_gtrace=<s>; ArmDTPlatform/PongBootReset.h starts it
 * pong_gtrace_delay seconds after SpringBoard (default 20). While on, every submission is recorded (fence, process, IB count, context change,
 * time in us of mach_absolute_time, the clock of MiniIOMobileFramebuffer's touch_lat=1 "tl:" lines) and a kernel thread polls the
 * CP-written timestamp every 200 us for when fences completed; PongAdrenoScanout.cpp adds each scanout copy (offer, pickup, done).
 * At the end the events go to the DIAG region as "GT" lines (tools/mac/gtrace.py joins them with the tl: lines per frame). */
extern "C" {volatile uint32_t pong_gtrace_on;}
extern "C" void pong_ramoops_diagnostic_write(const char *,int);
namespace {
struct GtEvent {uint32_t kind,id,a,b;uint64_t t0,t1,t2;char who[12];};
enum : uint32_t {GtMax=6144,GtSubmit=1,GtDone=2,GtScanout=3};
GtEvent gGt[GtMax];volatile uint32_t gGtCount;
NativeAdrenoRing *gGtRing;
/* zizhan zero-copy scanout (PongAdrenoScanout.cpp zcFlip): the newest fence handed out and the newest the GPU has retired. */
extern "C" int pong_gpu_fences(uint32_t *submitted,uint32_t *retired)
{
    if(!gGtRing)return 0;
    uint32_t v=0;if(!gGtRing->peekFence(&v))return 0;
    *submitted=gGtRing->lastSubmitted();*retired=v;return 1;
}
/* Per-IB GPU time (pong_gtrace_ib, default on with pong_gtrace): the ring words of a traced submission get, after the prologue and
 * after each of the first 32 IBs and after the last one, CP_THREAD_CONTROL(BR) + CP_EVENT_WRITE7(RB_DONE_TS, WRITE_ALWAYSON, RAM)
 * + CP_THREAD_CONTROL(BOTH): the BR pipe writes the 19.2 MHz always-on counter when its work so far is done, exactly Turnip's
 * tu_trace_record_ts for A7XX, so the differences are each IB's GPU time in order (BV binning runs ahead and is not counted).
 * The 64-bit stamps go to the fence page at 0x100 + slot*0x110 (slot = fence % 8, 34 stamps a slot; the page is 4 KiB and the
 * CP otherwise uses its first 16 bytes). The poller reads a slot when its fence completes, 8 submissions before it is reused. */
enum : uint32_t {GtIbMax=32,GtIbSlots=8,GtIbSlotBase=0x100,GtIbSlotBytes=0x110,GtIbRecords=1024,GtIbPacketWords=8,
    GtEventWrite7=0x46,GtRbDoneTs=0x16,GtWriteAlwaysOn=3u<<20,GtWriteEnabled=1u<<27,GtThreadControl=0x17,GtThreadBr=1,GtThreadBoth=3};
/* What an IB does, from a walk over its packets (and one level of CP_INDIRECT_BUFFER, where Turnip keeps the draws): render-mode
 * markers seen (CP_SET_MARKER modes as a bitmask), draws (CP_DRAW_INDX_OFFSET / _INDIRECT_MULTI / _INDX_INDIRECT), 2D blits (CP_BLIT),
 * compute (CP_EXEC_CS*), IB2 bytes, the last window scissor BR (GRAS_SC_WINDOW_SCISSOR_BR 0x80f1), MRT0 buffer info (RB_MRT[0].BUF_INFO
 * 0x8822: format bits 0..7), the last 2D destination BR (GRAS_A2D_DEST_BR 0x8406) and buffer info (RB_A2D_DEST_BUFFER_INFO 0x8c17),
 * the last resolve extent (RB_RESOLVE_CNTL_2 0x88d2). Register numbers: mesa registers/adreno/a6xx.xml (A6XX-A7XX variants). */
struct GtSig {uint32_t markers,draws,blits,cs,ib2bytes,scissorBR,mrt0,a2dBR,a2dInfo,resolveBR;};
struct GtIb {uint32_t fence,count,done;uint32_t bytes[GtIbMax];uint32_t head[GtIbMax][2];uint16_t us[GtIbMax+1];uint32_t start_us;GtSig sig[GtIbMax];};
GtIb *gGtIb;volatile uint32_t gGtIbCount;uint32_t gGtIbOn=1;
enum : uint32_t {GtScanWords=8192};
uint32_t gGtScan[GtScanWords];   /* under the owner's submit serialization */
void gtScan(NativeAdrenoVm *vm,uint64_t iova,uint32_t bytes,GtSig *sig,int depth)
{
    uint32_t *buf=gGtScan+(depth?GtScanWords/2:0);const uint32_t cap=GtScanWords/2;
    uint32_t n=bytes/4;if(n>cap)n=cap;
    if(!n || vm->peekWords(iova,buf,n))return;
    for(uint32_t i=0;i<n;) {
        const uint32_t h=buf[i];
        if((h>>28)==7) {
            const uint32_t op=(h>>16)&0x7f,cnt=h&0x3fff;
            if(op==0x3f && cnt>=3 && i+3<n) {   /* CP_INDIRECT_BUFFER: iova lo, hi, dwords */
                const uint32_t words=buf[i+3]&0xfffff;sig->ib2bytes+=words*4;
                if(depth<1)gtScan(vm,(uint64_t(buf[i+2])<<32)|buf[i+1],words*4,sig,depth+1);
            } else if(op==0x38 || op==0x2a || op==0x29)++sig->draws;
            else if(op==0x2c)++sig->blits;
            else if(op==0x33 || op==0x41)++sig->cs;
            else if(op==0x65 && cnt>=1 && i+1<n)sig->markers|=1u<<(buf[i+1]&0xf);
            i+=1+cnt;
        } else if((h>>28)==4) {
            const uint32_t reg=(h>>8)&0x7ffff,cnt=h&0x7f;
            for(uint32_t j=0;j<cnt && i+1+j<n;++j) {
                const uint32_t r=reg+j,v=buf[i+1+j];
                if(r==0x80f1)sig->scissorBR=v;else if(r==0x8822)sig->mrt0=v;else if(r==0x8406)sig->a2dBR=v;
                else if(r==0x8c17)sig->a2dInfo=v;else if(r==0x88d2)sig->resolveBR=v;
            }
            i+=1+cnt;
        } else ++i;
    }
}
uint64_t gtNow(){uint64_t n;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n/1000;}
void gtRecord(const GtEvent &e){const uint32_t i=__atomic_fetch_add(&gGtCount,1u,__ATOMIC_RELAXED);if(i<GtMax)gGt[i]=e;}
void gtDump(unsigned seconds)
{
    const uint32_t total=gGtCount,n=total<GtMax?total:GtMax;char text[160];unsigned kinds[4]={};
    for(uint32_t i=0;i<n;++i) {
        const GtEvent &e=gGt[i];int len=0;
        if(e.kind<4)++kinds[e.kind];
        if(e.kind==GtSubmit)len=snprintf(text,sizeof text,"GT s %u %llu %s %u %u\n",e.id,(unsigned long long)e.t0,e.who[0]?e.who:"?",e.a,e.b);
        else if(e.kind==GtDone)len=snprintf(text,sizeof text,"GT d %u %u %llu\n",e.id,e.a,(unsigned long long)e.t0);
        else if(e.kind==GtScanout)len=snprintf(text,sizeof text,"GT c %u %llu %llu %llu\n",e.id,(unsigned long long)e.t0,(unsigned long long)e.t1,(unsigned long long)e.t2);
        if(len>0)pong_ramoops_diagnostic_write(text,len);
    }
    const int len=snprintf(text,sizeof text,"PONG GTRACE: %u s, %u events (%u submits, %u completions, %u scanout copies), %u lost\n",
        seconds,n,kinds[GtSubmit],kinds[GtDone],kinds[GtScanout],total-n);
    if(len>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,len);}
    /* GT i FENCE N SUBMIT_US  us:bytes:head0:head1 ... (per IB in order, the first 32; then rest:us = the time after IB 31 up to the last
     * IB when N > 32). SUBMIT_US is the submission's CPU time (low 32 bits of the GT s time) to join the lines. */
    const uint32_t records=gGtIbCount<GtIbRecords?gGtIbCount:GtIbRecords;unsigned complete=0;
    for(uint32_t i=0;gGtIb && i<records;++i) {
        const GtIb &r=gGtIb[i];
        if(!r.done)continue;++complete;
        char line[2040];int at=snprintf(line,sizeof line,"GT i %u %u %u ",r.fence,r.count,r.start_us);
        const uint32_t n=r.count<GtIbMax?r.count:GtIbMax;
        for(uint32_t k=0;k<n && at>0 && at<(int)sizeof line-96;++k) {
            const GtSig &g=r.sig[k];
            at+=snprintf(line+at,sizeof line-at,"%u:%u:m%x.d%u.b%u.c%u.i%u.w%ux%u.f%x.a%ux%u.g%x.r%ux%u ",r.us[k],r.bytes[k],
                g.markers,g.draws,g.blits,g.cs,g.ib2bytes,(g.scissorBR&0x3fff)+1,((g.scissorBR>>16)&0x3fff)+1,g.mrt0&0xff,
                (g.a2dBR&0x3fff)+1,((g.a2dBR>>16)&0x3fff)+1,g.a2dInfo&0xff,(g.resolveBR&0x3fff)+1,((g.resolveBR>>16)&0x3fff)+1);
        }
        if(r.count>GtIbMax && at>0 && at<(int)sizeof line-24)at+=snprintf(line+at,sizeof line-at,"rest:%u ",r.us[GtIbMax]);
        if(at>0 && at<(int)sizeof line-2){line[at++]='\n';line[at]=0;pong_ramoops_diagnostic_write(line,at);}
    }
    const int len2=snprintf(text,sizeof text,"PONG GTRACE IB: %u submissions stamped, %u read back, %u lost\n",records,complete,
        gGtIbCount>GtIbRecords?gGtIbCount-GtIbRecords:0);
    if(len2>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,len2);}
}
/* A fence completed: read its slot's stamps into the record (ticks of the 19.2 MHz counter -> us). The page was invalidated by
 * peekFence() just before. */
void gtIbComplete(uint32_t fence)
{
    if(!gGtIb || !gGtRing)return;
    const uint32_t records=gGtIbCount<GtIbRecords?gGtIbCount:GtIbRecords;
    GtIb *r=nullptr;
    for(uint32_t i=records;i-- && i+16>=records;)if(gGtIb[i].fence==fence){r=&gGtIb[i];break;}
    if(!r || r->done)return;
    volatile const uint32_t *page=gGtRing->fencePage();
    const uint32_t slot=(fence%GtIbSlots),base=(GtIbSlotBase+slot*GtIbSlotBytes)/4;
    auto stamp=[&](uint32_t k){return (uint64_t(page[base+2*k+1])<<32)|page[base+2*k];};
    const uint32_t n=r->count<GtIbMax?r->count:GtIbMax;
    uint64_t prev=stamp(0);
    for(uint32_t k=0;k<n;++k) {
        const uint64_t now=stamp(k+1),d=now>=prev?(now-prev)*10/192:0;   /* 19.2 MHz: ticks / 19.2 = us */
        r->us[k]=d>65535?65535:uint16_t(d);prev=now;
    }
    if(r->count>GtIbMax) {
        const uint64_t now=stamp(GtIbMax+1),d=now>=prev?(now-prev)*10/192:0;
        r->us[GtIbMax]=d>65535?65535:uint16_t(d);
    }
    r->done=1;
}
void gtPoller(void *arg,wait_result_t)
{
    const unsigned seconds=(unsigned)(uintptr_t)arg;
    const uint64_t end=gtNow()+(uint64_t)seconds*1000000ull;
    uint32_t last=0;bool first=true;
    IOLog("PONG GTRACE: on for %u s\n",seconds);
    pong_gtrace_on=1;
    while(gtNow()<end) {
        uint32_t value=0;
        if(gGtRing && gGtRing->peekFence(&value)) {
            if(first){last=value;first=false;}
            if(value!=last){GtEvent e{GtDone,value,last,0,gtNow(),0,0,{}};gtRecord(e);
                for(uint32_t f=last+1;f!=value+1 && f-last<64;++f)gtIbComplete(f);
                last=value;}
        }
        delay_for_interval(200,NSEC_PER_USEC);
    }
    pong_gtrace_on=0;
    gtDump(seconds);
    thread_terminate(current_thread());
}
}
extern "C" void pong_gtrace_note(uint32_t kind,uint32_t id,uint64_t t0,uint64_t t1,uint64_t t2)
{
    if(!pong_gtrace_on)return;
    GtEvent e{kind,id,0,0,t0,t1,t2,{}};gtRecord(e);
}
extern "C" void pong_gtrace_start(unsigned seconds)
{
    thread_t thread;
    if(!seconds || !gGtRing){IOLog("PONG GTRACE: not started (ring %s)\n",gGtRing?"ready":"absent");return;}
    (void)PE_parse_boot_argn("pong_gtrace_ib",&gGtIbOn,sizeof gGtIbOn);
    if(gGtIbOn && !gGtIb && gGtRing->fenceBytes()>=GtIbSlotBase+GtIbSlots*GtIbSlotBytes) {
        gGtIb=(GtIb *)IOMalloc(sizeof(GtIb)*GtIbRecords);
        if(gGtIb)memset(gGtIb,0,sizeof(GtIb)*GtIbRecords);
    }
    IOLog("PONG GTRACE: per-IB stamps %s\n",gGtIb?"on":"off");
    if(kernel_thread_start(gtPoller,(void *)(uintptr_t)seconds,&thread)==KERN_SUCCESS)thread_deallocate(thread);
}
__attribute__((noinline)) IOReturn NativeAdrenoSubmit::poison()
{
    native_adreno_submit_poison[0]=native_adreno_submit_poison[0]+1;
    native_adreno_submit_poison[1]=(uint32_t)(uintptr_t)__builtin_return_address(0);
    poisoned_=true;return kIOReturnIOError;
}
NativeAdrenoSubmit::Job *NativeAdrenoSubmit::find(uint32_t fence)
{
    if(fence)for(auto &job:jobs_)if(job.active && job.fence==fence)return &job;
    return nullptr;
}
NativeAdrenoSubmit::Job *NativeAdrenoSubmit::free()
{
    for(auto &job:jobs_)if(!job.active)return &job;
    return nullptr;
}
IOReturn NativeAdrenoSubmit::attach(NativeAdrenoVm *vm,NativeAdrenoRing *ring)
{
    if(attached_ || poisoned_ || vm_ || ring_)return kIOReturnBusy;
    if(!vm || !ring || vm->poisoned() || ring->poisoned())return kIOReturnBadArgument;
    vm_=vm;ring_=ring;attached_=true;stats_={};current_context_=UINT32_MAX;
    gGtRing=ring;
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::release(Job &job)
{
    IOReturn status=kIOReturnSuccess;
    for(size_t i=0;i<job.count;++i) {
        IOReturn retired=vm_->retireJobAfterCompletion(job.handles[i]);
        if(retired)status=retired;else ++stats_.references_retired;
    }
    if(status)return poison();
    if(vm_->acquireImported(job.window_start,job.window_bytes))return poison();
    job={};return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::detach()
{
    if(poisoned_ || outstanding())return kIOReturnBusy;
    if(!attached_)return kIOReturnNotReady;
    attached_=false;vm_=nullptr;ring_=nullptr;return kIOReturnSuccess;
}
bool NativeAdrenoSubmit::queueOutstanding(uint32_t queue) const
{
    return rangeOutstanding(queue,1);
}
bool NativeAdrenoSubmit::rangeOutstanding(uint32_t first,uint32_t count) const
{
    for(const auto &job:jobs_)if(job.active && job.queue-first<count)return true;
    return false;
}
size_t NativeAdrenoSubmit::outstanding() const
{
    size_t count=0;for(const auto &job:jobs_)if(job.active)++count;return count;
}
IOReturn NativeAdrenoSubmit::submit(const NativeAdrenoSubmitBuffer *buffers,size_t count,
    uint32_t queue,uint32_t *fence,uint64_t window_start,uint64_t window_bytes)
{
    if(!fence)return kIOReturnBadArgument;*fence=0;
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    if(!buffers || !count || count>SubmitMaxBuffers)return kIOReturnBadArgument;
    if(window_bytes && window_start>UINT64_MAX-window_bytes)return kIOReturnBadArgument;
    /* Every buffer is validated against the live VM before any reference. */
    uint64_t *handles=scratch_handles_;size_t handle_count=0;
    uint32_t *words=scratch_words_;size_t word_count=0;
    /* pong_gtrace per-IB stamps: this submission's fence will be lastSubmitted()+1; its slot in the fence page. */
    const bool stamping=pong_gtrace_on && gGtIb && gGtIbOn;
    const uint64_t stampBase=stamping?ring_->fenceIova()+GtIbSlotBase+((ring_->lastSubmitted()+1)%GtIbSlots)*GtIbSlotBytes:0;
    auto stamp=[&](uint32_t k) {   /* THREAD_CONTROL(BR), EVENT_WRITE7 RB_DONE_TS -> stamp k, THREAD_CONTROL(BOTH) */
        uint32_t header=0;
        if(AdrenoPM4Type7(GtThreadControl,1,&header)!=AdrenoPM4Success)return false;
        words[word_count++]=header;words[word_count++]=GtThreadBr;
        if(AdrenoPM4Type7(GtEventWrite7,3,&header)!=AdrenoPM4Success)return false;
        const uint64_t at=stampBase+8ull*k;
        words[word_count++]=header;words[word_count++]=GtRbDoneTs|GtWriteAlwaysOn|GtWriteEnabled;
        words[word_count++]=(uint32_t)at;words[word_count++]=(uint32_t)(at>>32);
        if(AdrenoPM4Type7(GtThreadControl,1,&header)!=AdrenoPM4Success)return false;
        words[word_count++]=header;words[word_count++]=GtThreadBoth;return true;
    };
    if(stamping && !stamp(0))return poison();
    for(size_t i=0;i<count;++i) {
        const NativeAdrenoSubmitBuffer &buffer=buffers[i];
        if(!buffer.handle || !buffer.bytes || (buffer.bytes&3) || buffer.bytes>SubmitMaxIbBytes)
            return kIOReturnBadArgument;
        uint64_t iova=0;size_t bytes=0;unsigned permissions=0;
        IOReturn status=vm_->describe(buffer.handle,&iova,&bytes,&permissions);
        if(status)return status;
        if(window_bytes && (iova<window_start || iova-window_start>=window_bytes ||
            bytes>window_bytes-(iova-window_start)))return kIOReturnBadArgument;
        if(!(permissions&ArmSmmu4KRead) || buffer.iova<iova ||
            buffer.iova-iova>bytes || buffer.bytes>bytes-(buffer.iova-iova))return kIOReturnBadArgument;
        uint32_t header=0;
        if(AdrenoPM4Type7(SubmitIndirectBuffer,SubmitIbWords,&header)!=AdrenoPM4Success)
            return poison();
        words[word_count++]=header;
        words[word_count++]=(uint32_t)buffer.iova;
        words[word_count++]=(uint32_t)(buffer.iova>>32);
        words[word_count++]=(uint32_t)(buffer.bytes/4);
        if(stamping && (i<GtIbMax || i+1==count) && !stamp(i<GtIbMax?(uint32_t)i+1:GtIbMax+1))return poison();
        bool known=false;
        for(size_t j=0;j<handle_count;++j)if(handles[j]==buffer.handle) {known=true;break;}
        if(!known)handles[handle_count++]=buffer.handle;
    }
    IOReturn swept=retireCompleted();
    if(swept)return swept;
    Job *job=free();
    if(!job)return kIOReturnBusy;
    for(size_t i=0;i<handle_count;++i) {
        IOReturn status=vm_->holdJob(handles[i]);
        if(status) {
            while(i) {vm_->retireJobAfterCompletion(handles[--i]);++stats_.references_retired;}
            return status;
        }
        ++stats_.references_held;
    }
    uint32_t value=0;
    /* int-070: the first IB word as the kernel's cacheable alias and DRAM see it before any maintenance, then DRAM after the CPU
     * caches over every mapped BO are cleaned and invalidated. The board shows the last submission's record on a stall. */
    uint32_t cached=~0u,before=~0u,ignored=0,after=~0u;
    /* Write-combined stores this CPU made before the call (the client's command words) complete before anything below. */
    __asm__ volatile("dsb sy":::"memory");
    /* int-098/099: with write-combined user BOs and no maintenance the CP stopped at the end of the first IB, as in int-065..069 when an
     * IB sat in a CPU cache. While write-combined BOs are trusted, every line of this submission's IBs is compared as a CPU cache and
     * DRAM hold it (bounded). A dirty line means a client view still caches its BOs: maintain every BO from now on, this submission
     * included (the audit already wrote the IB lines back; the rest of the BOs may hold more). */
    if(vm_->writeCombinedTrusted()) {
        /* int-100: the dirty IB page was back at XNU's default (cacheable) attribute. Write-combine every such page again first; the
         * pmap writes its lines back on the way, so the audit below should then find none. */
        uint32_t reset=0;
        IOReturn audited=vm_->reassertWriteCombined(&reset,window_start,window_bytes);
        native_adreno_wc_audit[5]=native_adreno_wc_audit[5]+reset;
        uint32_t lines=0,dirty=0,stale=0;size_t budget=SubmitAuditBytes;
        /* The re-assertion above is the correction and runs every time; the audit only detects. It found no dirty line in int-141's
         * 133798 nor in sheng i566's 2765132 (1358 audits of every eighth submission, about 2 ms each under the publish lock, so every
         * eighth frame of every client waited), so only the first 16 submissions are compared now (Adreno740 5c8c145). */
        ++audit_tick_;
        const bool audit_now=audit_tick_<=16;
        if(!audit_now)budget=0;
        for(size_t i=0;i<count && budget && !audited;++i) {   /* a failed re-assertion skips the audit and ends the trust */
            const size_t bytes=buffers[i].bytes<budget?(size_t)buffers[i].bytes:budget;
            audited=vm_->auditRange(buffers[i].iova,bytes,&lines,&dirty,&stale);budget-=bytes;
        }
        if(audit_now && audited!=kIOReturnUnsupported) {   /* no board hook (host): nothing to compare */
            native_adreno_wc_audit[0]=native_adreno_wc_audit[0]+1;
            native_adreno_wc_audit[1]=native_adreno_wc_audit[1]+lines;
            native_adreno_wc_audit[2]=native_adreno_wc_audit[2]+dirty;
            native_adreno_wc_audit[3]=native_adreno_wc_audit[3]+stale;
            if(audited)native_adreno_wc_audit[4]=audited;
        }
        if(dirty || (audited && audited!=kIOReturnUnsupported)) {
            vm_->distrustWriteCombined();
            native_adreno_wc_distrusted=native_adreno_coherence[0]+1;
        }
    }
    /* The coherence sample reads the first IB's word through a fresh uncached mapping before and after the publication: every 64th
     * submission only (two such mappings per submission were CPU time on every frame; Adreno740 5c8c145). */
    static uint32_t coherence_turn;
    const bool sample=!(coherence_turn++&63);
    if(sample)(void)vm_->sampleWord(buffers[0].iova,&cached,&before);
    /* Only this client's IOVA window can be referenced by its validated
     * command buffers. Maintaining every other process's IOSurfaces here
     * was both expensive and outside this submission's ownership handoff. */
    IOReturn status=vm_->publishMapped(window_start,window_bytes);
    if(!status && sample) {
        (void)vm_->sampleWord(buffers[0].iova,&ignored,&after);
        native_adreno_coherence[0]=native_adreno_coherence[0]+1;
        native_adreno_coherence[1]=cached;native_adreno_coherence[2]=before;native_adreno_coherence[3]=after;
        if(cached!=before || before!=after) {
            if(!native_adreno_coherence[4]) {
                native_adreno_coherence[5]=cached;native_adreno_coherence[6]=before;native_adreno_coherence[7]=after;
            }
            native_adreno_coherence[4]=native_adreno_coherence[4]+1;
        }
    }
    /* Kept (unlike Adreno740 5c8c145): here it is only a barrier once a region has observed the bank, and unmap issues its own TLBIs. */
    if(!status)status=vm_->invalidateTlb();
    if(status) {
        for(size_t i=0;i<handle_count;++i) {vm_->retireJobAfterCompletion(handles[i]);++stats_.references_retired;}
        return status;
    }
    {
        const uint32_t slot=native_adreno_ib_log_count&3;uint32_t head[28];
        (void)vm_->peekWords(buffers[0].iova,head,28);
        native_adreno_ib_log[slot][0]=ring_->lastSubmitted()+1;native_adreno_ib_log[slot][1]=(uint32_t)buffers[0].iova;
        native_adreno_ib_log[slot][2]=(uint32_t)(buffers[0].iova>>32);native_adreno_ib_log[slot][3]=(uint32_t)buffers[0].bytes;
        for(unsigned i=0;i<28;++i)native_adreno_ib_log[slot][4+i]=head[i];
        proc_selfname(native_adreno_ib_log_who[slot],sizeof native_adreno_ib_log_who[slot]);
        native_adreno_ib_log_count=native_adreno_ib_log_count+1;
    }
    /* The single hardware ring alternates Vulkan clients. Reset the BR/BV
     * timestamps/resource table on each actual context change, as both
     * Nothing's gen7_rb_context_switch and Linux a6xx_set_pagetable do.
     * Queues within one client share that context. The prologue waits for
     * the preceding real fence before resetting any synchronization state. */
    const uint32_t context=queue/NativeAdrenoAbiMaxQueues;
    if(context!=current_context_)ring_->requestContextReset();
    status=ring_->submitFramed(words,word_count,&value);
    if(status) {
        /* A poisoned ring may already have consumed the WPTR write, so the
         * referenced buffers cannot be retired here. */
        if(ring_->poisoned()) {
            *job=Job{};++stats_.poisoned_jobs;
            for(size_t i=0;i<handle_count;++i)job->handles[i]=handles[i];
            job->count=handle_count;job->queue=queue;job->active=true;
            job->window_start=window_start;job->window_bytes=window_bytes;
            return poison();
        }
        while(handle_count) {vm_->retireJobAfterCompletion(handles[--handle_count]);++stats_.references_retired;}
        return status;
    }
    native_adreno_last_submit_us=gtNow();
    native_adreno_last_submit_fence=value;
    for(size_t i=0;i<handle_count;++i)job->handles[i]=handles[i];
    job->count=handle_count;job->fence=value;job->queue=queue;job->active=true;
    job->window_start=window_start;job->window_bytes=window_bytes;
    if(pong_gtrace_on) {
        GtEvent e{GtSubmit,value,(uint32_t)count,context!=current_context_?1u:0u,gtNow(),0,0,{}};
        proc_selfname(e.who,sizeof e.who);gtRecord(e);
        if(stamping) {
            const uint32_t i=__atomic_fetch_add(&gGtIbCount,1u,__ATOMIC_RELAXED);
            if(i<GtIbRecords) {
                GtIb &r=gGtIb[i];memset(&r,0,sizeof r);r.fence=value;r.count=(uint32_t)count;r.start_us=(uint32_t)e.t0;
                for(uint32_t k=0;k<count && k<GtIbMax;++k) {
                    r.bytes[k]=buffers[k].bytes;uint32_t head[2]={0,0};
                    (void)vm_->peekWords(buffers[k].iova,head,2);r.head[k][0]=head[0];r.head[k][1]=head[1];
                    gtScan(vm_,buffers[k].iova,buffers[k].bytes,&r.sig[k],0);
                }
            }
        }
    }
    current_context_=context;
    ++stats_.submissions;
    if(outstanding()>stats_.peak_jobs)stats_.peak_jobs=outstanding();
    *fence=value;
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::observeFence(uint32_t fence,const NativeAdrenoRingClock *clock,
    uint32_t timeout_us)
{
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    if(!fence)return kIOReturnBadArgument;
    const NativeAdrenoRingFence result=ring_->observeFence(fence,clock,timeout_us,nullptr);
    if(result==NativeAdrenoRingFencePoisoned)return poison();
    if(result==NativeAdrenoRingFenceArgument)return kIOReturnBadArgument;
    if(result==NativeAdrenoRingFenceTimeout)return kIOReturnTimeout;
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::wait(uint32_t fence,const NativeAdrenoRingClock *clock,
    uint32_t timeout_us)
{
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    Job *job=find(fence);
    uint32_t observed=0;
    native_adreno_fence_wait[0]=native_adreno_fence_wait[0]+1;
    if(!job) {
        native_adreno_fence_wait[6]=native_adreno_fence_wait[6]+1;
        const NativeAdrenoRingFence passed=ring_->observeFence(fence,clock,timeout_us,&observed);
        if(passed==NativeAdrenoRingFencePoisoned)return poison();
        if(passed==NativeAdrenoRingFenceArgument) {native_adreno_fence_wait[7]=fence;return kIOReturnBadArgument;}
        if(passed==NativeAdrenoRingFenceTimeout) {
            ++stats_.timeouts;
            native_adreno_fence_wait[1]=native_adreno_fence_wait[1]+1;native_adreno_fence_wait[2]=fence;native_adreno_fence_wait[3]=observed;
            native_adreno_fence_wait[4]=ring_->lastSubmitted();native_adreno_fence_wait[5]=ring_->completedFence();
            return kIOReturnTimeout;
        }
        return kIOReturnSuccess;
    }
    const NativeAdrenoRingFence result=ring_->wait(fence,clock,timeout_us,&observed);
    if(result==NativeAdrenoRingFencePoisoned)return poison();
    if(result==NativeAdrenoRingFenceArgument) {native_adreno_fence_wait[7]=fence;return kIOReturnBadArgument;}
    if(result==NativeAdrenoRingFenceTimeout) {
        ++stats_.timeouts;
        native_adreno_fence_wait[1]=native_adreno_fence_wait[1]+1;native_adreno_fence_wait[2]=fence;native_adreno_fence_wait[3]=observed;
        native_adreno_fence_wait[4]=ring_->lastSubmitted();native_adreno_fence_wait[5]=ring_->completedFence();
        return kIOReturnTimeout;
    }
    IOReturn status=release(*job);
    if(status)return status;
    ++stats_.completed;
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::waitAll(const NativeAdrenoRingClock *clock,uint32_t timeout_us)
{
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    while(outstanding()) {
        Job *oldest=nullptr;
        for(auto &job:jobs_)if(job.active && (!oldest || job.fence-oldest->fence>0x80000000u))oldest=&job;
        const IOReturn status=wait(oldest->fence,clock,timeout_us);
        if(status)return status;
    }
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::waitRange(uint32_t first,uint32_t count,const NativeAdrenoRingClock *clock,uint32_t timeout_us)
{
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    while(rangeOutstanding(first,count)) {
        Job *oldest=nullptr;
        for(auto &job:jobs_)if(job.active && job.queue-first<count && (!oldest || job.fence-oldest->fence>0x80000000u))oldest=&job;
        const IOReturn status=wait(oldest->fence,clock,timeout_us);
        if(status)return status;
    }
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSubmit::retireCompleted()
{
    if(!attached_ || poisoned_)return kIOReturnNotReady;
    for(auto &job:jobs_) {
        if(!job.active)continue;
        const NativeAdrenoRingFence state=ring_->observeFence(job.fence,nullptr,0,nullptr);
        if(state==NativeAdrenoRingFencePoisoned)return poison();
        if(state!=NativeAdrenoRingFenceComplete)continue;
        const IOReturn status=release(job);
        if(status)return status;
        ++stats_.completed;
    }
    return kIOReturnSuccess;
}
