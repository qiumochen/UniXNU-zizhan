/* SPDX-License-Identifier: MIT
 * Data A Live's busiest-core/headroom/hysteresis policy, adapted to Pong's
 * EPSS LUTs and 4+3+1 topology. Register facts: NothingOSS cpufreq-hw and
 * Pong DT. No SCMI/Oryon addresses, boost modes or foreign thermal trips.
 */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
extern "C" uint64_t pong_cpu_idle_mach(unsigned int);
/* zizhan touch boost (docs/progress/2026-11-01-120hz-and-cadence.md section 8): bsd/dev/pong_fts.c counts every touch report, real
 * or pong_touch_synth. A gesture's first frames were computed at the idle clock while the load windows ramped (at most doubling
 * per 32 ms): SpringBoard's main thread ran 200-300 ms before an app-open zoom moved. As data-a-live's fuxi_perf (0097) does on
 * input, every domain is held at its cap from the first report until pong_cpu_boost_ms (default 400, 0 = off) after the last;
 * the caps are the policy's own and no boost row is used. */
extern "C" volatile uint32_t pong_touch_activity __attribute__((weak));
/* zizhan L3 (docs/progress/2026-11-01-120hz-and-cadence.md section 12): nothing voted the EPSS L3, the CPUs' shared cache clock.
 * Mainline osm-l3.c "qcom,epss-l3" (sm8450.dtsi interconnect@17d90000), as UniXNU-sheng package/CpuFreqProbe.cpp votes it on
 * SM8550: LUT at 0x100 in the domains' row format, vote at 0x90; no other offset is touched. The boot loader leaves level 13
 * (1593 MHz of 300-1804). pong_l3=0 leaves that, 1 (default) votes the top level once, 2 follows the fastest domain (the lowest
 * level at or above that domain's share of its cap times the L3 top, the top while touch-boosted), as mainline's CPU OPP tables
 * scale the L3 with the CPU clock. Synthetic app opens (g173 mode 2, g174 the top): return-home gap median 116 vs 58 ms, moving
 * 47.5 vs 54.3 fps; the following mode spends most of the time below the boot level.
 * pong_cpu_probe=1 measures, once at start and bound to cpu 0, 4 and 7 in turn, the clock (dependent adds) and load-to-use
 * latency (pointer chase) with the L3 as booted and again at its top. */
extern "C" processor_t cpu_to_processor(int cpu);
extern "C" processor_t thread_bind(processor_t processor);
namespace PongCpuPolicy {
struct Domain {
    unsigned first,count,cap_khz;
    IOMemoryMap *map=nullptr;
    volatile uint32_t *regs=nullptr,*pdmem=nullptr;
    unsigned khz[40]{},levels=0,down=0;
    bool live=false;
};
static Domain domains[]={{0,4,1804800},{4,3,2496000},{7,1,2995200}};
static Domain l3{0,0,0};
static IOMemoryMap *pdmem_map;
static unsigned stateKhz(const Domain &d){return (d.regs[0x20/4]&255)*19200u;}
static bool prepareL3()
{
    auto memory=IODeviceMemory::withRange(0x17d90000ull,4096);
    if(!memory)return false;
    l3.map=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
    if(!l3.map)return false;
    l3.regs=reinterpret_cast<volatile uint32_t *>(l3.map->getVirtualAddress());
    unsigned previous=0;
    for(unsigned i=0;i<40;++i) {
        const uint32_t row=l3.regs[(0x100+4*i)/4];
        const unsigned khz=(row>>30)?(row&255)*19200u:300000u;
        if(i && khz==previous)break;
        if(khz<300000 || khz>3200000 || (i && khz<previous))return false;
        l3.khz[i]=previous=khz;l3.levels=i+1;
    }
    if(l3.levels<2)return false;
    l3.cap_khz=l3.khz[l3.levels-1];
    IOLog("Pong CPU: L3 enable %u LUT=%u %u-%u kHz, boot vote %u\n",l3.regs[0]&1,l3.levels,l3.khz[0],l3.cap_khz,l3.regs[0x90/4]);
    l3.live=(l3.regs[0]&1)!=0;
    return l3.live;
}
static void voteL3(unsigned level)
{
    if(!l3.live || level>=l3.levels || l3.regs[0x90/4]==level)return;
    l3.regs[0x90/4]=level;
    __asm__ volatile("dsb oshst":::"memory");
}
static void setTop(Domain &d)
{
    unsigned top=40;
    for(unsigned i=0;i<d.levels;++i)if(d.khz[i] && d.khz[i]<=d.cap_khz)top=i;
    if(top!=40 && d.regs[0x320/4]!=top){*d.pdmem=top;d.regs[0x320/4]=top;}
    d.down=0;
}
static uint64_t ticksToNs(uint64_t t){uint64_t ns=0;absolutetime_to_nanoseconds(t,&ns);return ns;}
/* load-to-use latency in tenths of ns over a random cycle of `bytes` worth of 64-byte lines */
static unsigned chase(uint64_t *buf,size_t bytes)
{
    const size_t lines=bytes/64;
    uint64_t seed=0x9e3779b97f4a7c15ull;
    for(size_t i=0;i<lines;++i)buf[i*8]=i;
    for(size_t i=lines-1;i>0;--i) {     /* Sattolo: one cycle through every line */
        seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;
        const size_t j=seed%i,t=buf[i*8];buf[i*8]=buf[j*8];buf[j*8]=t;
    }
    for(size_t i=0;i<lines;++i)buf[i*8]=(uint64_t)&buf[buf[i*8]*8];
    uint64_t *p=buf;
    for(size_t i=0;i<lines;++i)p=(uint64_t *)*p;     /* warm */
    const unsigned steps=200000;
    const uint64_t t0=mach_absolute_time();
    for(unsigned i=0;i<steps;i+=8) {
        p=(uint64_t *)*p;p=(uint64_t *)*p;p=(uint64_t *)*p;p=(uint64_t *)*p;
        p=(uint64_t *)*p;p=(uint64_t *)*p;p=(uint64_t *)*p;p=(uint64_t *)*p;
    }
    const uint64_t ns=ticksToNs(mach_absolute_time()-t0);
    __asm__ volatile(""::"r"(p));
    return unsigned(ns*10/steps);
}
/* MHz from a chain of dependent adds (one cycle each on the A510, A710 and X2) over about 5 ms */
static unsigned clockMhz()
{
    uint64_t x=0;const unsigned loops=20000;
    const uint64_t t0=mach_absolute_time();
    for(unsigned i=0;i<loops;++i)__asm__ volatile(".rept 500\n add %0, %0, #1\n .endr":"+r"(x));
    const uint64_t ns=ticksToNs(mach_absolute_time()-t0);
    return ns?unsigned(uint64_t(loops)*500*1000/ns):0;
}
static void probe()
{
    const size_t big=32u<<20;
    auto buf=static_cast<uint64_t *>(IOMallocAligned(big,16384));
    if(!buf){IOLog("Pong CPU probe: no buffer\n");return;}
    static const int cpus[]={0,4,7};
    static const size_t sizes[]={32u<<10,256u<<10,3u<<20,32u<<20};
    for(unsigned pass=0;pass<2;++pass) {
        if(pass)voteL3(l3.levels-1);
        for(int cpu:cpus) {
            processor_t processor=cpu_to_processor(cpu);
            if(!processor)continue;
            thread_bind(processor);thread_block(THREAD_CONTINUE_NULL);
            Domain *dom=nullptr;for(auto &d:domains)if(d.live && unsigned(cpu)>=d.first && unsigned(cpu)<d.first+d.count)dom=&d;
            if(dom)setTop(*dom);
            __asm__ volatile("dsb oshst":::"memory");
            uint64_t warm=0;nanoseconds_to_absolutetime(100000000ull,&warm);   /* 100 ms busy: the clock settles */
            for(const uint64_t until=mach_absolute_time()+warm;mach_absolute_time()<until;)clockMhz();
            const unsigned mhz=clockMhz(),state=dom?stateKhz(*dom)/1000:0;
            unsigned lat[4];for(unsigned i=0;i<4;++i)lat[i]=chase(buf,sizes[i]);
            IOLog("Pong CPU probe: L3 vote %u (%u MHz) cpu%d %u MHz (state %u): latency 32K %u.%u 256K %u.%u 3M %u.%u 32M %u.%u ns\n",
                l3.live?l3.regs[0x90/4]:99,l3.live && l3.regs[0x90/4]<l3.levels?l3.khz[l3.regs[0x90/4]]/1000:0,cpu,mhz,state,
                lat[0]/10,lat[0]%10,lat[1]/10,lat[1]%10,lat[2]/10,lat[2]%10,lat[3]/10,lat[3]%10);
        }
    }
    thread_bind(PROCESSOR_NULL);thread_block(THREAD_CONTINUE_NULL);
    IOFreeAligned(buf,big);
}
static void stop() __attribute__((noreturn));
static void stop()
{thread_terminate(current_thread());thread_block(THREAD_CONTINUE_NULL);__builtin_unreachable();}
static bool prepare(Domain &d,unsigned index)
{
    auto memory=IODeviceMemory::withRange(0x17d91000ull+index*4096,4096);
    if(!memory)return false;
    d.map=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
    if(!d.map)return false;
    d.regs=reinterpret_cast<volatile uint32_t *>(d.map->getVirtualAddress());
    unsigned previous=0;
    for(unsigned i=0;i<40;++i) {
        const uint32_t row=d.regs[(0x100+4*i)/4];
        const unsigned src=row>>30,cores=(row>>16)&15;
        const unsigned khz=src?(row&255)*19200u:300000u;
        if(i && khz==previous)break;
        if(khz<300000 || khz>3200000 || (i && khz<previous))return false;
        previous=khz;
        // DT max_cores=4 in all three frequency domains. Different core
        // counts mark boost rows; preserve their LUT indices but skip them.
        if(cores==4 && khz<=d.cap_khz)d.khz[i]=khz;
        d.levels=i+1;
    }
    const unsigned current=d.regs[0x320/4];
    if(d.levels<2 || current>=d.levels)return false;
    bool usable=false;for(unsigned i=0;i<d.levels;++i)usable|=d.khz[i]!=0;
    if(!usable)return false;
    d.pdmem=reinterpret_cast<volatile uint32_t *>(pdmem_map->getVirtualAddress()+0x804+4*index);
    d.live=true;
    IOLog("Pong CPU: EPSS domain%u CPU%u-%u LUT=%u cap=%u kHz, hardware limits retained\n",
        index,d.first,d.first+d.count-1,d.levels,d.cap_khz);
    return true;
}
static void worker(void *,wait_result_t)
{
    IOSleep(3000);
    auto memory=IODeviceMemory::withRange(0x17d09000,4096);
    if(memory){pdmem_map=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();}
    if(!pdmem_map){IOLog("Pong CPU: EPSS skipped: PDMEM unavailable\n");stop();}
    unsigned live=0;for(unsigned i=0;i<3;++i)live+=prepare(domains[i],i);
    if(!live){IOLog("Pong CPU: EPSS skipped: no valid native LUT\n");stop();}
    unsigned l3Mode=1;PE_parse_boot_argn("pong_l3",&l3Mode,sizeof l3Mode);
    if(!prepareL3())IOLog("Pong CPU: L3 not voted (unmapped, invalid LUT or disabled)\n");
    unsigned probeOn=0;PE_parse_boot_argn("pong_cpu_probe",&probeOn,sizeof probeOn);
    if(probeOn)probe();
    if(l3Mode==1)voteL3(l3.levels-1);
    if(!l3Mode)l3.live=false;
    uint64_t idle[8];for(unsigned c=0;c<8;++c)idle[c]=pong_cpu_idle_mach(c);
    uint64_t stamp=mach_absolute_time();
    unsigned boostMs=400;PE_parse_boot_argn("pong_cpu_boost_ms",&boostMs,sizeof boostMs);
    uint64_t boostTicks=0,boostUntil=0,boosts=0,boostedTicks=0,reportAt=0,stateSum[3]{},stateN=0,l3Sum=0,l3N=0;nanoseconds_to_absolutetime(uint64_t(boostMs)*1000000ull,&boostTicks);
    uint32_t touches=&pong_touch_activity?pong_touch_activity:0;
    for(unsigned tick=0;;++tick) {
        // 8 ms: a touch is seen within a frame or so; the load policy below still runs every 32 ms
        IOSleep(8);
        if(boostMs && &pong_touch_activity && pong_touch_activity!=touches) {
            touches=pong_touch_activity;
            const uint64_t at=mach_absolute_time();
            if(at>=boostUntil) {
                ++boosts;
                for(auto &d:domains)if(d.live)setTop(d);
                if(l3Mode==2)voteL3(l3.levels-1);
                __asm__ volatile("dsb oshst":::"memory");
            }
            boostUntil=at+boostTicks;
        }
        if(tick%4)continue;
        const uint64_t now=mach_absolute_time(),elapsed=now-stamp;stamp=now;
        if(!elapsed)continue;
        const bool boosted=now<boostUntil;
        if(boosted) {
            boostedTicks+=elapsed;++stateN;
            for(unsigned i=0;i<3;++i)if(domains[i].live)stateSum[i]+=stateKhz(domains[i])/1000;
        }
        if(l3.live && l3.regs[0x90/4]<l3.levels){l3Sum+=l3.khz[l3.regs[0x90/4]]/1000;++l3N;}
        if(now>=reportAt) {
            if(reportAt && boosts) {
                /* state: the clock each domain runs (0x20, which also drops while a whole cluster idles), averaged over boosted windows */
                IOLog("Pong CPU: touch boosts %llu, %llu ms boosted in 30 s (pong_cpu_boost_ms=%u), state while boosted %llu/%llu/%llu MHz, L3 vote avg %llu MHz\n",
                    boosts,ticksToNs(boostedTicks)/1000000ull,boostMs,stateN?stateSum[0]/stateN:0,stateN?stateSum[1]/stateN:0,
                    stateN?stateSum[2]/stateN:0,l3N?l3Sum/l3N:0);
            }
            boosts=0;boostedTicks=0;stateN=0;l3Sum=0;l3N=0;for(auto &v:stateSum)v=0;
            nanoseconds_to_absolutetime(30000000000ull,&reportAt);reportAt+=now;
        }
        unsigned busy[8]{};bool valid[8]{};
        for(unsigned c=0;c<8;++c) {
            const uint64_t value=pong_cpu_idle_mach(c);
            if(value!=UINT64_MAX && idle[c]!=UINT64_MAX && value>=idle[c]) {
                const uint64_t delta=value-idle[c];
                busy[c]=delta>=elapsed?0u:unsigned(100-(delta*100/elapsed));valid[c]=true;
            }
            idle[c]=value;
        }
        for(auto &d:domains)if(d.live) {
            unsigned load=0;bool sampled=false;
            for(unsigned c=d.first;c<d.first+d.count;++c)if(valid[c]) {
                sampled=true;if(busy[c]>load)load=busy[c];
            }
            if(!sampled)continue;
            const unsigned current=d.regs[0x320/4];
            if(current>=d.levels){d.live=false;continue;}
            unsigned freq=d.khz[current];if(!freq)freq=d.cap_khz;
            uint64_t requested=uint64_t(freq)*load*125/10000;
            if(load>=90)requested=uint64_t(freq)*2;
            if(boosted)requested=d.cap_khz;   /* hold the cap until the touch boost ends */
            unsigned cap=d.cap_khz;
            if(d.regs[0x30c/4]&4) {
                const unsigned hardware=(d.regs[0x20/4]&255)*19200u;
                if(hardware && hardware<cap)cap=hardware;
            }
            unsigned selected=40,last=40;
            for(unsigned i=0;i<d.levels;++i)if(d.khz[i] && d.khz[i]<=cap) {
                last=i;if(d.khz[i]>=requested){selected=i;break;}
            }
            if(selected==40)selected=last;
            if(selected==40 || selected==current){d.down=0;continue;}
            if(d.khz[selected]<freq && ++d.down<4)continue;
            d.down=0;
            // Voltage and thermal ceilings remain firmware-controlled.
            // Request only real, non-boost LUT indices through perf-lock.
            *d.pdmem=selected;d.regs[0x320/4]=selected;
            __asm__ volatile("dsb oshst":::"memory");
        }
        if(l3Mode==2 && l3.live) {
            uint64_t want=l3.cap_khz;
            if(!boosted) {
                unsigned share=0;   /* per mille of its cap, fastest domain */
                for(auto &d:domains)if(d.live) {
                    const unsigned current=d.regs[0x320/4];
                    const unsigned khz=current<d.levels && d.khz[current]?d.khz[current]:d.cap_khz;
                    const unsigned s=unsigned(uint64_t(khz)*1000/d.cap_khz);if(s>share)share=s;
                }
                want=uint64_t(l3.cap_khz)*share/1000;
            }
            unsigned level=l3.levels-1;
            for(unsigned i=0;i<l3.levels;++i)if(l3.khz[i]>=want){level=i;break;}
            voteL3(level);
        }
    }
}
static void start()
{
    unsigned enabled=1;PE_parse_boot_argn("pong_cpu_policy",&enabled,sizeof enabled);
    if(!enabled)return;
    thread_t thread=THREAD_NULL;
    kern_return_t result=kernel_thread_start(worker,nullptr,&thread);
    if(result==KERN_SUCCESS)thread_deallocate(thread);
    else IOLog("Pong CPU: optional EPSS worker unavailable %d\n",result);
}
}
