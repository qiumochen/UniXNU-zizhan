/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Real frame offer/map/complete, adapted to Pong's existing Mini framebuffer.
 * A separate render-server connection waits for frames, never holding the BO
 * or submission scanout_gate. Only a real completed GPU copy retires a frame.
 */
#include "PongAdrenoScanout.h"
#include "../PongPower/PongDsiLease.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOSubMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/clock.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <libkern/OSAtomic.h>
extern "C" void pong_ramoops_diagnostic_write(const char *,int);
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr,vm_size_t size,uint32_t flags);
extern "C" void flush_dcache64(addr64_t addr,unsigned count,int phys);
/* NativeUfs/PongUfsTransport.cpp: copies the ramoops zones to the pinned klog file (reason 1 deadline, 2 panic; 3 here) */
extern "C" int pong_klog_flush(int reason) __attribute__((weak));
extern "C" int pong_gpu_fences(uint32_t *submitted,uint32_t *retired);   /* submit/NativeAdrenoSubmit.cpp */
extern "C" volatile uint32_t native_adreno_io_coherent; extern "C" volatile uint32_t native_adreno_coherent_skips[4];
namespace {
IOLock *scanout_gate;
IOMemoryDescriptor *scanout;
task_t client;
PongScanoutModeInfo mode{};
bool waiting=false,workerReady=false,poisoned=false;
uint64_t sequence=0,completed=0;
/* per-stage timing (absolute time), reported every 64 frames */
uint64_t tMapped=0,tPrepare=0,tPickup=0,tMap=0,tUser=0,tRelease=0,tWindow=0,tLast=0,tMoving=0;unsigned tFrames=0,tGaps[6]={};
/* Frame-to-frame intervals under 17/25/34/50/90 ms and pauses (90 ms or more: nothing was moving). The rate over the moving
 * frames alone is the animation frame rate; the window rate also counts the idle time between swipes. */
static const unsigned kGapMs[5]={17,25,34,50,90};
/* The same over the whole boot, for one summary at the end of an unattended round (the per-64 lines lose bits in ramoops). */
static uint64_t gAllMoving;static unsigned gAllGaps[6],gAllFrames;
/* A1 (docs/plan/2026-10-30-cpu-side.md): moving intervals in refresh beats (fb_hz, default 120: 8.33 ms), as annibale's frame-budget
 * analysis did. Mostly whole beats means frames wait for refresh ticks; spread out means the rendering itself is slow. Off-beat:
 * more than 2 ms from the nearest whole beat. */
static unsigned gBeats[8],gOffBeat,gBeatUs;
extern "C" void pong_scanout_summary_copies(unsigned copies);
/* submit/NativeAdrenoSubmit.cpp: the GPU timeline (pong_gtrace), kind 3 = a scanout copy: offered, picked up by the worker, retired. */
extern "C" void pong_gtrace_note(uint32_t kind,uint32_t id,uint64_t t0,uint64_t t1,uint64_t t2);
extern "C" {extern volatile uint32_t pong_gtrace_on;}
/* submit/NativeAdrenoSubmit.cpp's fence-wait record. g116/g119: backboardd's copy never retired around frame 60, the GPU then
 * 0% busy and metal2vulkan reporting DeviceLost, with nothing in the kernel log; this says which fence it waited for and
 * what the GPU had written by then. */
extern "C" volatile uint32_t native_adreno_fence_wait[8];
extern "C" volatile uint32_t native_adreno_ring_poison[6];
extern "C" volatile uint32_t native_adreno_submit_poison[2];
extern "C" volatile uint32_t native_adreno_ib_log[4][4+28];
extern "C" volatile uint32_t native_adreno_ib_log_count;
extern "C" char native_adreno_ib_log_who[4][16];
extern "C" volatile uint32_t native_adreno_last_submit_fence;
extern "C" volatile uint64_t native_adreno_last_submit_us;
extern "C" int pong_gpu_fences(uint32_t *,uint32_t *);
uint64_t us(uint64_t a);
void fenceReport(const char *why,bool ibs=false)
{
    char text[300];
    const volatile uint32_t *w=native_adreno_fence_wait,*r=native_adreno_ring_poison,*c=native_adreno_submit_poison;
    int n=snprintf(text,sizeof(text),"PONG FENCE %s: waits %u timeouts %u, last timed out on %u (GPU had written %u), "
        "wait snapshot submitted %u completed %u, unheld waits %u, last refused %u\n",why,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
    if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
    uint32_t submitted=0,retired=0;
    const int observed=pong_gpu_fences(&submitted,&retired);
    n=snprintf(text,sizeof text,"PONG FENCE live %s: observed=%d submitted=%u retired=%u\n",why,observed,submitted,retired);
    if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
    n=snprintf(text,sizeof(text),"PONG POISON %s: ring %u from %08x (fence in memory %u, seqno %u, next %u, rptr %u), submit cores %u from %08x\n",
        why,r[0],r[1],r[2],r[3],r[4],r[5],c[0],c[1]);
    if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
    const uint32_t lastFence=native_adreno_last_submit_fence;
    const uint64_t lastUs=native_adreno_last_submit_us,nowUs=us(mach_absolute_time());
    n=snprintf(text,sizeof(text),"PONG FENCE age %s: newest submit %u, %llu ms ago\n",
        why,lastFence,(unsigned long long)(lastUs && nowUs>=lastUs?(nowUs-lastUs)/1000:0));
    if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
    /* the last four submissions, oldest first: fence, process, first IB address and size, its first 12 dwords */
    for(unsigned k=0;ibs && k<4;++k) {
        const unsigned slot=(native_adreno_ib_log_count+k)&3;
        const volatile uint32_t *e=native_adreno_ib_log[slot];
        char who[17]={};memcpy(who,(const void *)native_adreno_ib_log_who[slot],16);
        n=snprintf(text,sizeof(text),"PONG IB fence %u by %s at %08x%08x, %u bytes: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
            e[0],who[0]?who:"?",e[2],e[1],e[3],e[4],e[5],e[6],e[7],e[8],e[9],e[10],e[11],e[12],e[13],e[14],e[15]);
        if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
    }
}
uint64_t us(uint64_t a){uint64_t n;absolutetime_to_nanoseconds(a,&n);return n/1000;}
struct Pending {bool offered=false,delivered=false,done=false;uint64_t id=0;
    uint32_t status=0;IOMemoryDescriptor *memory=nullptr;IOMemoryMap *map=nullptr;
    uint32_t stride=0;uint64_t offeredAt=0;int slot=-1;uint32_t evict=0;
    IOMemoryDescriptor *evictMemory=nullptr;IOMemoryMap *evictMap=nullptr;
    int target=0;   /* the panel buffer the copy goes into (dbuf below); 0 = the panel memory */} pending;
/* Mappings kept per compositor surface (CoreAnimation cycles a few), so the
 * presenter can keep its GPU import instead of re-importing 16 MiB a frame.
 * An entry retains its source, prepared range and client mapping. */
struct Kept {IOMemoryDescriptor *source=nullptr;size_t offset=0,bytes=0;
    IOMemoryDescriptor *memory=nullptr;IOMemoryMap *map=nullptr;uint32_t token=0;uint64_t used=0;};
constexpr unsigned KeptCount=4;
Kept kept[KeptCount];
unsigned keepEnabled=1;uint32_t lastToken=0;
void drop(IOMemoryDescriptor *memory,IOMemoryMap *map)
{
    if(map)map->release();
    if(memory){(void)memory->complete();memory->release();}
}
void forget(Kept &k)
{
    drop(k.memory,k.map);if(k.source)k.source->release();k={};
}
void note(const char *phase,uint64_t id,unsigned status)
{
    char text[180];int n=snprintf(text,sizeof(text),"PONG SCANOUT: %s sequence=%llu status=0x%x\n",phase,id,status);
    if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
}
uint64_t deadline(unsigned ms){uint64_t d;clock_interval_to_deadline(ms,kMillisecondScale,&d);return d;}
/* DPU access (docs/progress/2026-10-31-frame-timeline.md section 7): a read-only probe (pong_dpu_probe) and, below, the panel double
 * buffering that flips the display's source address instead of copying every frame into the one buffer it fetches (pong_dpu_flip).
 * Register offsets: iOSPlus10 patches/xnu/0234-negroni-fb-zero-copy.patch and 0269-negroni-fb-double-buffer.patch (OnePlus 10 Pro,
 * SM8450, DPU 8.1; from Qualcomm sde_hw_sspp.c / sde_hw_ctl.c), identical to zizhan's live.dts qcom,mdss_mdp@ae00000 (sde-sspp-off
 * 0x5000 0x7000 0x9000 0xb000 0x25000 0x27000 0x29000 0x2b000, sde-ctl-off 0x16000..0x1b000, intf types dp dsi dsi dp). */
vm_offset_t dpuMap;uint64_t dpuConsolePhys;unsigned dpuProbeMode;
uint32_t dpuRd(vm_offset_t at){return *reinterpret_cast<volatile uint32_t *>(at);}
void dpuWr(vm_offset_t at,uint32_t v){*reinterpret_cast<volatile uint32_t *>(at)=v;__asm__ volatile("dsb sy":::"memory");}
void dpuLine(const char *text,int n){if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}}
/* A Qualcomm clock branch register (CBCR): bit 0 CLK_ENABLE, bit 31 CLK_OFF (1 = the branch is stopped). Enabling is what mainline's
 * qcom_branch_set_clk_en does for the always-on list (clk-branch.c); the clock is up when CLK_OFF drops. */
bool dpuBranchOn(vm_offset_t cbcr){return !(dpuRd(cbcr)&0x80000000u);}
bool dpuBranchEnable(vm_offset_t cbcr)
{
    dpuWr(cbcr,dpuRd(cbcr)|1u);
    for(unsigned i=0;i<2000 && !dpuBranchOn(cbcr);++i)IODelay(1);
    return dpuBranchOn(cbcr);
}
/* GCC 0x00100000 (the touch driver maps it as well): 0x37004 GCC_DISP_AHB_CLK, 0x37008 GCC_QMIP_DISP_AHB_CLK, 0x3700c GCC_DISP_HF_AXI_CLK,
 * 0x37014 GCC_DISP_SF_AXI_CLK, 0x3701c GCC_DISP_XO_CLK. DISPCC 0x0af00000: 0x80a4 DISP_CC_MDSS_AHB_CLK (the register path), 0x800c
 * MDSS_MDP_CLK, 0x8024 MDSS_VSYNC_CLK, 0xc004 MDSS_NON_GDSC_AHB_CLK, 0xc00c MDSS_RSCC_AHB_CLK, 0x9000 MDSS_GDSC GDSCR (bit 31 PWR_ON,
 * bit 0 SW_COLLAPSE), 0xb000 MDSS_INT2_GDSC (gcc-sm8450.c / dispcc-sm8450.c; qcom,sm8475-dispcc shares the layout; our DT says
 * qcom,cape-dispcc). MDSS 0x0ae00000: SSPP registers SRC_SIZE, SRC_XY, OUT_SIZE, OUT_XY, SRC0_ADDR, YSTRIDE0, SRC_FORMAT, OP_MODE; CTL
 * registers LAYER0.., TOP, FLUSH, INTF_ACTIVE, FETCH_PIPE_ACTIVE. */
enum : uint32_t {GccPa=0x00130000u,GccLen=0x10000,GccDispAhb=0x7004,GccQmipDispAhb=0x7008,GccDispHfAxi=0x700c,GccDispSfAxi=0x7014,GccDispXo=0x701c};
enum : uint32_t {DispccPa=0x0af00000u,DispccLen=0x10000,DcMdssAhb=0x80a4,DcMdp=0x800c,DcVsync=0x8024,DcNonGdscAhb=0xc004,DcRsccAhb=0xc00c,DcGdsc=0x9000,DcInt2Gdsc=0xb000};
enum : uint32_t {MdssPa=0x0ae00000u,MdssLen=0x84000,SsppSrcSize=0x00,SsppSrcXy=0x08,SsppOutSize=0x0c,SsppOutXy=0x10,SsppSrc0Addr=0x14,
    SsppYstride0=0x24,SsppSrcFormat=0x30,SsppOpMode=0x38,CtlLayer0=0x00,CtlTop=0x14,CtlFlush=0x18,CtlIntfActive=0xf4,CtlFetchActive=0xfc};
static const uint32_t sspp[8]={0x5000,0x7000,0x9000,0xb000,0x25000,0x27000,0x29000,0x2b000};
static const uint32_t ctl[6]={0x16000,0x17000,0x18000,0x19000,0x1a000,0x1b000};
static const char *const ssppNames[8]={"VIG0","VIG1","VIG2","VIG3","DMA0","DMA1","DMA2","DMA3"};
/* CTL_FLUSH bit of each SSPP (sde_hw_ctl.c sspp_tbl: VIG0..3 = 0 1 2 18, DMA0..3 = 11 12 24 25) and its CTL_FETCH_PIPE_ACTIVE bit
 * (fetch_tbl: VIG0..3 = 16..19, DMA0..3 = 0..3). CTL bit 17 flushes the CTL's own configuration. */
static const uint32_t ssppFlushBit[8]={0,1,2,18,11,12,24,25},ssppFetchBit[8]={16,17,18,19,0,1,2,3};
constexpr uint32_t CtlFlushCtl=1u<<17;
vm_offset_t gccMap,dispccMap;
/* g147 (2026-10-31): the first MDSS register read hung the SoC (watchdog, cold reset, no klog) although the panel was scanning. The
 * register path into the display subsystem is its AHB: GCC_DISP_AHB_CLK (GCC 0x37004) and DISP_CC_MDSS_AHB_CLK (DISPCC 0x80a4), both
 * of which Linux turns on itself before touching the DPU (gcc-sm8450.c keeps 0x37004/0x3701c always on at probe; sde_power_resource_enable
 * takes "iface_clk" = disp_cc_mdss_ahb_clk, live.dts mdss_mdp clock-names). The firmware leaves DISP_CC_MDSS_AHB_CLK off while scanning
 * (g148); turning it on changed nothing on screen (g149, g151). So every MDSS access goes through here: GCC -> DISPCC -> MDSS mapping, a
 * line (cache-cleaned into the ramoops) after each stage when verbose, stopping before a register whose clock is off unless fix. */
bool dpuBus(const char *when,bool fix,bool verbose)
{
    char text[240];
    if(!gccMap)gccMap=ml_io_map_unmappable(GccPa,GccLen,0x7 /* VM_WIMG_IO */);
    if(!gccMap){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: no mapping of GCC\n",when));return false;}
    const vm_offset_t gcc=gccMap;
    if(verbose)dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: console phys 0x%llx; GCC disp_ahb 0x%x qmip_disp_ahb 0x%x disp_hf_axi 0x%x disp_sf_axi 0x%x disp_xo 0x%x\n",
        when,(unsigned long long)dpuConsolePhys,dpuRd(gcc+GccDispAhb),dpuRd(gcc+GccQmipDispAhb),dpuRd(gcc+GccDispHfAxi),dpuRd(gcc+GccDispSfAxi),dpuRd(gcc+GccDispXo)));
    if(!dpuBranchOn(gcc+GccDispAhb)) {
        if(!fix){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: GCC_DISP_AHB_CLK is off, stopping before DISPCC (pong_dpu_probe|=0x10 turns it on)\n",when));return false;}
        const bool ok=dpuBranchEnable(gcc+GccDispAhb);
        dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: GCC_DISP_AHB_CLK enabled -> 0x%x (%s)\n",when,dpuRd(gcc+GccDispAhb),ok?"running":"still off"));
        if(!ok)return false;
    }
    if(!dispccMap)dispccMap=ml_io_map_unmappable(DispccPa,DispccLen,0x7);
    if(!dispccMap){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: no mapping of DISPCC\n",when));return false;}
    const vm_offset_t dispcc=dispccMap;
    const uint32_t gdsc=dpuRd(dispcc+DcGdsc);
    if(verbose)dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: DISPCC mdss_gdsc 0x%x int2_gdsc 0x%x mdss_ahb 0x%x mdp 0x%x vsync 0x%x non_gdsc_ahb 0x%x rscc_ahb 0x%x\n",
        when,gdsc,dpuRd(dispcc+DcInt2Gdsc),dpuRd(dispcc+DcMdssAhb),dpuRd(dispcc+DcMdp),dpuRd(dispcc+DcVsync),dpuRd(dispcc+DcNonGdscAhb),dpuRd(dispcc+DcRsccAhb)));
    if(!(gdsc&0x80000000u)){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: MDSS GDSC not powered, stopping before MDSS\n",when));return false;}
    if(!dpuBranchOn(dispcc+DcMdssAhb)) {
        if(!fix){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: DISP_CC_MDSS_AHB_CLK is off, stopping before MDSS (pong_dpu_probe|=0x10 turns it on)\n",when));return false;}
        const bool ok=dpuBranchEnable(dispcc+DcMdssAhb);
        dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: DISP_CC_MDSS_AHB_CLK enabled -> 0x%x (%s)\n",when,dpuRd(dispcc+DcMdssAhb),ok?"running":"still off"));
        if(!ok)return false;
    }
    if(!dpuMap)dpuMap=ml_io_map_unmappable(MdssPa,MdssLen,0x7 /* VM_WIMG_IO */);
    if(!dpuMap){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: no mapping of 0x%x\n",when,MdssPa));return false;}
    return true;
}
/* pong_dpu_probe bits: 1 at the deadline, 2 at the console handoff, 0x10 turn on a display AHB found off (writes two bits), 0x20 read the
 * APPS SMMU stream groups up to the display's (g149: reading on past them, SMR5.., ended in an SError panic "Unhandled implementation
 * specific error" - the hypervisor's SMMU; the display is group 3), 0x40 switch the display stream's S2CR to bypass (TYPE 01) and read it
 * back: the splash region is identity-mapped (SSPP address == physical), so a working bypass changes nothing on screen and means the
 * pipes may fetch any physical memory (g150: the write never returned, the phone reset after 29 s - the hypervisor owns S2CR), 0x80 read
 * the display's context bank (SCTLR, TTBR0/1, TCR, MAIR, FSR) and CBA2R. g151: CB3 SCTLR 0xe0 (M=0), TTBR0 0, CBA2R 1: the display's
 * stream is not translated at all, the pipes fetch physical addresses, so a flip needs no SMMU change. */
void dpuProbe(const char *when)
{
    const bool fix=dpuProbeMode&0x10,readSmmu=dpuProbeMode&0x20,bypass=dpuProbeMode&0x40,readCb=dpuProbeMode&0x80;
    char text[240];
    if(!dpuBus(when,fix,true))return;
    /* every SSPP's source address, size, crop, output, stride, format and op mode, every CTL's layer words, top, flush, active interface
     * and fetch pipes, and which pipe(s) read the console's physical pages. */
    for(unsigned p=0;p<8;++p) {
        const vm_offset_t b=dpuMap+sspp[p];const uint32_t addr=dpuRd(b+SsppSrc0Addr);
        dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: %s addr 0x%x size 0x%x xy 0x%x out 0x%x at 0x%x stride 0x%x fmt 0x%x op 0x%x%s\n",
            when,ssppNames[p],addr,dpuRd(b+SsppSrcSize),dpuRd(b+SsppSrcXy),dpuRd(b+SsppOutSize),dpuRd(b+SsppOutXy),dpuRd(b+SsppYstride0),
            dpuRd(b+SsppSrcFormat),dpuRd(b+SsppOpMode),addr==(uint32_t)dpuConsolePhys?" <- console":""));
    }
    for(unsigned c=0;c<6;++c) {
        const vm_offset_t b=dpuMap+ctl[c];
        dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: CTL%u layer 0x%x 0x%x 0x%x 0x%x top 0x%x flush 0x%x intf 0x%x fetch 0x%x\n",when,c,
            dpuRd(b+CtlLayer0),dpuRd(b+CtlLayer0+4),dpuRd(b+CtlLayer0+8),dpuRd(b+CtlLayer0+12),dpuRd(b+CtlTop),dpuRd(b+CtlFlush),
            dpuRd(b+CtlIntfActive),dpuRd(b+CtlFetchActive)));
    }
    dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: MDSS read\n",when));
    if(!readSmmu){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: SMMU not read (pong_dpu_probe 0x20 reads it)\n",when));return;}
    /* The display's stream in the APPS SMMU (live.dts apps-smmu@15000000, qcom,qsmmu-v500, 120 SMRs; qcom,handoff-smrs = <0x2800 0x402>
     * is the stream the bootloader leaves programmed for the display; smmu_sde_unsec_cb iommus <0x2800 0x402>). SMMU_sCR0 bit 0 CLIENTPD
     * (1 = every unmatched client bypasses), SMMU_SMRn at 0x800 (bit 31 valid, mask 16..30, id 0..14), SMMU_S2CRn at 0xc00 (TYPE bits
     * 16..17: 0 translate, 1 bypass, 2 fault; CBNDX 0..7), SMMU_CBARn at 0x1000: ARM MMU-500 TRM / SMMUv2. Whether a flip may point
     * the pipes at memory outside the splash region depends on this: bypass or an identity-mapped context says yes. */
    enum : uint32_t {SmmuPa=0x15000000u,SmmuLen=0x2000,SmmuSmr=0x800,SmmuS2cr=0xc00,SmmuCbar=0x1000,SmmuSmrs=120};
    static vm_offset_t smmu;
    if(!smmu)smmu=ml_io_map_unmappable(SmmuPa,SmmuLen,0x7);
    if(!smmu){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: no mapping of the APPS SMMU\n",when));return;}
    dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: SMMU sCR0 0x%x sCR2 0x%x IDR0 0x%x IDR1 0x%x IDR2 0x%x sGFSR 0x%x\n",when,
        dpuRd(smmu+0x0),dpuRd(smmu+0x8),dpuRd(smmu+0x20),dpuRd(smmu+0x24),dpuRd(smmu+0x28),dpuRd(smmu+0x48)));
    for(unsigned n=0;n<SmmuSmrs;++n) {
        const uint32_t smr=dpuRd(smmu+SmmuSmr+4*n);
        if(!(smr&0x80000000u))continue;
        const uint32_t s2cr=dpuRd(smmu+SmmuS2cr+4*n),cbar=dpuRd(smmu+SmmuCbar+4*n);
        const bool display=((smr&0x7fff)&~((smr>>16)&0x7fff))==(0x2800&~((smr>>16)&0x7fff));
        dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: SMR%u id 0x%x mask 0x%x S2CR 0x%x (%s cb %u) CBAR 0x%x%s\n",when,n,smr&0x7fff,(smr>>16)&0x7fff,
            s2cr,((s2cr>>16)&3)==0?"translate":((s2cr>>16)&3)==1?"bypass":"fault",s2cr&0xff,cbar,display?" <- display stream":""));
        if(!display)continue;
        if(bypass && ((s2cr>>16)&3)==0) {
            dpuWr(smmu+SmmuS2cr+4*n,(s2cr&~0x30000u)|0x10000u);
            IODelay(100);
            const uint32_t back=dpuRd(smmu+SmmuS2cr+4*n);
            dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: S2CR%u 0x%x -> bypass written, reads back 0x%x (%s), sGFSR 0x%x\n",when,n,s2cr,back,
                ((back>>16)&3)==1?"bypass":"NOT bypass",dpuRd(smmu+0x48)));
        }
        /* 0x80: the display's context bank (MMU-500: CB space at base + NUMPAGE * PAGESIZE, IDR1 NUMPAGENDXB=6 -> 128 pages of 4 KiB = 0x80000;
         * CB n at + n * 0x1000; SCTLR 0x0, TCR2 0x10, TTBR0 0x20, TTBR1 0x28, TCR 0x30, MAIR0 0x38, MAIR1 0x3c, FSR 0x58; CBA2R at 0x1800 + 4n).
         * TTBR0 is where the bootloader's identity map of the splash region lives: adding mappings there is a memory write, no SMMU register. */
        if(readCb) {
            const uint32_t cb=s2cr&0xff;
            static vm_offset_t cbm;static uint32_t cbMapped=~0u;
            if(cbMapped!=cb){cbm=ml_io_map_unmappable(SmmuPa+0x80000u+cb*0x1000u,0x1000,0x7);cbMapped=cb;}
            if(!cbm){dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: no mapping of CB%u\n",when,cb));break;}
            dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: CBA2R%u 0x%x\n",when,cb,dpuRd(smmu+0x1800+4*n)));
            const uint32_t sctlr=dpuRd(cbm+0x0);
            dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: CB%u SCTLR 0x%x\n",when,cb,sctlr));
            const uint64_t ttbr0=dpuRd(cbm+0x20)|(uint64_t(dpuRd(cbm+0x24))<<32),ttbr1=dpuRd(cbm+0x28)|(uint64_t(dpuRd(cbm+0x2c))<<32);
            dpuLine(text,snprintf(text,sizeof text,"PONG DPU %s: CB%u TTBR0 0x%llx TTBR1 0x%llx TCR 0x%x TCR2 0x%x MAIR0 0x%x MAIR1 0x%x FSR 0x%x\n",when,cb,
                (unsigned long long)ttbr0,(unsigned long long)ttbr1,dpuRd(cbm+0x30),dpuRd(cbm+0x10),dpuRd(cbm+0x38),dpuRd(cbm+0x3c),dpuRd(cbm+0x58)));
        }
        break;   /* g149: reading on past the display's group ended in an SError panic */
    }
}

/* Panel double buffering (iOSPlus10 0269's design, docs/progress/2026-10-31-frame-timeline.md "零拷贝路线" step 2): the GPU copies a frame
 * into a panel buffer the display is not fetching; when the copy has retired (the presenter's PongScanoutComplete comes after
 * waitUntilCompleted) the pipes that fetch the panel memory get that buffer's address (SSPP_SRC0_ADDR of each, the same address: VIG0 and
 * VIG1 each scan half the width through SRC_XY, g151) and the CTL driving them is flushed (CTL_FLUSH: the pipes' bits + CTL bit 17). The
 * engine latches a flush at its next frame start; under the firmware's auto-refresh (AUTOREFRESH_CONFIG 0x80000001, one frame per TE) no
 * CTL_START is written, as 0234/0269 (sde_encoder_phys_cmd.c:2169-2175: no CTL_START when auto-refresh is on). The buffer shown before is
 * free when CTL_FLUSH reads back clear (0234 zeroCopyLatched). Frames the CPU composes go into the panel memory as before, so the pipes
 * go back to it first (pong_scanout_cpu_frame from MiniIOMobileFramebuffer), and before the deadline reset too, for the panic text.
 *   pong_dpu_flip=1  the other buffers are kernel memory: one physically contiguous block below 4 GiB (SSPP_SRC0_ADDR is 32 bits, the
 *                    stream is untranslated, g151), pong_dpu_bufs of them (default 3, so a copy never waits for a flip to be taken: the
 *                    shown and the flipped buffer are busy, the third is free); the presenter's mapping is that block, the panel memory
 *                    stays the CPU's. This is also the test of a pipe fetching HLOS memory, which step 3 (CA's surfaces) needs.
 *   pong_dpu_flip=2  the other buffer is the free end of the splash region (zizhan.dts region29@b8000000 0x2b00000, no-map: the panel
 *                    memory at its start, the outer panel's 1080x2520 buffer at 0xb8fc9900, g151; the last 16.5 MiB are unused): two
 *                    buffers, the panel memory and that one, both in the presenter's mapping, a frame waits (at most 12 ms, spinning
 *                    with the gate held) when the flip before it is not taken yet. Mode 1 falls back to this when the allocation fails.
 * Mode 1 and 2 share the flip; a flip not taken within 1 s turns the double buffering off (pipes back to the panel memory). */
constexpr unsigned DbMax=8;
struct DbBuf {uint64_t phys,offset;uint32_t stride;};   /* offset: where the buffer is in the presenter's mapping (~0 = not in it: the CPU's) */
DbBuf db[DbMax];unsigned dbN=0,dbMode=0;bool dbOn=false;
int dbShown=0,dbFlipped=-1;uint64_t dbFlipAt=0;
/* The loader's panel pool, for the screenshots (shotCapture), mapped once at display start when pong_shot is set: later mappings of
 * the pool (per capture, or the whole pool uncached, write-combined or copy-back) were all refused once an app had been launched
 * (g187-g191), while one made before any app worked (g183). Frames outside the pool (the console buffer) are not captured. */
static uint64_t shotPoolPhys,shotPoolBytes;static IOMemoryMap *shotPoolMap;
static uint32_t hudOn;   /* fps_hud=1: frame rate in the top right corner (hudDraw) */
static void shotMapPool(uint64_t phys,uint64_t bytes)
{
    uint32_t at=0;(void)PE_parse_boot_argn("pong_shot",&at,sizeof at);(void)PE_parse_boot_argn("fps_hud",&hudOn,sizeof hudOn);
    if(!at && !hudOn)return;
    shotPoolPhys=phys;shotPoolBytes=bytes;
    IOMemoryDescriptor *md=IOMemoryDescriptor::withPhysicalAddress(phys,bytes,kIODirectionIn);
    const char *how="NOT mapped";
    if(md && (shotPoolMap=md->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapInhibitCache)))how="mapped uncached";
    else if(md && (shotPoolMap=md->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapWriteCombineCache)))how="mapped write-combined";
    if(md)md->release();
    IOLog("PONG SHOT: pool 0x%llx + %llu MiB %s\n",(unsigned long long)phys,(unsigned long long)(bytes>>20),how);
}
IOBufferMemoryDescriptor *dbAlloc;
IOMemoryDescriptor *dbPool;uint64_t dbPoolBytes;   /* mode 1 on the loader's panel pool (/chosen/pong-panel-pool) */
/* Pool buffers dbuf does not use itself are slots for backboardd's panel-sized canvases (xnu patch 0011 MiniIOSurface, boot-arg
 * pong_zc=1): step 3.1 of docs/plan/2026-10-31-zero-copy-step3.md. Taken/given under slotLock; geometry must match the panel. */
constexpr unsigned SlotMax=8;
uint64_t slotPhys[SlotMax];bool slotUsed[SlotMax];uint32_t slotBpr[SlotMax];int slotDb[SlotMax];unsigned slotCount,slotTaken,slotRefused;uint32_t slotBytes,zc;IOSimpleLock *slotLock;
/* The physical panel pool is outside managed RAM. Establish each cached
 * kernel mapping before user clients start, then reuse bounded subranges.
 * Recreating a physical pager from a later client thread can fail even
 * after prepare() succeeded (g342). A failed reservation is never offered. */
IOMemoryDescriptor *slotMemory[SlotMax];IOMemoryMap *slotMapping[SlotMax];
uint32_t slotPreMap=1;
uint32_t dsiOwner;
constexpr uint64_t SlotStride=0x1100000;   /* 17 MiB: 2160 x 1916 at up to 8704 bytes per row (CoreAnimation's 256-aligned rows, g152l) */
uint32_t dbCurStride;   /* SSPP_YSTRIDE0 as last written by dbFlip (0 = the firmware's) */
/* step 3.2 (pong_zc=2): frames shown straight from a canvas slot, no presenter copy */
uint64_t zcFrames,zcInPlace,zcFenceUs,zcFenceMaxUs,zcFenceTimeouts,zcLatchUs,zcLatchMaxUs,zcLatchTimeouts;
struct {uint32_t pipes[8];unsigned count;uint32_t ctl,flush;} dbPipes;
uint64_t dbFlips,dbWaits,dbWaitMaxUs,dbLatchMaxUs,dbInPlace,dbCpuFlips,dbTaken;unsigned dbLogged;
/* g152 (2026-10-31 18:42, pong_dpu_flip=2): the phone hung hard - no fastboot, no adb, no watchdog return in 7 minutes, a power-key reset
 * needed, the DRAM ramoops lost. Bring-up aids for the retry: pong_dpu_flip_klog=1 writes a mark line and copies the ramoops to the klog
 * file (UFS, survives a cold reset) before and after each step that could hang: the first GPU copy into another buffer, the first
 * SSPP_SRC0_ADDR write, the first CTL_FLUSH write. pong_dpu_flip_test bits, run at the first GPU frame (klog is up by then), no GPU
 * involved: 1 = a no-op flush (SRC0_ADDR = the panel memory again + CTL_FLUSH) and whether it latches, 2 = flip to buffer 1 (its
 * content as found) for 300 ms and back, 8 = then turn the double buffering off: the round goes on as before. */
unsigned dbTest,dbKlog,dbFirstCopy;bool dbTested,dbCpuOnly;
/* g152b: the self-test passed (writes and flips latch) and the phone hung after it; the first frame's GPU copy and the next MDSS read
 * are the remaining suspects. More test bits: 4 = flush without CTL bit 17 (SSPP bits only), 0x10 = after the self-test the frames are
 * the CPU's (the GPU never writes through the new mapping). More marks: the first GPU copy whatever its target, and the first MDSS
 * read after the self-test (a mark without register values first, so the line is on the medium before the read). */
void dbMark(const char *what,bool regs=true)
{
    char text[200];
    if(!dbN)regs=false;   /* mode 3: no MDSS access */
    if(regs)dpuLine(text,snprintf(text,sizeof text,"PONG DBUF mark: %s (CTL_FLUSH 0x%x, SSPP addr 0x%x)\n",what,dpuRd(dpuMap+dbPipes.ctl+CtlFlush),
        dpuRd(dpuMap+sspp[dbPipes.pipes[0]]+SsppSrc0Addr)));
    else dpuLine(text,snprintf(text,sizeof text,"PONG DBUF mark: %s\n",what));
    if(dbKlog && pong_klog_flush)pong_klog_flush(3);
}
/* the pipes whose SRC0_ADDR is the panel memory and the CTL whose FETCH_PIPE_ACTIVE lists them all */
bool dbFindPipes(void)
{
    char text[240];uint32_t fetch=0;dbPipes.count=0;dbPipes.flush=0;
    for(unsigned p=0;p<8;++p) {
        if(dpuRd(dpuMap+sspp[p]+SsppSrc0Addr)!=(uint32_t)dpuConsolePhys)continue;
        dbPipes.pipes[dbPipes.count++]=p;dbPipes.flush|=1u<<ssppFlushBit[p];fetch|=1u<<ssppFetchBit[p];
    }
    if(!dbPipes.count){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: no SSPP fetches the panel memory 0x%llx\n",(unsigned long long)dpuConsolePhys));return false;}
    int found=-1;
    for(unsigned c=0;c<6;++c)if((dpuRd(dpuMap+ctl[c]+CtlFetchActive)&fetch)==fetch){found=int(c);break;}
    if(found<0){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: no CTL fetches pipes 0x%x\n",fetch));return false;}
    dbPipes.ctl=ctl[found];if(!(dbTest&4))dbPipes.flush|=CtlFlushCtl;
    dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: pipes %s %s %s on CTL%d (intf 0x%x), flush 0x%x\n",ssppNames[dbPipes.pipes[0]],
        dbPipes.count>1?ssppNames[dbPipes.pipes[1]]:"",dbPipes.count>2?ssppNames[dbPipes.pipes[2]]:"",found,dpuRd(dpuMap+dbPipes.ctl+CtlIntfActive),dbPipes.flush));
    return true;
}
bool dbLatched(void){return (dpuRd(dpuMap+dbPipes.ctl+CtlFlush)&dbPipes.flush)==0;}
/* a taken flip: its buffer is the shown one, the one before is free */
void dbSettle(void)
{
    if(dbFlipped<0 || !dbLatched())return;
    const uint64_t t=us(mach_absolute_time()-dbFlipAt);if(t>dbLatchMaxUs)dbLatchMaxUs=t;
    ++dbTaken;dbShown=dbFlipped;dbFlipped=-1;
}
/* the pipes fetch buffer i from their next frame on */
void dbFlip(int i)
{
    const bool first=dbFlips==0 && dbKlog;
    if(first)dbMark("first flip: before the SSPP_SRC0_ADDR writes");
    __asm__ volatile("dsb sy":::"memory");
    const uint32_t st=db[i].stride?db[i].stride:uint32_t(mode.stride);
    if(st!=dbCurStride) {   /* CoreAnimation's canvases have 8704-byte rows, the panel memory 8640: SSPP_YSTRIDE0 low half */
        for(unsigned k=0;k<dbPipes.count;++k){const vm_offset_t r=dpuMap+sspp[dbPipes.pipes[k]]+SsppYstride0;dpuWr(r,(dpuRd(r)&0xffff0000u)|(st&0xffffu));}
        dbCurStride=st;
    }
    for(unsigned k=0;k<dbPipes.count;++k)dpuWr(dpuMap+sspp[dbPipes.pipes[k]]+SsppSrc0Addr,(uint32_t)db[i].phys);
    if(first)dbMark("first flip: SSPP written, before CTL_FLUSH");
    dpuWr(dpuMap+dbPipes.ctl+CtlFlush,dbPipes.flush);
    if(first)dbMark("first flip: CTL_FLUSH written");
    dbFlipped=i;dbFlipAt=mach_absolute_time();++dbFlips;
    if(dbLogged<4) {   /* bring-up: how long the engine takes to apply a flip (expected: up to one 8.3 ms frame) */
        ++dbLogged;unsigned k;
        for(k=0;k<80 && !dbLatched();++k)IODelay(250);
        char text[200];
        dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: flip %llu: buffer %d at 0x%llx %s after %llu us, CTL_FLUSH 0x%x, SSPP addr 0x%x\n",dbFlips,i,
            (unsigned long long)db[i].phys,dbLatched()?"taken":"NOT taken",us(mach_absolute_time()-dbFlipAt),dpuRd(dpuMap+dbPipes.ctl+CtlFlush),
            dpuRd(dpuMap+sspp[dbPipes.pipes[0]]+SsppSrc0Addr)));
        if(first)dbMark("first flip: done");
    }
}
void dbOff(const char *why);void intfProbe(void);void intfRate(const char *when);
/* pong_dpu_flip_test, at the first GPU frame, gate held */
void dbSelfTest(void)
{
    char text[200];unsigned k;
    dbTested=true;
    if(dbTest&1) {
        dbMark("test A: no-op flush, before the SSPP writes");
        for(k=0;k<dbPipes.count;++k)dpuWr(dpuMap+sspp[dbPipes.pipes[k]]+SsppSrc0Addr,(uint32_t)db[0].phys);
        dbMark("test A: SSPP written, before CTL_FLUSH");
        const uint64_t t0=mach_absolute_time();
        dpuWr(dpuMap+dbPipes.ctl+CtlFlush,dbPipes.flush);
        dbMark("test A: CTL_FLUSH written");
        for(k=0;k<80 && !dbLatched();++k)IODelay(250);
        dpuLine(text,snprintf(text,sizeof text,"PONG DBUF test A: no-op flush %s after %llu us\n",dbLatched()?"taken":"NOT taken",us(mach_absolute_time()-t0)));
        dbMark("test A: done");
    }
    if(dbTest&2) {
        dbMark("test B: flip to buffer 1 for 300 ms");
        dbFlip(1);
        IOSleep(300);
        dbMark("test B: back to the panel memory");
        dbFlip(0);
        for(k=0;k<80 && !dbLatched();++k)IODelay(250);
        dbSettle();
        dbMark("test B: done");
    }
    if(dbTest&8)dbOff("self-test only (pong_dpu_flip_test bit 8)");
    if(dbTest&0x10){dbCpuOnly=true;dbMark("CPU frames only from here (pong_dpu_flip_test bit 0x10)",false);}
}
void dbOff(const char *why)
{
    char text[200];
    dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: %s (CTL_FLUSH 0x%x, %llu flips, %llu taken)\n",why,dpuRd(dpuMap+dbPipes.ctl+CtlFlush),dbFlips,dbTaken));
    dbOn=false;
    if(dbShown!=0 || dbFlipped>0)dbFlip(0);
}
/* the buffer the next GPU copy goes into, or -1 when the GPU has to copy into the panel memory (no double buffering) */
int dbTarget(void)
{
    if(!dbOn)return -1;
    dbSettle();
    auto pick=[](){for(unsigned i=0;i<dbN;++i)if(db[i].offset!=~0ull && int(i)!=dbShown && int(i)!=dbFlipped)return int(i);return -1;};
    int t=pick();
    if(t<0 && dbFlipped>=0) {
        const uint64_t w0=mach_absolute_time();
        for(unsigned i=0;i<120 && !dbLatched();++i)IODelay(100);
        const uint64_t w=us(mach_absolute_time()-w0);++dbWaits;if(w>dbWaitMaxUs)dbWaitMaxUs=w;
        dbSettle();t=pick();
        if(t<0) {
            if(us(mach_absolute_time()-dbFlipAt)>1000000){dbOff("the display engine does not take flips");return -1;}
            ++dbInPlace;t=dbFlipped;   /* into the buffer about to be shown: one torn frame, the order kept */
        }
    }
    return t;
}
void dbInit(uint64_t physical,size_t rounded,uint32_t stride,uint32_t height)
{
    char text[300];
    unsigned want=2;(void)PE_parse_boot_argn("pong_dpu_bufs",&want,sizeof want);   /* 2 own buffers + 3 canvas slots (g152w) */if(want<2)want=2;if(want>DbMax-1)want=DbMax-1;
    (void)PE_parse_boot_argn("pong_dpu_flip_test",&dbTest,sizeof dbTest);(void)PE_parse_boot_argn("pong_dpu_flip_klog",&dbKlog,sizeof dbKlog);
    /* g152c (2026-10-31) hung with no MDSS write at all; mode 3 isolates the other half: the presenter gets the mode-2 two-range mapping
     * (panel memory + the splash tail) and copies into the panel memory, but no display clock is touched, no MDSS register read or
     * written, no flip. A hang here is the GPU/mapping path alone. */
    if(dbMode==3) {
        const uint64_t end=0xb8000000ull+0x2b00000ull,b=(end-rounded)&~4095ull;
        IOAddressRange r[2]={{physical,rounded},{b,rounded}};
        IOMemoryDescriptor *m=IOMemoryDescriptor::withAddressRanges(r,2,kIODirectionInOut,TASK_NULL);
        if(!m){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: mode 3: no descriptor\n"));return;}
        scanout->release();scanout=m;mode.bytes=rounded*2;
        dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: mode 3 (mapping test only): presenter mapping 0x%llx + 0x%llx, %llu MiB, no MDSS access\n",
            (unsigned long long)physical,(unsigned long long)b,(unsigned long long)(mode.bytes>>20)));
        return;
    }
    if(!dpuBus("flip",true,false)){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: MDSS not reachable\n"));return;}
    if(!dbFindPipes())return;
    intfProbe();
    db[0]={physical,~0ull};dbN=1;
    if(dbMode==1) {
        /* 3.0 (2026-10-31): the loader's low-RAM lease (DarwinBoot24 PANEL_POOL_PAGES, /chosen/pong-panel-pool {pa,bytes}). XNU owns
         * no page below 4 GiB on zizhan, so the inTaskWithPhysicalMask fallback below can never succeed (g152i). pong_panel_pool=0 ignores it. */
        uint32_t usePool=1;(void)PE_parse_boot_argn("pong_panel_pool",&usePool,sizeof usePool);
        uint64_t lease[2]={};
        if(usePool) {
            IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
            OSData *data=chosen?OSDynamicCast(OSData,chosen->getProperty("pong-panel-pool")):nullptr;
            if(data && data->getLength()==16)memcpy(lease,data->getBytesNoCopy(),16);
            if(chosen)chosen->release();
        }
        const uint64_t stride=rounded>SlotStride?rounded:SlotStride;   /* every pool buffer, dbuf's own too, is one slot wide */
        if(lease[0] && lease[1]>=2*stride && !(lease[0]&4095) && lease[0]+lease[1]<=0x100000000ull) {
            unsigned n=unsigned(lease[1]/stride);if(n>want)n=want;if(n>3)n=3;
            dbPool=IOMemoryDescriptor::withPhysicalAddress(lease[0],uint64_t(n)*stride,kIODirectionInOut);
            if(dbPool) {
                shotMapPool(lease[0],lease[1]);
                for(unsigned i=0;i<n;++i){db[dbN]={lease[0]+uint64_t(i)*stride,uint64_t(i)*stride};++dbN;}
                dbPoolBytes=uint64_t(n)*stride;
                zc=2;(void)PE_parse_boot_argn("pong_zc",&zc,sizeof zc);   /* default: canvases in slots and flipped directly (g152t/w) */
                const unsigned total=unsigned(lease[1]/stride);
                if(!slotLock)slotLock=IOSimpleLockAlloc();
                (void)PE_parse_boot_argn("pong_slot_premap",&slotPreMap,sizeof slotPreMap);
                for(unsigned i=n;i<total && slotCount<SlotMax && dbN<DbMax;++i) {
                    const uint64_t physical=lease[0]+uint64_t(i)*stride;
                    IOMemoryDescriptor *memory=nullptr;IOMemoryMap *mapping=nullptr;
                    if(slotPreMap) {
                        memory=IOMemoryDescriptor::withPhysicalAddress(physical,stride,kIODirectionInOut);
                        const IOReturn held=memory?memory->prepare():kIOReturnNoMemory;
                        if(held==kIOReturnSuccess)mapping=memory->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapCopybackCache);
                        if(!mapping) {
                            dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: slot premap FAILED phys=0x%llx bytes=%llu prepare=0x%x; slot skipped\n",physical,stride,held));
                            if(memory){if(held==kIOReturnSuccess)memory->complete();memory->release();}
                            continue;
                        }
                        dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: slot premap PASS phys=0x%llx bytes=%llu\n",physical,stride));
                    }
                    slotPhys[slotCount]=physical;slotDb[slotCount]=int(dbN);
                    slotMemory[slotCount]=memory;slotMapping[slotCount]=mapping;
                    db[dbN]={slotPhys[slotCount],~0ull};++dbN;++slotCount;   /* flip targets for step 3.2, never presenter targets */
                }
                slotBytes=uint32_t(stride);
                dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: panel pool 0x%llx (%llu MiB from the loader): %u buffers, %u canvas slots (pong_zc=%u)\n",
                    (unsigned long long)lease[0],(unsigned long long)(lease[1]>>20),n,slotCount,zc));
            }
        } else if(usePool) dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: no panel pool lease (0x%llx + 0x%llx)\n",(unsigned long long)lease[0],(unsigned long long)lease[1]));
        for(unsigned n=want;n>=2 && !dbAlloc && !dbPool;--n) {
            const mach_vm_size_t bytes=mach_vm_size_t(n)*rounded;
            dbAlloc=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryKernelUserShared,bytes,0xFFFFF000ull);
            if(!dbAlloc){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: no contiguous %llu MiB below 4 GiB\n",(unsigned long long)(bytes>>20)));continue;}
            IOByteCount seg=0;const addr64_t p=dbAlloc->getPhysicalSegment(0,&seg,kIOMemoryMapperNone);
            if(!p || seg<bytes || p+bytes>0x100000000ull) {
                dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: allocation 0x%llx (+0x%llx) not usable\n",(unsigned long long)p,(unsigned long long)seg));
                dbAlloc->release();dbAlloc=nullptr;continue;
            }
            flush_dcache64(p,unsigned(bytes),1);   /* the allocator's zeroing must not sit dirty over what the GPU writes */
            for(unsigned i=0;i<n;++i){db[dbN]={p+uint64_t(i)*rounded,uint64_t(i)*rounded};++dbN;}
        }
        if(dbN==1){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: falling back to the splash region\n"));dbMode=2;}
    }
    if(dbMode==2) {
        const uint64_t end=0xb8000000ull+0x2b00000ull,b=(end-rounded)&~4095ull;bool clear=b>=physical+rounded;
        for(unsigned p=0;p<8 && clear;++p) {   /* nothing the engine fetches may lie in it */
            const vm_offset_t r=dpuMap+sspp[p];const uint64_t a=dpuRd(r+SsppSrc0Addr);if(!a)continue;
            const uint64_t h=(dpuRd(r+SsppSrcSize)>>16)&0xffff,s=dpuRd(r+SsppYstride0)&0xffff,e=a+h*s;
            if(a<b+rounded && e>b){clear=false;dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: %s fetches 0x%llx..0x%llx, over the free slot 0x%llx\n",ssppNames[p],
                (unsigned long long)a,(unsigned long long)e,(unsigned long long)b));}
        }
        if(!clear)return;
        db[0].offset=0;db[dbN]={b,rounded};++dbN;
    }
    if(dbN<2)return;
    IOMemoryDescriptor *m=nullptr;
    if(dbMode==1){m=dbPool?dbPool:dbAlloc;m->retain();}
    else {IOAddressRange r[2]={{physical,rounded},{db[1].phys,rounded}};m=IOMemoryDescriptor::withAddressRanges(r,2,kIODirectionInOut,TASK_NULL);}
    if(!m){dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: off: no descriptor over the buffers\n"));dbN=1;return;}
    scanout->release();scanout=m;mode.bytes=dbPool?dbPoolBytes:rounded*(dbMode==1?dbN-1:dbN);dbOn=true;
    dpuLine(text,snprintf(text,sizeof text,"PONG DBUF: on (mode %u): %u buffers 0x%llx 0x%llx 0x%llx 0x%llx, %u x %u stride %u, presenter mapping %llu MiB\n",dbMode,dbN,
        (unsigned long long)db[0].phys,(unsigned long long)db[1].phys,dbN>2?(unsigned long long)db[2].phys:0ull,dbN>3?(unsigned long long)db[3].phys:0ull,
        unsigned(mode.width),height,stride,(unsigned long long)(mode.bytes>>20)));
}
extern "C" int pong_panel_slot_take(uint32_t width,uint32_t height,uint32_t bytesPerRow,uint32_t bytes,uint64_t *phys)
{
    *phys=0;
    if(!zc||!slotCount||!slotLock)return 0;
    if(width!=mode.width||height!=mode.height||bytesPerRow<width*4||(bytesPerRow&63)||bytesPerRow>0xffff||bytes>slotBytes||uint64_t(bytesPerRow)*height>slotBytes){
        if(slotRefused++<4)IOLog("PONG DBUF: canvas %ux%u bpr %u (%u bytes) does not fit a slot (%ux%u bpr %u, %u bytes)\n",
            width,height,bytesPerRow,bytes,unsigned(mode.width),unsigned(mode.height),unsigned(mode.stride),slotBytes);
        return 0;
    }
    IOSimpleLockLock(slotLock);
    for(unsigned i=0;i<slotCount;++i)if(!slotUsed[i]){slotUsed[i]=true;slotBpr[i]=bytesPerRow;db[slotDb[i]].stride=bytesPerRow;*phys=slotPhys[i];++slotTaken;break;}
    IOSimpleLockUnlock(slotLock);
    IOLog("PONG DBUF: canvas slot %s 0x%llx (%u taken so far)\n",*phys?"given":"NONE FREE for",(unsigned long long)*phys,slotTaken);
    return *phys?1:0;
}
extern "C" IOMemoryDescriptor *pong_panel_slot_memory(uint64_t phys,uint32_t bytes)
{
    if(!bytes || bytes>slotBytes || !slotLock)return nullptr;
    IOMemoryDescriptor *parent=nullptr;bool leased=false;
    IOSimpleLockLock(slotLock);
    for(unsigned i=0;i<slotCount;++i)if(slotPhys[i]==phys && slotUsed[i]) {
        leased=true;parent=slotMemory[i];if(parent)parent->retain();break;
    }
    IOSimpleLockUnlock(slotLock);
    if(!leased)return nullptr;
    if(!slotPreMap)return IOMemoryDescriptor::withPhysicalAddress(phys,bytes,kIODirectionInOut);
    if(!parent)return nullptr;
    IOMemoryDescriptor *memory=IOSubMemoryDescriptor::withSubRange(parent,0,bytes,kIODirectionInOut);
    parent->release();return memory;
}
extern "C" bool pong_panel_slot_range(uint64_t phys,uint64_t bytes)
{
    if(!bytes || bytes>slotBytes)return false;
    for(unsigned i=0;i<slotCount;++i)if(phys>=slotPhys[i] && phys-slotPhys[i]<=slotBytes-bytes)return true;
    return false;
}
extern "C" uint64_t pong_panel_slot_address(uint64_t phys)
{
    for(unsigned i=0;i<slotCount;++i)if(slotPhys[i]==phys && slotMapping[i])return slotMapping[i]->getVirtualAddress();
    return 0;
}
extern "C" bool pong_dsi_try_acquire(uint32_t owner)
{
    return pong_dsi_lease_try(&dsiOwner,owner);
}
extern "C" bool pong_dsi_release(uint32_t owner)
{
    return pong_dsi_lease_release(&dsiOwner,owner);
}
extern "C" void pong_panel_slot_give(uint64_t phys)
{
    if(!slotLock)return;
    IOSimpleLockLock(slotLock);
    for(unsigned i=0;i<slotCount;++i)if(slotPhys[i]==phys)slotUsed[i]=false;
    IOSimpleLockUnlock(slotLock);
    IOLog("PONG DBUF: canvas slot 0x%llx returned\n",(unsigned long long)phys);
}
void dbStats(const char *when)
{
    if(!dbN)return;
    if(zcFrames) {
        char text[260];
        dpuLine(text,snprintf(text,sizeof text,"PONG ZC %s: frames %llu in-place %llu, fence wait avg %llu max %llu us (timeouts %llu), latch wait avg %llu max %llu us (timeouts %llu)\n",when,
            (unsigned long long)zcFrames,(unsigned long long)zcInPlace,(unsigned long long)(zcFenceUs/zcFrames),(unsigned long long)zcFenceMaxUs,(unsigned long long)zcFenceTimeouts,
            (unsigned long long)(zcLatchUs/zcFrames),(unsigned long long)zcLatchMaxUs,(unsigned long long)zcLatchTimeouts));
    }
    char text[240];
    dpuLine(text,snprintf(text,sizeof text,"PONG DBUF %s: %s, flips %llu taken %llu (latch max %llu us) waits %llu (max %llu us) in-place %llu cpu-flips %llu, shown %d flipped %d, CTL_FLUSH 0x%x\n",
        when,dbOn?"on":"off",dbFlips,dbTaken,dbLatchMaxUs,dbWaits,dbWaitMaxUs,dbInPlace,dbCpuFlips,dbShown,dbFlipped,dpuRd(dpuMap+dbPipes.ctl+CtlFlush)));
    intfRate(when);
}
/* Panel timing, read only (docs/plan/2026-10-31-triple-buffer-swapwait.md section 3.3): the INTF tearcheck the firmware left. Offsets: Linux
 * dpu_hw_intf.c INTF_FRAME_COUNT 0xac and INTF_TEAR_* 0x280..0x2b4 (sheng DisplayTimingProbe.cpp reads the same 0x284 / 0x2b4); INTF blocks
 * at live.dts qcom,sde-intf-off 0x35000 0x36000 0x37000 0x38000 (MDSS-relative like the SSPP offsets; the inner panel is INTF_1, g149).
 * vsync_count (SYNC_CONFIG_VSYNC low 19 bits) = 19.2 MHz / (vtotal x fps): vtotal 2076 -> 154 at 60 Hz, 77 at 120. INT_COUNT_VAL = frame
 * << 16 | line: frames per second there is the TE rate the panel runs at. */
enum : uint32_t {IntfFrameCount=0xac,IntfTearVsyncSel=0x280,IntfTearCheckEn=0x284,IntfTearSyncVsync=0x288,IntfTearSyncHeight=0x28c,IntfTearWrCount=0x290,
    IntfTearInitVal=0x294,IntfTearIntCount=0x298,IntfTearThresh=0x29c,IntfTearStartPos=0x2a0,IntfTearRdPtrIrq=0x2a4,IntfTearLineCount=0x2b0,IntfTearAutorefresh=0x2b4};
static const uint32_t intfOff[4]={0x35000,0x36000,0x37000,0x38000};
unsigned teIntf=1;uint32_t teCount0,teFrames0;uint64_t teAt0;
void intfProbe(void)
{
    char text[300];
    const uint32_t active=dpuRd(dpuMap+dbPipes.ctl+CtlIntfActive);
    for(unsigned i=1;i<4;++i)if(active&(1u<<i)){teIntf=i;break;}
    for(unsigned i=1;i<3;++i) {
        const vm_offset_t b=dpuMap+intfOff[i];
        dpuLine(text,snprintf(text,sizeof text,"PONG INTF_%u%s: vsync_sel 0x%x tear_en 0x%x sync_cfg 0x%x (vsync_count %u) height 0x%x wrcount 0x%x init 0x%x int_count 0x%x thresh 0x%x "
            "start 0x%x rd_ptr 0x%x line 0x%x autorefresh 0x%x frames %u\n",i,i==teIntf?" (panel)":"",dpuRd(b+IntfTearVsyncSel),dpuRd(b+IntfTearCheckEn),dpuRd(b+IntfTearSyncVsync),
            dpuRd(b+IntfTearSyncVsync)&0x7ffff,dpuRd(b+IntfTearSyncHeight),dpuRd(b+IntfTearWrCount),dpuRd(b+IntfTearInitVal),dpuRd(b+IntfTearIntCount),dpuRd(b+IntfTearThresh),
            dpuRd(b+IntfTearStartPos),dpuRd(b+IntfTearRdPtrIrq),dpuRd(b+IntfTearLineCount),dpuRd(b+IntfTearAutorefresh),dpuRd(b+IntfFrameCount)));
    }
    const vm_offset_t b=dpuMap+intfOff[teIntf];
    teCount0=dpuRd(b+IntfTearIntCount)>>16;teFrames0=dpuRd(b+IntfFrameCount);teAt0=mach_absolute_time();
}
/* the TE and INTF frame rates since the last call, in tenths per second */
void intfRate(const char *when)
{
    if(!teAt0)return;
    const vm_offset_t b=dpuMap+intfOff[teIntf];
    const uint32_t count=dpuRd(b+IntfTearIntCount)>>16,frames=dpuRd(b+IntfFrameCount);const uint64_t now=mach_absolute_time(),t=us(now-teAt0);
    if(t<200000)return;
    const uint64_t te=uint64_t(uint16_t(count-teCount0))*10000000ull/t,fr=uint64_t(frames-teFrames0)*10000000ull/t;
    char text[200];
    dpuLine(text,snprintf(text,sizeof text,"PONG TE %s: INTF_%u te %llu.%llu/s frames %llu.%llu/s over %llu ms (vsync_count %u)\n",when,teIntf,te/10,te%10,fr/10,fr%10,t/1000,
        dpuRd(b+IntfTearSyncVsync)&0x7ffff));
    teCount0=count;teFrames0=frames;teAt0=now;
}
/* Panel refresh rate (step 2; docs/plan/2026-10-31-triple-buffer-swapwait.md section 3). The firmware leaves the panel at timing@0 = 60 Hz
 * (g153: INTF_1 60.5 frames/s, vsync_count 167) while CoreAnimation is told 120 Hz and paces its animations on a 120 Hz grid (40 Hz =
 * 3 ticks), which a 60 Hz panel can only show as 33/33/17 ms. The panel switches rate itself on a DCS command set: live.dts
 * qcom,mdss-dsi-timing-switch-command of timing@1 (120 Hz) / timing@0 (60 Hz), sent in HS mode; both timings have the same DSI clock,
 * porches and DSC, and the auto-refresh sends one frame per TE whatever the rate. Commands go through DSI0's command DMA in FIFO mode:
 * iOSPlus10 patches/xnu/0250-negroni-panel-brightness.patch negroni_dsi_dcs_write (booted on the OnePlus 10 Pro, the same SM8450 DSI 2.6
 * at 0xae94000; = Linux dsi_ctrl_hw_cmn_kickoff_fifo_command) and 0259 negroni_dsi_dcs_set (the DT packet format). Boot-arg pong_panel_hz
 * (default 120 since g160: g155 INTF_1 120.2 frames/s after 8 packets in 5.7 ms; 60 = leave the firmware's rate), sent at the first GPU frame with a klog mark around each step; pong_panel_tc=1 also sets the INTF tearcheck
 * vsync_count to the new rate (Android does, sde_encoder_phys_cmd.c tearcheck_config; with the TE pin and start = init the transfer starts
 * at the TE either way, 0259's reasoning). */
enum : uint32_t {DsiPa=0x0ae94000u,DsiLen=0x400,DsiHwVersion=0x000,DsiCtrl=0x004,DsiStatus=0x008,DsiCmdDmaCtrl=0x03c,DsiDmaCmdLength=0x04c,DsiTrigCtrl=0x084,
    DsiCmdDmaSwTrigger=0x090,DsiIntCtrl=0x110,DsiTpgCtrl=0x15c,DsiTpgCmdDmaInitVal=0x17c,DsiTpgDmaFifoReset=0x1ec};
constexpr uint32_t DsiDmaBusy=1u<<1,DsiMdpBusy=1u<<2,DsiIntDmaDone=1u<<0,DsiIntEnableBits=0xaaaaaaaau,DsiTpgFifoOn=3u<<16|1u<<2|1u<<1,DsiTrigSw=4u,DsiMaxPayload=60;
vm_offset_t dsiMap;unsigned panelHzWant,panelHzNow=60,dsiFailures,dsiMdpWaits;bool panelDone;
static const uint8_t panelSwitch120[]={0x39,0,0,0x40,0,0,3,0xf0,0x5a,0x5a, 0x39,0,0,0x40,0,0,3,0x60,0x08,0x00, 0x39,0,0,0x40,0,0,3,0xb0,0x03,0xf2,
    0x39,0,0,0x40,0,0,2,0xf2,0x70, 0x39,0,0,0x40,0,0,2,0xb9,0x02, 0x39,0,0,0x40,0,0,8,0xbd,0x01,0x00,0x00,0x80,0x00,0x01,0xc0,
    0x39,0,0,0x40,0,0,3,0x60,0x08,0x00, 0x39,0,0,0x00,0,0,2,0xf7,0x0f};
static const uint8_t panelSwitch60[]={0x39,0,0,0x40,0,0,3,0xf0,0x5a,0x5a, 0x39,0,0,0x40,0,0,3,0x60,0x08,0x00, 0x39,0,0,0x40,0,0,3,0xb0,0x03,0xf2,
    0x39,0,0,0x40,0,0,2,0xf2,0x70, 0x39,0,0,0x40,0,0,2,0xb9,0x11, 0x39,0,0,0x40,0,0,8,0xbd,0x01,0x00,0x00,0x80,0x00,0x01,0xc0,
    0x39,0,0,0x40,0,0,3,0x60,0x08,0x01, 0x39,0,0,0x00,0,0,2,0xf7,0x0f};
uint32_t dsiRd(uint32_t off){return dpuRd(dsiMap+off);}
void dsiWr(uint32_t off,uint32_t v){dpuWr(dsiMap+off,v);}
bool dsiInit(void)
{
    char text[240];
    if(!dsiMap)dsiMap=ml_io_map_unmappable(DsiPa,DsiLen,0x7 /* VM_WIMG_IO */);
    if(!dsiMap){dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: no mapping of DSI0\n"));return false;}
    const uint32_t ver=dsiRd(DsiHwVersion),ctl=dsiRd(DsiCtrl);
    dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: DSI0 version 0x%08x ctrl 0x%08x status 0x%08x dma_ctrl 0x%08x trig 0x%08x tpg 0x%08x int 0x%08x\n",
        ver,ctl,dsiRd(DsiStatus),dsiRd(DsiCmdDmaCtrl),dsiRd(DsiTrigCtrl),dsiRd(DsiTpgCtrl),dsiRd(DsiIntCtrl)));
    if((ver>>28)!=2 || (ctl&5u)!=5u){dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: DSI0 is not a running command-mode DSI 2.x controller: no DCS\n"));return false;}
    return true;
}
/* one DCS write (buf[0] = command): short 0x05/0x15 or long 0x39, virtual channel 0; 0 when the controller reported CMD_DMA_DONE */
int dsiDcs(const uint8_t *buf,uint32_t len,bool lp)
{
    uint32_t words[2+(DsiMaxPayload+3)/4];uint32_t n=0,i;
    if(!dsiMap || !len || len>DsiMaxPayload || dsiFailures>=3)return -1;
    if(len<=2)words[n++]=1u<<31|(len==1?0x05u:0x15u)<<16|(len==2?uint32_t(buf[1])<<8:0u)|buf[0];
    else {
        words[n++]=1u<<31|1u<<30|0x39u<<16|len;
        for(i=0;i<len;i+=4){uint32_t w=0;for(uint32_t b=0;b<4 && i+b<len;++b)w|=uint32_t(buf[i+b])<<(8*b);words[n++]=w;}
    }
    const uint32_t bytes=n*4;if(n&1u)words[n++]=0;   /* the FIFO takes an even number of words */
    int rc=0;
    for(i=0;i<250 && (dsiRd(DsiStatus)&DsiDmaBusy);++i)IODelay(20);
    if(dsiRd(DsiStatus)&DsiDmaBusy)rc=-2;   /* someone else's command never finished: leave it */
    else {
        for(i=0;i<1000 && (dsiRd(DsiStatus)&DsiMdpBusy);++i)IODelay(20);   /* between two auto-refresh transfers if possible */
        if(i==1000)++dsiMdpWaits;
        const uint32_t tpg=dsiRd(DsiTpgCtrl),dma=dsiRd(DsiCmdDmaCtrl),trig=dsiRd(DsiTrigCtrl);
        if((trig&0xfu)!=DsiTrigSw)dsiWr(DsiTrigCtrl,(trig&~0xfu)|DsiTrigSw);
        dsiWr(DsiIntCtrl,(dsiRd(DsiIntCtrl)&DsiIntEnableBits)|DsiIntDmaDone);
        dsiWr(DsiTpgCtrl,DsiTpgFifoOn);
        for(uint32_t w=0;w<n;++w)dsiWr(DsiTpgCmdDmaInitVal,words[w]);
        dsiWr(DsiCmdDmaCtrl,(dma&~(1u<<31|1u<<30|1u<<26))|1u<<28|(lp?1u<<26:0u));
        dsiWr(DsiDmaCmdLength,bytes);
        dsiWr(DsiCmdDmaSwTrigger,1);
        bool done=false;
        for(i=0;i<1250 && !done;++i){done=(dsiRd(DsiIntCtrl)&DsiIntDmaDone)!=0;if(!done)IODelay(20);}   /* 25 ms */
        /* Never reset/reuse a timed-out FIFO while its DMA is still active.
         * The shared lease stays held, so brightness cannot overwrite it. */
        if(!(dsiRd(DsiStatus)&DsiDmaBusy)) {
            dsiWr(DsiIntCtrl,(dsiRd(DsiIntCtrl)&DsiIntEnableBits)|DsiIntDmaDone);
            dsiWr(DsiTpgCtrl,0);dsiWr(DsiTpgDmaFifoReset,1);IODelay(1);dsiWr(DsiTpgDmaFifoReset,0);
            dsiWr(DsiTpgCtrl,tpg);dsiWr(DsiCmdDmaCtrl,dma);
            if((trig&0xfu)!=DsiTrigSw)dsiWr(DsiTrigCtrl,trig);
        }
        if(!done)rc=-3;
    }
    if(rc){++dsiFailures;char text[200];dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: DCS 0x%02x (%u bytes) failed %d, status 0x%x int 0x%x\n",buf[0],len,rc,dsiRd(DsiStatus),dsiRd(DsiIntCtrl)));}
    return rc;
}
/* a set in the device-tree packet format {type, last, vc, flags, wait ms, length hi, length lo, payload} */
int dsiSet(const uint8_t *set,uint32_t len,bool lp,unsigned *sent)
{
    uint32_t i=0;int rc=0;*sent=0;
    while(i+7<=len && !rc) {
        const uint32_t plen=uint32_t(set[i+5])<<8|set[i+6];
        if(!plen || i+7+plen>len){rc=-4;break;}
        if(set[i]!=0x05 && set[i]!=0x15 && set[i]!=0x39){rc=-5;break;}
        rc=dsiDcs(set+i+7,plen,lp);
        if(!rc){++*sent;if(set[i+4])IODelay(1000u*set[i+4]);}
        i+=7+plen;
    }
    return rc;
}
void panelHz(void)
{
    char text[240];
    panelHzWant=120;(void)PE_parse_boot_argn("pong_panel_hz",&panelHzWant,sizeof panelHzWant);   /* default since g160 (11-01) */
    if(panelHzWant!=120){panelDone=true;return;} // 60 preserves the firmware timing
    if(!pong_dsi_try_acquire(PongDsiTiming))return; // retry on the next real frame
    panelDone=true;
    if(!dsiInit()){pong_dsi_release(PongDsiTiming);return;}
    intfRate("before the switch");
    if(pong_klog_flush)pong_klog_flush(3);
    const uint64_t t0=mach_absolute_time();unsigned sent=0;
    const int rc=panelHzWant==120?dsiSet(panelSwitch120,sizeof panelSwitch120,false,&sent):dsiSet(panelSwitch60,sizeof panelSwitch60,false,&sent);
    if(!(dsiRd(DsiStatus)&DsiDmaBusy))pong_dsi_release(PongDsiTiming);
    dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: timing switch to %u Hz: %u packets sent, rc %d, %llu us, MDP-busy waits %u\n",panelHzWant,sent,rc,us(mach_absolute_time()-t0),dsiMdpWaits));
    if(pong_klog_flush)pong_klog_flush(3);
    if(rc)return;
    panelHzNow=panelHzWant;
    uint32_t tc=0;(void)PE_parse_boot_argn("pong_panel_tc",&tc,sizeof tc);
    const vm_offset_t b=dpuMap+intfOff[teIntf];const uint32_t cfg=dpuRd(b+IntfTearSyncVsync);
    if(tc) {   /* firmware convention: 19.2 MHz / (1916 lines x rate): 167 at 60, 83 at 120 */
        const uint32_t count=19200000u/(uint32_t(mode.height)*panelHzNow);
        dpuWr(b+IntfTearSyncVsync,(cfg&~0x7ffffu)|count);
        dpuLine(text,snprintf(text,sizeof text,"PONG PANEL: INTF_%u vsync_count %u -> %u (sync_cfg 0x%x)\n",teIntf,cfg&0x7ffff,count,dpuRd(b+IntfTearSyncVsync)));
    }
    for(unsigned k=0;k<300;++k)IODelay(1000);   /* 300 ms at the new rate (once, gate held) */
    intfRate("after the switch");
    if(pong_klog_flush)pong_klog_flush(3);
}
/* before the deadline reset and the panic text: the pipes back on the panel memory */
void dbRestore(void)
{
    if(!dbN)return;
    dbSettle();
    if(dbShown!=0 || dbFlipped>0){dbFlip(0);for(unsigned k=0;k<80 && !dbLatched();++k)IODelay(250);}
}
}
extern "C" void pong_scanout_initialize(uint64_t address,size_t bytes,uint32_t stride,uint32_t width,uint32_t height)
{
    unsigned enabled=0;
    if(scanout_gate || !PE_parse_boot_argn("pong_gpu_scanout",&enabled,sizeof(enabled)) || enabled!=1)return;
    if(!address || (address&4095) || !width || !height || stride<uint64_t(width)*4 ||
       bytes!=uint64_t(stride)*height || bytes>SIZE_MAX-4095)return;
    const size_t rounded=(bytes+4095)&~size_t(4095);
    if(address>UINT64_MAX-rounded)return;
    /* Sheng's GOP presenter hands out its device-memory descriptor, not a
     * pageable descriptor over the console's I/O VA. Pong's console is mapped
     * by ml_io_map_unmappable(), so prepare() cannot wire that VA. Use the
     * actual firmware physical range retained by PE_current_console(). Bit 0
     * is PE_create_console's physical-address marker; v_offset is the same
     * offset initialize_screen() adds to the console VA. */
    PE_Video video{};
    if(PE_current_console(&video) || !video.v_baseAddr || (video.v_baseAddr>>63) ||
       video.v_width!=width || video.v_height!=height || video.v_rowBytes!=stride ||
       video.v_depth!=32 || video.v_rotate ||
       (video.v_length && (video.v_offset>video.v_length || rounded>video.v_length-video.v_offset)))return;
    const uint64_t base=video.v_baseAddr&~UINT64_C(3);
    if(video.v_offset>UINT64_MAX-base)return;
    const uint64_t physical=base+video.v_offset;
    if(!physical || (physical&4095) || physical>UINT64_MAX-rounded)return;
    scanout=IOMemoryDescriptor::withPhysicalAddress(physical,rounded,kIODirectionInOut);
    if(!scanout)return;
    scanout_gate=IOLockAlloc();if(!scanout_gate){scanout->release();scanout=nullptr;return;}
    mode={PongScanoutVersion,PongScanoutBgra,width,height,stride,rounded};
    (void)PE_parse_boot_argn("pong_scanout_cache",&keepEnabled,sizeof(keepEnabled));
    dpuConsolePhys=physical;(void)PE_parse_boot_argn("pong_dpu_probe",&dpuProbeMode,sizeof(dpuProbeMode));
    static bool probed;   /* the console handoff comes twice */
    if((dpuProbeMode&2) && !probed){probed=true;dpuProbe("boot");}
    dbMode=1;(void)PE_parse_boot_argn("pong_dpu_flip",&dbMode,sizeof(dbMode));   /* default since 10-31: the loader's panel pool, splash fallback */
    if(dbMode)dbInit(physical,rounded,stride,height);
    note("actual framebuffer handoff available",0,0);
}
extern "C" IOReturn pong_scanout_connect(task_t owner)
{
    if(!scanout_gate || !owner)return kIOReturnNotReady;
    IOLockLock(scanout_gate);IOReturn result=kIOReturnBusy;
    if(!client && !poisoned){task_reference(owner);client=owner;result=kIOReturnSuccess;}
    IOLockUnlock(scanout_gate);return result;
}
extern "C" void pong_scanout_disconnect(task_t owner)
{
    if(!scanout_gate)return;
    IOLockLock(scanout_gate);
    if(client==owner) {
        waiting=false;workerReady=false;
        if(pending.offered && pending.delivered && !pending.done) {
            poisoned=true; // unknown DMA lifetime: retain source/mapping.
            note("client closed with outstanding GPU copy; retain pages",pending.id,kIOReturnAborted);
        } else if(pending.offered && !pending.delivered) {
            pending.done=true;pending.status=kIOReturnAborted;
        }
        if(!poisoned)for(auto &k:kept)forget(k);
        client=nullptr;task_deallocate(owner);IOLockWakeup(scanout_gate,&pending,false);
    }
    IOLockUnlock(scanout_gate);
}
extern "C" IOMemoryDescriptor *pong_scanout_memory(task_t owner)
{
    if(!scanout_gate)return nullptr;
    IOLockLock(scanout_gate);auto *memory=client==owner && !poisoned?scanout:nullptr;
    if(memory)memory->retain();IOLockUnlock(scanout_gate);return memory;
}
extern "C" bool pong_scanout_quarantined(void)
{
    if(!scanout_gate)return false;
    IOLockLock(scanout_gate);bool blocked=poisoned;IOLockUnlock(scanout_gate);return blocked;
}
extern "C" IOReturn pong_scanout_call(task_t owner,unsigned op,const void *in,size_t inBytes,void *out,size_t outBytes)
{
    if(!scanout_gate)return kIOReturnNotReady;
    IOLockLock(scanout_gate);
    if(client!=owner || poisoned){IOLockUnlock(scanout_gate);return kIOReturnNotReady;}
    if(op==PongScanoutMode && !inBytes && out && outBytes==sizeof(mode)) {
        memcpy(out,&mode,sizeof(mode));IOLockUnlock(scanout_gate);return kIOReturnSuccess;
    }
    if(op==PongScanoutComplete && in && inBytes==sizeof(PongScanoutDone) && !outBytes) {
        const auto &q=*static_cast<const PongScanoutDone *>(in);
        if(q.reserved || !pending.offered || !pending.delivered || pending.done || q.sequence!=pending.id) {
            IOLockUnlock(scanout_gate);return kIOReturnBadArgument;
        }
        pending.status=q.status;pending.done=true;tUser+=mach_absolute_time()-tMapped;
        if(q.status==PongScanoutNotSubmitted)workerReady=false;
        if(q.status && q.status!=PongScanoutNotSubmitted)poisoned=true;
        IOLockWakeup(scanout_gate,&pending,false);IOLockUnlock(scanout_gate);return kIOReturnSuccess;
    }
    if(op!=PongScanoutWait || !in || inBytes!=sizeof(PongScanoutWaitInfo) || !out || outBytes!=sizeof(PongScanoutFrame)) {
        IOLockUnlock(scanout_gate);return kIOReturnBadArgument;
    }
    if(waiting){IOLockUnlock(scanout_gate);return kIOReturnBusy;}
    uint64_t ms=static_cast<const PongScanoutWaitInfo *>(in)->timeout_ms;
    const uint64_t until=deadline(unsigned(ms>1000?1000:ms));waiting=true;workerReady=true;
    while(client==owner && !poisoned && (!pending.offered || pending.delivered)) {
        if(IOLockSleepDeadline(scanout_gate,&pending,until,THREAD_ABORTSAFE)!=THREAD_AWAKENED) {
            waiting=false;IOLockUnlock(scanout_gate);return kIOReturnTimeout;
        }
    }
    waiting=false;
    if(client!=owner || poisoned){IOLockUnlock(scanout_gate);return kIOReturnNotReady;}
    const uint64_t t0=mach_absolute_time();
    if(pending.offeredAt)tPickup+=t0-pending.offeredAt;
    auto *map=pending.map?pending.map:pending.memory->createMappingInTask(owner,0,kIOMapAnywhere);
    tMapped=mach_absolute_time();tMap+=tMapped-t0;
    if(!map || !map->getVirtualAddress()) {
        if(map)map->release();workerReady=false;pending.done=true;pending.status=kIOReturnNoMemory;
        IOLockWakeup(scanout_gate,&pending,false);IOLockUnlock(scanout_gate);return kIOReturnNoMemory;
    }
    pending.map=map;pending.delivered=true;uint32_t token=0;
    if(pending.slot>=0){kept[pending.slot].map=map;token=kept[pending.slot].token;}
    *static_cast<PongScanoutFrame *>(out)={pending.id,map->getVirtualAddress(),map->getLength(),pending.stride,
        uint32_t(mode.width),uint32_t(mode.height),mode.format,token,pending.evict,dbN?uint32_t(db[pending.target].offset):0u};
    IOLockUnlock(scanout_gate);return kIOReturnSuccess;
}
/* zizhan screenshots (docs/progress/2026-11-01-120hz-and-cadence.md section 12): pong_shot=<s> takes, from s seconds after boot,
 * pong_shot_n (default 6) captures pong_shot_ms (default 1000) apart of the zero-copy frame just flipped: every 16th pixel of every
 * 16th row (135 x 120 of 2160 x 1916), read through an uncached mapping once the frame's GPU work retired (copied frames too: with an
 * app in front CoreAnimation's canvas is not a pool slot). A thread prints each as
 * "PONG SHOT <n> begin <w> <h> at <us>", then "PONG SHOT <n> <row> <x> <RGB hex>" in 32-pixel chunks.
 * IOLog truncates a line near 255 bytes: a whole 135-pixel row silently lost its right 95 pixels, and shot.py painted them black.
 * tools/mac/shot.py writes PNGs. The canvases are BGRA8 (deep framebuffers are off). Lets a round check what the panel showed. */
static uint32_t shotAt,shotN=6,shotMs=1000,shotTaken,shotHud;   /* pong_shot_hud=1: 136 x 120 at full resolution from 160 px left of the top right corner (the fps_hud box) */static uint64_t shotNext;static bool shotArgs,shotThread;
static uint8_t shotPix[120][136*3];static unsigned shotW,shotH;static uint64_t shotWhen;static volatile uint32_t shotReady;
static void shotPrint(void *,wait_result_t)
{
    static char line[256];
    for(;;) {
        IOSleep(100);
        const uint32_t n=shotReady;
        if(n) {
            IOLog("PONG SHOT %u begin %u %u at %llu\n",n,shotW,shotH,us(shotWhen));
            for(unsigned y=0;y<shotH;++y) {
                for(unsigned x=0;x<shotW;x+=32) {
                    int k=snprintf(line,sizeof line,"PONG SHOT %u %u %u ",n,y,x);
                    const unsigned end=x+32<shotW?x+32:shotW;
                    for(unsigned i=x*3;i<end*3;++i)k+=snprintf(line+k,sizeof line-k,"%02x",shotPix[y][i]);
                    IOLog("%s\n",line);
                }
                if(y%8==7)IOSleep(5);   /* let the log drain */
            }
            shotReady=0;
            if(n>=shotN)break;
        }
    }
    thread_terminate(current_thread());thread_block(THREAD_CONTINUE_NULL);
}
static void shotCapture(uint64_t phys,uint32_t stride)
{
    if(!shotArgs) {
        shotArgs=true;(void)PE_parse_boot_argn("pong_shot",&shotAt,sizeof shotAt);
        (void)PE_parse_boot_argn("pong_shot_n",&shotN,sizeof shotN);(void)PE_parse_boot_argn("pong_shot_hud",&shotHud,sizeof shotHud);(void)PE_parse_boot_argn("pong_shot_ms",&shotMs,sizeof shotMs);
        if(shotAt)nanoseconds_to_absolutetime(uint64_t(shotAt)*1000000000ull,&shotNext);
        if(shotAt)IOLog("PONG SHOT: %u captures from %u s, %u ms apart (first zero-copy frame at %llu us)\n",shotN,shotAt,shotMs,us(mach_absolute_time()));
    }
    if(!shotAt || shotTaken>=shotN || shotReady)return;
    const uint64_t now=mach_absolute_time();
    if(now<shotNext)return;
    uint64_t step=0;nanoseconds_to_absolutetime(uint64_t(shotMs)*1000000ull,&step);shotNext=now+step;
    if(!shotThread) {
        thread_t thread=THREAD_NULL;
        if(kernel_thread_start(shotPrint,nullptr,&thread)!=KERN_SUCCESS){IOLog("PONG SHOT: no print thread\n");shotAt=0;return;}
        thread_deallocate(thread);shotThread=true;
    }
    const unsigned h=unsigned(mode.height);
    if(!shotPoolMap){shotAt=0;return;}
    if(shotPoolMap && phys>=shotPoolPhys && phys+uint64_t(stride)*h<=shotPoolPhys+shotPoolBytes) {
        const uint8_t *base=reinterpret_cast<const uint8_t *>(shotPoolMap->getVirtualAddress())+(phys-shotPoolPhys);
        shotW=(unsigned(mode.width)+15)/16;shotH=(h+15)/16;if(shotW>136)shotW=136;if(shotH>120)shotH=120;
        const unsigned step=shotHud?1:16,left=shotHud?unsigned(mode.width)-160:0;
        if(shotHud){shotW=136;shotH=120;}
        for(unsigned y=0;y<shotH;++y)for(unsigned x=0;x<shotW;++x) {
            const uint8_t *at=base+uint64_t(y)*step*stride+uint64_t(left+x*step)*4;
            const uint32_t v=*reinterpret_cast<const volatile uint32_t *>(at);
            shotPix[y][x*3]=uint8_t(v>>16);shotPix[y][x*3+1]=uint8_t(v>>8);shotPix[y][x*3+2]=uint8_t(v);
        }
        shotWhen=now;shotReady=++shotTaken;
    }
}
/* zizhan frame-rate HUD (docs/progress/2026-11-01-120hz-and-cadence.md section 16): fps_hud=1 draws, into every frame just before its
 * flip, the number of frames the panel took in the last second: white 5x7 digits scaled 6x on a black box in the top right corner,
 * written through the pool mapping made at display start (shotMapPool; frames outside the pool are not marked). About 7000 32-bit
 * stores per frame. When no frame comes for a second, a thread writes the new value (usually 0) into the frame on screen, under the
 * gate, so an idle screen does not keep showing the last rate. */
static const uint8_t hudFont[10][7]={{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},{2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12}};
static uint32_t hudValue,hudCount;static uint64_t hudWindow;static bool hudThread;
static void hudPaint(uint64_t phys,uint32_t stride)
{
    const unsigned h=unsigned(mode.height),w=unsigned(mode.width);
    if(!shotPoolMap || phys<shotPoolPhys || phys+uint64_t(stride)*h>shotPoolPhys+shotPoolBytes || w<200)return;
    uint8_t *base=reinterpret_cast<uint8_t *>(shotPoolMap->getVirtualAddress())+(phys-shotPoolPhys);
    enum {S=6,DW=5*S,DH=7*S,Gap=S,Pad=8,BoxW=3*DW+2*Gap+2*Pad,BoxH=DH+2*Pad};
    const unsigned x0=w-BoxW-24,y0=24;
    const unsigned v=hudValue>999?999:hudValue;
    const int digits[3]={v>=100?int(v/100):-1,v>=10?int(v/10%10):-1,int(v%10)};
    for(unsigned y=0;y<BoxH;++y) {
        volatile uint32_t *row=reinterpret_cast<volatile uint32_t *>(base+uint64_t(y0+y)*stride)+x0;
        const int fy=(int(y)-Pad)/S;const bool inY=y>=Pad && fy<7;
        for(unsigned x=0;x<BoxW;++x) {
            bool on=false;
            if(inY && x>=Pad) {
                const unsigned cx=x-Pad,d=cx/(DW+Gap),dx=cx%(DW+Gap);
                if(d<3 && dx<DW && digits[d]>=0)on=(hudFont[digits[d]][fy]>>(4-dx/S))&1;
            }
            row[x]=on?0xffffffffu:0xff000000u;
        }
    }
}
static void hudTick(void *,wait_result_t)
{
    for(;;) {
        IOSleep(250);
        IOLockLock(scanout_gate);
        uint64_t second=0;nanoseconds_to_absolutetime(1000000000ull,&second);
        const uint64_t now=mach_absolute_time();
        if(now-hudWindow>=second+second/4) {   /* no frame closed the window: the screen is idle */
            hudValue=uint32_t(uint64_t(hudCount)*1000000000ull/(us(now-hudWindow)*1000+1));hudCount=0;hudWindow=now;
            if(dbOn && dbShown>=0)hudPaint(db[dbShown].phys,db[dbShown].stride?db[dbShown].stride:uint32_t(mode.stride));
        }
        IOLockUnlock(scanout_gate);
    }
}
/* gate held, the frame's GPU work done, before its flip */
static void hudDraw(uint64_t phys,uint32_t stride)
{
    if(!hudOn || !shotPoolMap)return;
    const uint64_t now=mach_absolute_time();
    if(!hudThread) {
        hudThread=true;hudWindow=now;
        thread_t thread=THREAD_NULL;
        if(kernel_thread_start(hudTick,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    }
    ++hudCount;
    uint64_t second=0;nanoseconds_to_absolutetime(1000000000ull,&second);
    if(now-hudWindow>=second){hudValue=uint32_t((uint64_t(hudCount)*1000000ull+us(now-hudWindow)/2)/us(now-hudWindow));hudCount=0;hudWindow=now;}
    hudPaint(phys,stride);
}
static int finish();static int zcFlip(int zi,uint32_t stride);
/* Step 1 of docs/progress/2026-11-01-zero-copy-and-return-paths.md section 7 (pong_zc_async=1): a zero-copy frame returns once its flip
 * is written instead of after the latch (up to a whole TE period with the presenter waiting; at 120 Hz that is the frame budget). The
 * framebuffer (xnu 0013) tags each frame with its swap id before pong_scanout_frame; the flip records the tag, and whoever wants to know
 * which swap is on the panel (SwapWait, from CoreAnimation's thread) calls pong_scanout_latch_poll: CTL_FLUSH read without the gate,
 * after the tag (the tag is stored after the flush write, so a clear flush seen after a tag belongs to that tag or a later one). The
 * next flip still waits for the one before to latch (the order of frames is kept; overwriting an unlatched SRC0_ADDR pair could show
 * the halves of two frames). Return value 3 = flipped, latch pending. iOSPlus10 0234 (fZCShown/fZCNext, zeroCopyRetire) is the same
 * idea driven by a vsync tick. */
extern "C" {volatile uint32_t pong_zc_flipped_tag,pong_zc_latched_tag;}
uint32_t zcTag;unsigned zcAsync;
extern "C" void pong_scanout_set_tag(uint32_t tag){zcTag=tag;}
static void zcLatchedTo(uint32_t t){for(;;){const uint32_t o=pong_zc_latched_tag;if(int32_t(t-o)<=0)return;if(OSCompareAndSwap(o,t,&pong_zc_latched_tag))return;}}
extern "C" uint32_t pong_scanout_latch_poll(void)
{
    const uint32_t t=pong_zc_flipped_tag;
    __asm__ volatile("dmb ish":::"memory");
    if(t!=pong_zc_latched_tag && dbOn && dbN && dbLatched())zcLatchedTo(t);
    return pong_zc_latched_tag;
}
//0: caller keeps CPU path.1: completed real copy.2: DMA uncertain: keep the
// previous screen and pinned pages, never race CPU scanout against the GPU.
extern "C" int pong_scanout_frame(IOMemoryDescriptor *source,size_t offset,uint32_t stride,uint32_t width,uint32_t height)
{
    if(!scanout_gate || !source || stride<uint64_t(width)*4 || width!=mode.width || height!=mode.height || (offset&4095))return 0;
    IOLockLock(scanout_gate);
    if(poisoned){IOLockUnlock(scanout_gate);return 2;}
    if(!client || !workerReady || pending.offered){IOLockUnlock(scanout_gate);return 0;}
    size_t bytes=(size_t(stride)*height+4095)&~size_t(4095);
    if(offset>source->getLength() || bytes>source->getLength()-offset){IOLockUnlock(scanout_gate);return 0;}
    /* dbuf: the buffer this copy goes into. Mode 1 off (a flip was not taken): the panel memory is not in the presenter's mapping, so
     * the frames are the CPU's from here on. */
    int target=0;
    if(dbOn && dbTest && !dbTested)dbSelfTest();
    if(dbOn && !panelDone)panelHz();
    if(dbCpuOnly){IOLockUnlock(scanout_gate);return 0;}
    /* step 3.2: CoreAnimation's canvas is a pool slot: no copy, flip the pipes to it (zcFlip releases the gate) */
    if(zc>=2 && dbOn && dbPool && offset==0 && slotCount) {
        IOByteCount seg=0;const addr64_t p=source->getPhysicalSegment(0,&seg,kIOMemoryMapperNone);
        for(unsigned i=0;i<slotCount;++i)if(slotPhys[i]==p && seg>=bytes)return zcFlip(slotDb[i],stride);
    }
    if(dbOn){target=dbTarget();if(target<0){if(dbMode==1){IOLockUnlock(scanout_gate);return 0;}target=0;}}
    else if(dbN>1 && dbMode==1){IOLockUnlock(scanout_gate);return 0;}
    if((dbN>1 || dbMode==3) && dbKlog && !dbFirstCopy){dbFirstCopy=1;char what[96];snprintf(what,sizeof what,"first GPU copy (buffer %d, offset 0x%llx): offered",target,(unsigned long long)db[target].offset);dbMark(what);}
    int slot=-1;
    if(keepEnabled==1)for(unsigned i=0;i<KeptCount;i++)
        if(kept[i].source==source && kept[i].offset==offset && kept[i].bytes==bytes && kept[i].map){slot=int(i);break;}
    if(slot>=0) {
        Kept &k=kept[slot];k.used=sequence+1;
        pending={true,false,false,++sequence,0,k.memory,k.map,stride,mach_absolute_time(),slot};pending.target=target;
        return finish();
    }
    auto *memory=IOSubMemoryDescriptor::withSubRange(source,offset,bytes,kIODirectionInOut);
    if(!memory){IOLockUnlock(scanout_gate);return 0;}
    /* Pin the source for the whole offer/map/copy lifetime. A quarantined
     * DMA keeps this preparation as well as its descriptor and mapping. */
    const uint64_t p0=mach_absolute_time();
    if(memory->prepare()!=kIOReturnSuccess) {
        memory->release();IOLockUnlock(scanout_gate);return 0;
    }
    tPrepare+=mach_absolute_time()-p0;
    pending={true,false,false,++sequence,0,memory,nullptr,stride,mach_absolute_time()};pending.target=target;
    if(keepEnabled==1) {
        unsigned victim=0;
        for(unsigned i=0;i<KeptCount;i++){if(!kept[i].memory){victim=i;break;}if(kept[i].used<kept[victim].used)victim=i;}
        Kept &k=kept[victim];
        if(k.memory){pending.evict=k.token;pending.evictMemory=k.memory;pending.evictMap=k.map;
            if(k.source)k.source->release();k={};}
        source->retain(); // the entry owns the prepared range from here on
        if(!++lastToken)++lastToken;
        k={source,offset,bytes,memory,nullptr,lastToken,pending.id};pending.slot=int(victim);
    }
    return finish();
}
/* Zero-copy frame, gate held: the GPU work CoreAnimation submitted for it is at most the ring's newest fence; wait for that,
 * flip the pipes to the slot, wait for the latch (so the frame is on screen when show() returns and CoreAnimation may draw into
 * its other canvas), then account for it like a copied frame through finish() with a frame that is already done. */
static int zcFlip(int zi,uint32_t stride)
{
    const uint64_t z0=mach_absolute_time();
    if(!zcFrames)(void)PE_parse_boot_argn("pong_zc_async",&zcAsync,sizeof zcAsync);
    ++zcFrames;dbSettle();
    if(zi==dbShown)++zcInPlace;   /* CoreAnimation drew into the canvas on screen: that frame tore */
    uint32_t submitted=0,retired=0;const uint64_t f0=mach_absolute_time();
    if(pong_gpu_fences(&submitted,&retired)) {
        unsigned k=0;
        while(int32_t(submitted-retired)>0 && k<500){IODelay(100);++k;if(!pong_gpu_fences(&submitted,&retired))break;}   /* <= 50 ms */
        if(int32_t(submitted-retired)>0)++zcFenceTimeouts;
    }
    const uint64_t fw=us(mach_absolute_time()-f0);zcFenceUs+=fw;if(fw>zcFenceMaxUs)zcFenceMaxUs=fw;
    uint64_t lw=0;
    hudDraw(db[zi].phys,stride);
    if(zi!=dbFlipped && zi!=dbShown) {
        if(dbFlipped>=0) {   /* keep flips in order (async: this is where the presenter waits for the TE now) */
            const uint64_t o0=mach_absolute_time();unsigned k=0;while(k<200 && !dbLatched()){IODelay(100);++k;}
            if(dbLatched())zcLatchedTo(pong_zc_flipped_tag);
            dbSettle();if(zcAsync)lw=us(mach_absolute_time()-o0);
        }
        dbFlip(zi);
    }
    shotCapture(db[zi].phys,stride);
    pong_zc_flipped_tag=zcTag;   /* after the CTL_FLUSH write (dbFlip ends in a dsb) */
    const uint64_t l0=mach_absolute_time();unsigned k=0;tMapped=l0;   /* GT c: entry, flip written, latched (async: returned) */
    if(!zcAsync) {
        while(k<200 && !dbLatched()){IODelay(100);++k;}   /* <= 20 ms, about one 120 Hz frame plus slack */
        lw=us(mach_absolute_time()-l0);if(!dbLatched())++zcLatchTimeouts;else zcLatchedTo(zcTag);
    }
    zcLatchUs+=lw;if(lw>zcLatchMaxUs)zcLatchMaxUs=lw;
    dbSettle();
    if(zcFrames<=4) {
        char text[220];
        dpuLine(text,snprintf(text,sizeof text,"PONG ZC: frame %llu: buffer %d at 0x%llx stride %u, fence %u/%u after %llu us, %s after %llu us\n",
            (unsigned long long)zcFrames,zi,(unsigned long long)db[zi].phys,stride,submitted,retired,(unsigned long long)fw,dbShown==zi?"shown":zcAsync?"flipped (async)":"NOT latched",(unsigned long long)lw));
    }
    pending={true,true,true,++sequence,0,nullptr,nullptr,stride,z0,-1};pending.target=zi;
    const int r=finish();
    return r==1 && zcAsync?3:r;
}
/* Called with the gate held and pending offered: wake the presenter, wait for
 * the copy to retire, release what this frame does not keep. */
static int finish()
{
    IOLockWakeup(scanout_gate,&pending,false);const uint64_t until=deadline(2000);
    while(!pending.done && !poisoned)
        if(IOLockSleepDeadline(scanout_gate,&pending,until,THREAD_UNINT)!=THREAD_AWAKENED)break;
    if(pending.delivered && (!pending.done || (pending.status && pending.status!=PongScanoutNotSubmitted))) {
        poisoned=true;note("GPU copy not retired; retain pages and previous frame",pending.id,pending.status?:kIOReturnTimeout);
        fenceReport("at the stuck copy",true);
        IOLockUnlock(scanout_gate);return 2;
    }
    if(!pending.delivered)workerReady=false; // a vanished waiter cannot stall every subsequent CPU frame
    bool ok=pending.done && !pending.status;
    if(ok && completed++<3)note("GPU frame completed",pending.id,0);
    if(dbFirstCopy==1 && pending.delivered && pending.done){dbFirstCopy=2;dbMark(ok?"first GPU copy: retired":"first GPU copy: failed");}
    if(dbFirstCopy==2 && dbMode==3 && dbKlog && completed==64){dbFirstCopy=3;dbMark("64 GPU copies through the two-range mapping done");}
    if(ok && dbOn && pending.target!=dbShown && pending.target!=dbFlipped){hudDraw(db[pending.target].phys,db[pending.target].stride?db[pending.target].stride:uint32_t(mode.stride));dbFlip(pending.target);shotCapture(db[pending.target].phys,db[pending.target].stride?db[pending.target].stride:uint32_t(mode.stride));}   /* the copy has retired: show it */
    /* GT c: a copy (offered, picked up, retired) or, id bit 31, a zero-copy frame (zcFlip entered, flip written, latched) */
    if(pong_gtrace_on)pong_gtrace_note(3,(uint32_t)pending.id|(pending.memory?0u:0x80000000u),us(pending.offeredAt),pending.delivered?us(tMapped):0,us(mach_absolute_time()));
    const uint64_t r0=mach_absolute_time();
    drop(pending.evictMemory,pending.evictMap);
    if(pending.slot<0)drop(pending.memory,pending.map);
    else if(!pending.delivered)forget(kept[pending.slot]);
    pending={};
    tRelease+=mach_absolute_time()-r0;
    {
        const uint64_t now=mach_absolute_time();
        if(tLast) {
            const uint64_t gap=us(now-tLast);unsigned b=0;
            while(b<5 && gap>=kGapMs[b]*1000ull)++b;
            ++tGaps[b];if(b<5)tMoving+=gap;
            ++gAllGaps[b];if(b<5)gAllMoving+=gap;
            if(b<5) {
                if(!gBeatUs) {unsigned hz=120;(void)PE_parse_boot_argn("fb_hz",&hz,sizeof(hz));gBeatUs=1000000/(hz==60 || hz==90 || hz==120?hz:120);}
                uint64_t k=(gap+gBeatUs/2)/gBeatUs;if(!k)k=1;
                const uint64_t at=k*gBeatUs,off=gap>at?gap-at:at-gap;
                ++gBeats[k<7?k:7];if(off>2000)++gOffBeat;
            }
        }
        tLast=now;++gAllFrames;
    }
    if(tFrames==0)tWindow=mach_absolute_time();   /* wall time of the 64-frame window: the frame rate actually shown */
    if(++tFrames==64) {
        const uint64_t span=us(mach_absolute_time()-tWindow);
        const unsigned moving=tGaps[0]+tGaps[1]+tGaps[2]+tGaps[3]+tGaps[4];
        char text[300];int n=snprintf(text,sizeof(text),"PONG SCANOUT TIMING: 64 frames in %llu ms (%llu.%llu fps; moving %u at %llu fps; gaps %u/%u/%u/%u/%u/%u) avg us prepare=%llu pickup=%llu map=%llu user=%llu release=%llu coh=%u skips=%u/%u/%u/%u\n",
            span/1000,span?64000000ull/span:0,span?(640000000ull/span)%10:0,moving,tMoving?moving*1000000ull/tMoving:0,
            tGaps[0],tGaps[1],tGaps[2],tGaps[3],tGaps[4],tGaps[5],us(tPrepare)/64,us(tPickup)/64,us(tMap)/64,us(tUser)/64,us(tRelease)/64,native_adreno_io_coherent,native_adreno_coherent_skips[0],
            native_adreno_coherent_skips[1],native_adreno_coherent_skips[2],native_adreno_coherent_skips[3]);
        if(n>0){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
        tFrames=0;tPrepare=tPickup=tMap=tUser=tRelease=0;tMoving=0;for(unsigned &g:tGaps)g=0;
        pong_scanout_summary_copies(1);   /* the running total too: many copies spread through the log */
        dbStats("64");
    }
    IOLockUnlock(scanout_gate);return ok?1:0;
}

/* The whole-boot total: after every 64 frames and eight times from pong_autoreset before the reset, so the copies can outvote
 * the bit errors a ramoops round trip leaves (worst near the end of a long round). tools/mac/fpsvote.py recovers it. */
extern "C" void pong_scanout_summary_copies(unsigned copies)
{
    const unsigned moving=gAllGaps[0]+gAllGaps[1]+gAllGaps[2]+gAllGaps[3]+gAllGaps[4];
    const uint64_t rate10=gAllMoving?moving*10000000ull/gAllMoving:0;
    char text[320];
    const int n=snprintf(text,sizeof(text),"PONG FPS SUMMARY: frames %u, moving %u at %llu.%llu fps, gaps %u/%u/%u/%u/%u/%u (<17/<25/<34/<50/<90 ms/pause), beats %u/%u/%u/%u/%u/%u/%u off %u (1-7+ x %u us)\n",
        gAllFrames,moving,rate10/10,rate10%10,gAllGaps[0],gAllGaps[1],gAllGaps[2],gAllGaps[3],gAllGaps[4],gAllGaps[5],
        gBeats[1],gBeats[2],gBeats[3],gBeats[4],gBeats[5],gBeats[6],gBeats[7],gOffBeat,gBeatUs);
    for(unsigned i=0;i<copies && n>0;++i){IOLog("%s",text);pong_ramoops_diagnostic_write(text,n);}
}
extern "C" void pong_scanout_summary(void)
{
    /* The autoreset pinger thread. g152/g152b (2026-10-31): an MDSS register write froze the compositor thread for good (the display
     * AHB never answered); when the gate is held here the DPU is the likely culprit, and one more MDSS access would hang this thread too,
     * and with it the reset and the klog flush: there is no watchdog behind us (the Gunyah WDT never bit in 7 minutes). */
    if(scanout_gate) {
        if(IOLockTryLock(scanout_gate)){dbRestore();dbStats("deadline");IOLockUnlock(scanout_gate);}
        else if(dbN){char text[160];dpuLine(text,snprintf(text,sizeof text,"PONG DBUF deadline: gate held (a frame stuck?), MDSS not touched; flips %llu taken %llu shown %d flipped %d\n",dbFlips,dbTaken,dbShown,dbFlipped));}
    }
    fenceReport("at the deadline",true);pong_scanout_summary_copies(8);if(dpuProbeMode&1)dpuProbe("deadline");
}
/* MiniIOMobileFramebuffer, before it composes a frame on the CPU into the panel memory (patches/xnu-ipados18/0009): the pipes fetch the
 * panel memory again. Written before the CPU's copy, so that frame may tear once, as every CPU frame did before. */
extern "C" void pong_scanout_cpu_frame(void)
{
    if(!scanout_gate || !dbN)return;
    IOLockLock(scanout_gate);
    static bool firstRead;
    if(!firstRead && dbTested){firstRead=true;dbMark("first MDSS read after the self-test (a CPU frame): before",false);dbSettle();dbMark("first MDSS read after the self-test: after");}
    dbSettle();
    if(dbOn && (dbShown!=0 || dbFlipped>0)){dbFlip(0);++dbCpuFlips;}
    IOLockUnlock(scanout_gate);
}
extern "C" void pong_fence_report_if_changed(void)
{
    static uint32_t ring,core;
    if(native_adreno_ring_poison[0]==ring && native_adreno_submit_poison[0]==core)return;
    ring=native_adreno_ring_poison[0];core=native_adreno_submit_poison[0];
    fenceReport("new poison",true);
}
