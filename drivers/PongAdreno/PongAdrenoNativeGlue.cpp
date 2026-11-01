/* SPDX-License-Identifier: MIT */
/* A730 board -> complete upstream NativeAdreno publication/runtime. */
#include "PongAdrenoCp.h"
#include "PongAdrenoScanout.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <pexpert/pexpert.h>
#include <kern/task.h>
#include <kern/thread.h>
extern "C" IOReturn native_adreno_publish(NativeAdrenoVm *,NativeAdrenoSubmit *,NativeAdrenoRing *,
    const NativeAdrenoRingClock *,const NativeAdrenoTimer *,const NativeAdrenoAbiDeviceInfo *,uint32_t,uint32_t);
extern "C" IOService *native_adreno_published_service(void);
extern "C" void pong_ramoops_diagnostic_write(const char *,int);
extern IOReturn PongAdrenoPublishMetalAccelerator();
extern "C" struct vnode *rootvnode;
/* zizhan: publishing the IOAcceleratorES provider before the root volume is mounted stalled the APFS
 * volume IOMedia, so IOFindBSDRoot panicked (g2). Its only clients start under launchd, so publish once
 * rootvnode exists (bounded wait). */
static void PongAdrenoPublishMetalAfterRoot(void *, wait_result_t)
{
    for(unsigned i=0;i<3000 && !__atomic_load_n(&rootvnode,__ATOMIC_ACQUIRE);i++)IOSleep(100);
    const IOReturn status=PongAdrenoPublishMetalAccelerator();
    char line[160];const int n=snprintf(line,sizeof(line),
        "PONG METAL: NativeAdreno complete runtime publication status=0x%x (after root mount)\n",status);
    IOLog("%s",line);pong_ramoops_diagnostic_write(line,n);
}
static PongCpBackend *backend;
extern "C" bool pong_adreno_native_healthy(void)
{return backend && backend->healthy();}
extern "C" const NativeAdrenoStatus *pong_adreno_native_status(void)
{return backend?backend->status:nullptr;}
extern "C" IOReturn native_gop_handoff_call(uint32_t op,task_t task,const void *in,size_t inSize,void *out,size_t outSize)
{return pong_scanout_call(task,op+PongScanoutMode,in,inSize,out,outSize);}
extern "C" IOMemoryDescriptor *native_gop_handoff_memory(void)
{return pong_scanout_memory(current_task());}
bool PongAdrenoConfigureService(IOService *service)
{
    if(!service || !pong_adreno_native_healthy())return false;
    service->setProperty("PongAdrenoCPReady",true);
    service->setProperty("PongChipID",backend->info.chip_id,32);
    service->setProperty("PongNativePageBytes",4096,32);
    unsigned metal=0;
    const bool enabled=PE_parse_boot_argn("pong_metal",&metal,sizeof(metal)) && metal==1;
    #if PONG_GUEST_22H374
    service->setProperty("PongMetal22H374Enabled",enabled);
    unsigned global=0;
    if(enabled && PE_parse_boot_argn("pong_metal_global",&global,sizeof(global)) && global==1)
        service->setProperty("PongNativeMetalGlobal",kOSBooleanTrue);
#else
    service->setProperty("PongMetal20G81Enabled",enabled);
#endif
    if(enabled)service->setProperty("NativeSandboxGrantProcess",
        "backboardd,SpringBoard,CollectionsPoster,WidgetRenderer_Default,PosterBoard,Weather,WeatherPoster,GradientPosterExtension"
#if PONG_GUEST_22H374
        ",Preferences,LegacyPoster,MercuryPosterExtension,Unity2025Poster,PridePosterExtension"
#endif
    );
    return true;
}
#include "submit/AdrenoPM4.h"
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr,vm_size_t size,uint32_t flags);
extern "C" void ml_io_unmap(vm_offset_t addr,vm_size_t sz);
extern "C" volatile uint32_t native_adreno_io_coherent;
/* IO coherence, ported from UniXNU-sheng's coherenceProbe (package/GpuPowerProbe.cpp). The A730, like the A740, is
 * ADRENO_QUIRK_HAS_CACHED_COHERENT behind the dma-coherent GPU SMMU, and Linux maps its cached BOs write-back inner-shareable. A
 * kernel page is imported and mapped that way and both directions are proved against DRAM:
 *   read:  the CPU stores new values over lines DRAM still holds old (seen through an uncached mapping), the GPU copies the page,
 *          and DRAM's copy must hold the new values, which only the CPU's caches had;
 *   write: the CPU keeps clean lines of a page it read, the GPU copies other values over it, and plain cached loads must see them.
 * Only both proofs set native_adreno_io_coherent; imported IOSurfaces are then no longer cleaned and invalidated around every
 * submission. g40/g41 (forced on): the scanout blit went from 24-35 ms to 5-6 ms a frame. pong_gpu_coherent: 2 probe (default),
 * 1 force on, 0 off. Row: d<dirty lines of 64> a<attempts> r<GPU read new>/<old> w<CPU saw GPU>/<DRAM> s<status> k<stage>. */
namespace {
bool dramPage(uint64_t pa,uint32_t *out)
{
    const vm_offset_t page=ml_io_map_unmappable(vm_offset_t(pa),0x1000,0x7 /* VM_WIMG_IO */);
    if(!page)return false;
    for(unsigned i=0;i<1024;++i)out[i]=reinterpret_cast<const volatile uint32_t *>(page)[i];
    ml_io_unmap(page,0x1000);
    return true;
}
bool cpFence(PongCpBackend *device,const uint32_t *words,size_t count)
{
    uint32_t fence=0,seen=0;
    if(device->ring->submit(words,count,&fence))return false;
    return device->ring->wait(fence,&device->clock,200000,&seen)==NativeAdrenoRingFenceComplete;
}
bool gpuCopyPage(PongCpBackend *device,uint64_t src,uint64_t dst)
{
    uint32_t words[8]={};   /* CP_MEMCPY (0x75: dwords, src lo/hi, dst lo/hi), CP_WAIT_MEM_WRITES (0x12) */
    if(AdrenoPM4Type7(0x75,5,&words[0])!=AdrenoPM4Success || AdrenoPM4Type7(0x12,0,&words[6])!=AdrenoPM4Success)return false;
    words[1]=1024;words[2]=uint32_t(src);words[3]=uint32_t(src>>32);words[4]=uint32_t(dst);words[5]=uint32_t(dst>>32);
    return cpFence(device,words,7);
}
bool coherenceProbe(PongCpBackend *device,char *row,size_t cap)
{
    constexpr size_t kBytes=4*4096;
    uint32_t k[8]={};unsigned attempts=0;
    uint8_t *buf=static_cast<uint8_t *>(IOMallocAligned(kBytes,4096));
    NativeAdrenoMemory mem;uint64_t handle=0,iova=0;
    IOReturn st=buf?mem.importTask(kernel_task,uint64_t(uintptr_t(buf)),kBytes,device->output_bits,kBytes):kIOReturnNoMemory;
    k[0]=1;
    if(!st)st=device->vm->findAddress(0,kBytes,4096,&iova);
    if(!st){k[0]=2;st=device->vm->map(&mem,0,iova,3,1,3,&handle);}
    uint64_t phys[4]={};
    for(size_t j=0;!st && j<mem.spanCount();++j)
        for(size_t at=0;at<mem.spans()[j].bytes;at+=4096)phys[(mem.spans()[j].offset+at)/4096]=mem.spans()[j].physical+at;
    static uint32_t words[1024];
    bool pass=false;
    if(!st && phys[0] && phys[1] && phys[2] && phys[3]) {
        k[0]=3;
        volatile uint32_t *S=reinterpret_cast<volatile uint32_t *>(buf),*D=S+1024,*R=S+2048,*T=S+3072;
        /* read direction: S's new words must still be dirty in a CPU cache (DRAM holding the old ones, >= 48 of 64 lines) when the
         * GPU copies it, so the setup repeats until they are (sheng int-150 found them written back once and ran without coherence) */
        bool dirty=false;
        for(unsigned attempt=0;attempt<8 && !st && !dirty;++attempt) {
            attempts=attempt+1;
            for(unsigned i=0;i<1024;++i){S[i]=0x11110000u|i;D[i]=0;R[i]=0x33330000u|i;T[i]=0x44440000u|i;}
            st=NativeAdrenoMaintainOwnedPages(buf,kBytes,NativeAdrenoCleanInvalidate);
            for(unsigned i=0;i<1024;++i)S[i]=0x22220000u|i;
            __asm__ volatile("dsb sy":::"memory");
            k[1]=0;
            if(!st && dramPage(phys[0],words))for(unsigned i=0;i<1024;i+=16)k[1]+=words[i]==(0x11110000u|i);
            dirty=k[1]>=48;
        }
        if(!st && dirty) {
            k[0]=4;
            if(gpuCopyPage(device,iova,iova+4096) && dramPage(phys[1],words)) {
                k[0]=5;
                for(unsigned i=0;i<1024;++i){k[2]+=words[i]==(0x22220000u|i);k[3]+=words[i]==(0x11110000u|i);}
            }
        }
        /* write direction: T written back and kept (clean, resident), then read so its lines are present when the GPU writes */
        if(!st && k[0]==5)st=NativeAdrenoMaintainOwnedPages(const_cast<uint32_t *>(T),4096,NativeAdrenoClean);
        uint32_t touch=0;
        for(unsigned i=0;i<1024;++i)touch+=T[i];
        __asm__ volatile("dsb sy":::"memory");
        if(!st && k[0]==5 && gpuCopyPage(device,iova+8192,iova+12288)) {
            k[0]=6;
            for(unsigned i=0;i<1024;++i)k[4]+=T[i]==(0x33330000u|i);
            if(dramPage(phys[3],words))for(unsigned i=0;i<1024;++i)k[5]+=words[i]==(0x33330000u|i);
        }
        (void)touch;
        pass=k[0]==6 && k[1]>=48 && k[2]==1024 && k[4]==1024 && k[5]==1024;
    }
    k[6]=uint32_t(st);
    if(handle)(void)device->vm->unmap(handle);
    if(buf)(void)NativeAdrenoMaintainOwnedPages(buf,kBytes,NativeAdrenoCleanInvalidate);   /* no stale line outlives the free */
    (void)mem.release();
    if(buf)IOFreeAligned(buf,kBytes);
    snprintf(row,cap,"d%u/64 a%u r%u/%u w%u/%u s%x k%u",k[1],attempts,k[2],k[3],k[4],k[5],k[6],k[0]);
    return pass;
}
}
bool PongAdrenoPublishClient(PongCpBackend *device)
{
    if(backend || !device || !device->healthy())return false;
    backend=device;
    unsigned coherent=2;
    (void)PE_parse_boot_argn("pong_gpu_coherent",&coherent,sizeof(coherent));
    char row[96]="not probed";
    if(coherent==2)native_adreno_io_coherent=coherenceProbe(device,row,sizeof(row))?1:0;
    else native_adreno_io_coherent=coherent==1;
    IOLog("PONG ADRENO: IO coherence %s (pong_gpu_coherent=%u, probe %s)\n",native_adreno_io_coherent?"on":"off",coherent,row);
    const IOReturn result=native_adreno_publish(device->vm,device->submit,device->ring,
        &device->clock,device->timer,&device->info,32,device->output_bits);
    if(result)return false;
    if(!PongAdrenoConfigureService(native_adreno_published_service()))return false;
    unsigned metal=0;
    if(PE_parse_boot_argn("pong_metal",&metal,sizeof(metal)) && metal==1) {
        thread_t thread;
        if(kernel_thread_start(PongAdrenoPublishMetalAfterRoot,nullptr,&thread)!=KERN_SUCCESS)return false;
        thread_deallocate(thread);
    }
    return true;
}
