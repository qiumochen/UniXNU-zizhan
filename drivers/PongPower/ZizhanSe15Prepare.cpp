/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include "ZizhanAmpSha256.h"
#include "ZizhanSe15Setup.h"
#include "ZizhanGeniTiming.h"
#include "ZizhanCs35l41.h"
#include "ZizhanAmpReset.h"
#include "ZizhanAudioSilent.h"
#include "ZizhanAudioSck.h"
#include "ZizhanAudioDataPin.h"
namespace {
extern "C" unsigned zizhan_cs35l41_run_size(void);
struct Page {
    IOMemoryMap *map = nullptr;
    volatile uint8_t *va = nullptr;
    bool writable = false;
    bool open(uint64_t base, uint64_t bytes, bool write = false) {
        writable = write;
        auto memory = IODeviceMemory::withRange(base, bytes);
        const IOOptionBits flags = kIOMapAnywhere | kIOMapInhibitCache | (write ? 0 : kIOMapReadOnly);
        map = memory ? memory->map(flags) : nullptr;
        if (memory) memory->release();
        va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
        return va != nullptr;
    }
    uint32_t read(unsigned at) {
        uint32_t value = *reinterpret_cast<volatile uint32_t *>(va + at);
        __asm__ volatile("dmb oshld" ::: "memory"); return value;
    }
    void write(unsigned at, uint32_t value) {
        if (!writable) return;
        *reinterpret_cast<volatile uint32_t *>(va + at) = value;
        __asm__ volatile("dsb oshst" ::: "memory");
    }
    ~Page() { if (map) map->release(); }
};

/* zizhan_audio_pinsample=1 (dry diagnostics): read the GPIO_IN bit of the TDM pads (121 sck, 123 ws, 124 dout) in a tight
 * loop while the stream runs. Counts ones and level changes per pad: a stuck dout with a toggling sck means the SoC drives no data. */
namespace zizhan_pin_sample {
static Page *pads[4];
static uint32_t ones[4],edges[4],samples,first[4][2];
static uint64_t elapsed_ns;
static void run(){
    if(!pads[0]||!pads[1]||!pads[2])return;
    const unsigned count=pads[3]?4u:3u;uint32_t last[4]={2,2,2,2};
    const uint64_t t0=mach_absolute_time();
    for(uint32_t n=0;n<20000u;++n){
        for(unsigned p=0;p<count;++p){
            const uint32_t bit=pads[p]->read(4)&1u;
            if(n<64u&&bit)first[p][n>>5]|=1u<<(n&31u);
            ones[p]+=bit;if(last[p]!=2u&&bit!=last[p]){++edges[p];}last[p]=bit;}
        ++samples;}
    absolutetime_to_nanoseconds(mach_absolute_time()-t0,&elapsed_ns);
}
}
uint64_t nowUs() { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000; }
int ampHash(const uint8_t *bytes,unsigned length,uint8_t digest[32]) {
    zizhan_amp_sha256(bytes,length,digest);return 0;
}
struct RamInput {
    uint8_t *bytes=nullptr;
    unsigned size=0;
    bool open(const char *property="zizhan-cs35l41-protection",unsigned length=ZizhanAmpRamFileBytes) {
        auto chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
        auto data=chosen?OSDynamicCast(OSData,chosen->getProperty(property)):nullptr;
        if(data&&data->getLength()==length){
            size=length;bytes=static_cast<uint8_t *>(IOMalloc(size));
            if(bytes)memcpy(bytes,data->getBytesNoCopy(),size);
        }
        if(chosen){chosen->release();}return bytes!=nullptr;
    }
    ~RamInput(){if(bytes)IOFree(bytes,size);}
};
extern "C" int pong_klog_flush(int reason) __attribute__((weak));
// zizhan_audio_checkpoint=1 (diagnostics only): log each pulse stage and copy the console to the UFS klog
// synchronously (reason 6), so a hang that ends in the cold watchdog bite still names its last stage.
void amp_checkpoint(unsigned chip,unsigned stage){
    const uint64_t begin=nowUs();
    IOLog("ZIZHAN_AUDIO_CHECKPOINT: chip=%u stage=%u\n",chip,stage);
    const int flushed=pong_klog_flush?pong_klog_flush(6):-1;
    IOLog("ZIZHAN_AUDIO_CHECKPOINT: chip=%u stage=%u flushed=%d us=%llu\n",chip,stage,flushed,(unsigned long long)(nowUs()-begin));
}
void prepare(unsigned mode,ZizhanAudioSilentReceipt *combined=nullptr,const ZizhanAudioTransfer *transfer=nullptr) {
    using namespace zizhan_se15_setup;
    thread_set_thread_name(current_thread(), "zizhan_se15_prep");
    const bool header=mode==14,capture_mpu=mode==18||mode==25||mode==33,observed_mpu=mode==19;
    const bool run=mode>=17,coefficients=mode>=16,burst=mode>=15,ram=mode>=13;
    const bool trim=mode>=12,b2_patch=mode>=11,owned_snapshot=mode>=10,config_observe=mode>=9;
    const bool source_handoff=mode>=8,rx_setup=mode>=7,handoff=mode>=6,safeoff=mode>=5;
    const bool snapshot=mode>=4,identity=mode>=3,timing=mode>=2;
    if((mode>=35&&mode<=39)&&(!combined||!transfer||!transfer->once)){IOLog("ZIZHAN_AUDIO_TRANSFER: SKIP missing-zero-callback writes=0\n");return;}
    // Mode 40 (PRM 41): profile 19 dual-amp playback window, fed by the transfer's stream callback.
    if(mode==40&&(!combined||!transfer||!transfer->stream)){IOLog("ZIZHAN_AUDIO_PLAY: SKIP missing-stream-callback writes=0\n");return;}
    if(mode==39||mode==40){unsigned attended=0,dry=0,nopup=0,lvl=0;PE_parse_boot_argn("zizhan_pulse_dry",&dry,sizeof dry);PE_parse_boot_argn("zizhan_pulse_nopup",&nopup,sizeof nopup);
        PE_parse_boot_argn("zizhan_amp_lvl",&lvl,sizeof lvl);
        zizhan_amp_pulse_flags=(dry==1?1u:0u)|(nopup==1?2u:0u)|(mode==40&&lvl==1?4u:0u);
        if(zizhan_amp_pulse_flags&4u){IOLog("ZIZHAN_AUDIO_PULSE: android_level=1 volume=0dB gain=0x253\n");}if(zizhan_amp_pulse_flags)IOLog("ZIZHAN_AUDIO_PULSE: flags dry=%u nopup=%u\n",zizhan_amp_pulse_flags&1u,(zizhan_amp_pulse_flags>>1)&1u);
        if((!PE_parse_boot_argn("zizhan_audio_attended",&attended,sizeof attended)||attended!=1)&&dry!=1){IOLog("ZIZHAN_AUDIO_PULSE: SKIP attended=0 output_enables=0\n");return;}
        unsigned checkpoint=0;if(PE_parse_boot_argn("zizhan_audio_checkpoint",&checkpoint,sizeof checkpoint)&&checkpoint==1){
            zizhan_amp_checkpoint=amp_checkpoint;IOLog("ZIZHAN_AUDIO_CHECKPOINT: armed klog=%u\n",pong_klog_flush?1u:0u);}
        unsigned hold=0;if(mode==39&&PE_parse_boot_argn("zizhan_pulse_hold",&hold,sizeof hold)&&hold>=1&&hold<=300){zizhan_amp_pulse_hold_us=hold*1000u;IOLog("ZIZHAN_AUDIO_PULSE: hold_ms=%u\n",hold);}
        unsigned play=0;if(mode==40){if(!PE_parse_boot_argn("zizhan_audio_play_ms",&play,sizeof play)||play<100||play>2000){IOLog("ZIZHAN_AUDIO_PLAY: SKIP play_ms=%u needs=100..2000\n",play);return;}
            zizhan_amp_play_ms=play;IOLog("ZIZHAN_AUDIO_PLAY: armed play_ms=%u dry=%u\n",play,zizhan_amp_pulse_flags&1u);
            unsigned echo=0;zizhan_amp_echo=(dry==1&&PE_parse_boot_argn("zizhan_audio_echo",&echo,sizeof echo)&&echo==1)?1u:0u;
            if(zizhan_amp_echo)IOLog("ZIZHAN_AUDIO_ECHO: armed dry=1 top_asptx1=asprx1 din=gpio122\n");
            unsigned dump=0;zizhan_amp_dump_on=(dry==1&&PE_parse_boot_argn("zizhan_audio_ampdump",&dump,sizeof dump)&&dump==1)?1u:0u;
            if(zizhan_amp_dump_on)IOLog("ZIZHAN_AMP_DUMP: armed dry=1 registers=228\n");
            unsigned ps=0;if(dry==1&&PE_parse_boot_argn("zizhan_audio_pinsample",&ps,sizeof ps)&&ps==1){zizhan_play_pin_sample=zizhan_pin_sample::run;IOLog("ZIZHAN_PIN_SAMPLE: armed dry=1 pads=121,123,124\n");}}}
    ZizhanAmpSpeakerCalibration speaker_cal={1,sizeof(ZizhanAmpSpeakerCalibration),{0,0}};
    if(mode>=29){
        const bool top_present=PE_parse_boot_argn("zizhan_cal_t",&speaker_cal.resistance[0],sizeof(uint32_t));
        const bool bottom_present=PE_parse_boot_argn("zizhan_cal_b",&speaker_cal.resistance[1],sizeof(uint32_t));
        if(!top_present||!bottom_present||!speaker_cal.resistance[0]||!speaker_cal.resistance[1]||
           speaker_cal.resistance[0]>=0xffffffu||speaker_cal.resistance[1]>=0xffffffu){
            IOLog("ZIZHAN_AUDIO_PRERUN: SKIP missing-or-unrepresentable-cached-input amp_mmio=0 calibration_run=0\n");return;}
    }
    RamInput input,top,bottom;
    if(ram&&!input.open()){IOLog("ZIZHAN_AMP_RAM: SKIP missing-private-boot-input writes=0 finished=1\n");return;}
    if(coefficients&&(!top.open("zizhan-cs35l41-prot-t",5060)||!bottom.open("zizhan-cs35l41-prot-b",5148))){
        IOLog("ZIZHAN_AMP_COEFF: SKIP missing-private-input writes=0 finished=1\n");return;
    }
    Page votes, gates, core, sda, scl, reset,sck,ws,data_pin,din_pin;
    zizhan_audio_sck::State sck_state,ws_state;
    zizhan_audio_data_pin::State data_state,din_state;
    if (!votes.open(0x162000, 0x1000, true) || !gates.open(0x12e000, 0x1000) ||
        !core.open(0x133000, 0x1000) || !sda.open(0xf138000, 0x1000, true) ||
        !scl.open(0xf139000, 0x1000, true) || !reset.open(0xf101000, 0x1000, identity)) {
        IOLog("ZIZHAN_SE15_PREP: FAIL map writes=0 finished=1\n"); return;
    }
    State s;
    const uint32_t reset_config = reset.read(0), reset_io = reset.read(4);
    // Prior device evidence: GPIO1 input low. Do not set up a bus against a
    // newly changed/released shared amplifier reset or an unknown pin owner.
    if ((reset_config & 0x23c) != 0 || (reset_io & 3) != 0) {
        IOLog("ZIZHAN_SE15_PREP: SKIP reset-state-changed config=%x io=%x writes=0 finished=1\n", reset_config, reset_io); return;
    }
    Result result = clocks(votes, gates, core, s, nowUs, [](unsigned us) { IODelay(us); });
    IOLog("ZIZHAN_SE15_PREP: clocks result=%u votes=%x/%x writes=%u retained=%u\n",
          unsigned(result), s.original_votes, s.observed_votes, s.writes, unsigned(s.retained));
    if (result != Ok) { IOLog("ZIZHAN_SE15_PREP: FAIL clocks se_mapped=0 finished=1\n"); return; }
    zizhan_geni::Receipt receipt;
    if (timing) {
        const auto selection = zizhan_geni::select(gates, receipt);
        IOLog("ZIZHAN_GENI: clock-select result=%u root=%x cfg=%x dfs=%x level=%u selected=%x\n",
              unsigned(selection), receipt.root, receipt.current, receipt.dfs, receipt.level, receipt.selected);
        if (selection != zizhan_geni::Ok) { IOLog("ZIZHAN_GENI: SKIP no-qualified-xo se_writes=0 finished=1\n"); return; }
    }
    Page se;
    if (!se.open(0x880000, 0x4000, timing)) { IOLog("ZIZHAN_SE15_PREP: FAIL se-map retained=1 finished=1\n"); return; }
    uint32_t fw = 0, status = 0, irq = 0;
    result = engine(se, fw, status, irq);
    IOLog("ZIZHAN_SE15_PREP: engine result=%u fw=%x status=%x irq=%x root=%x cfg=%x\n",
          unsigned(result), fw, status, irq, gates.read(0x14), gates.read(0x18));
    if (result != Ok) {
        IOLog("ZIZHAN_SE15_PREP: SKIP engine-not-idle-i2c pin_writes=0 i2c_commands=0 finished=1\n"); return;
    }
    result = pins(sda, scl, s);
    const bool unchanged = reset.read(0) == reset_config && reset.read(4) == reset_io;
    IOLog("ZIZHAN_SE15_PREP: %s bus-setup result=%u pins=%x/%x writes=%u retained=%u reset_unchanged=%u i2c_commands=0 amp_identity=UNTESTED finished=%u\n",
          result == Ok && unchanged ? "PASS" : "FAIL", unsigned(result), sda.read(0), scl.read(0),
          s.writes, unsigned(s.retained), unsigned(unchanged), timing ? 0u : 1u);
    if (timing && result == Ok && unchanged) {
        const auto prepared = zizhan_geni::prepare(se, receipt);
        const bool reset_unchanged = reset.read(0) == reset_config && reset.read(4) == reset_io;
        IOLog("ZIZHAN_GENI: %s timing-fifo result=%u tx=%x rx=%x clk=%x scl=%x route=%x retained=%u reset_unchanged=%u i2c_commands=0 amp_identity=UNTESTED finished=%u\n",
              prepared == zizhan_geni::Ok && reset_unchanged ? "PASS" : "FAIL", unsigned(prepared),
              receipt.tx_parameter, receipt.rx_parameter, se.read(GENI_CLK_CFG), se.read(GENI_SCL_COUNTERS),
              se.read(GENI_SE_IRQ_EN), unsigned(receipt.retained), unsigned(reset_unchanged),
              identity && prepared == zizhan_geni::Ok && reset_unchanged ? 0u : 1u);
        if (identity && prepared == zizhan_geni::Ok && reset_unchanged) {
            if (rx_setup) {
                zizhan_geni::RxReceipt rx;
                const auto rx_result = zizhan_geni::prepare_rx(se, receipt, rx);
                IOLog("ZIZHAN_GENI_RX: %s result=%u tx=%x rx=%x watermark=%x/%x rfr=%x/%x writes=%u i2c_commands=0 reset_unchanged=1 finished=1\n",
                      rx_result == zizhan_geni::Ok ? "PASS" : "FAIL", unsigned(rx_result), rx.tx_parameter, rx.rx_parameter,
                      rx.watermark_before, rx.watermark_after, rx.rfr_before, rx.rfr_after, rx.writes);
                if (rx_result != zizhan_geni::Ok) return;
            }
            zizhan_amp_reset::State reset_state;
            if(combined)combined->release_safe=0;
            const auto reset_result = zizhan_amp_reset::release(reset, reset_state, [](unsigned us) { IODelay(us); });
            IOLog("ZIZHAN_AMP: reset result=%u claimed=%u released=%u\n", unsigned(reset_result),
                  unsigned(reset_state.claimed), unsigned(reset_state.released));
            // 11-18 (n18c-p-1 panic "Invalid kernel stack pointer (probable overflow)"): these ~5 KiB of
            // receipts sat on the 16 KiB kernel stack under the PRM worker frame; an IOLog at an ADSP WRITE
            // that took the os_log -> kqueue wakeup path then ran off the stack, usually ending as a cold
            // watchdog hang. prepare() runs at most once at a time (the PRM and SE15 entries are exclusive
            // and latched per boot), so keep them in static storage and clear them on every entry.
            static struct Receipts {
                ZizhanAmpIdentity observed; ZizhanAmpSnapshot captured; ZizhanAmpSafeOff disabled;
                ZizhanAmpHandoff transferred; ZizhanAmpReadiness ready; ZizhanAmpB2Patch patched;
                ZizhanAmpTrim calibrated; ZizhanAmpRam loaded; ZizhanAmpCoefficients configured;
                ZizhanAmpRun executed; ZizhanAmpOutputSetup output_setup; ZizhanAmpBusConfig before_config, after_config;
            } receipts;
            memset(&receipts, 0, sizeof receipts);
            auto &observed = receipts.observed; auto &captured = receipts.captured; auto &disabled = receipts.disabled;
            auto &transferred = receipts.transferred; auto &ready = receipts.ready; auto &patched = receipts.patched;
            auto &calibrated = receipts.calibrated; auto &loaded = receipts.loaded; auto &configured = receipts.configured;
            auto &executed = receipts.executed; auto &output_setup = receipts.output_setup;
            auto &before_config = receipts.before_config; auto &after_config = receipts.after_config;
            if (reset_result == zizhan_amp_reset::Ok) {
                GeniI2c bus = {};
                bus.io = {[](void *p, uint32_t at) { return static_cast<Page *>(p)->read(at); },
                          [](void *p, uint32_t at, uint32_t value) { static_cast<Page *>(p)->write(at, value); }, &se};
                const ZizhanAmpClock clock = {nullptr, [](void *) { return nowUs(); },
                                             [](void *, unsigned us) { IODelay(us); }};
                // Exclusive one-shot platform worker, exact board pins read
                // back, actual shared reset released; dummy suppliers are an
                // observed topology, NOT a PowerVerified flag. Both amps must
                // respond with OTP boot done/no error before ID reads.
                const unsigned flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
                    ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
                if (safeoff) {
                    zizhan_cs35l41_safe_off(1, flags, &bus, &clock, &disabled);
                    observed = disabled.identity;
                }
                if (config_observe && disabled.status == ZizhanAmpPass)
                    zizhan_cs35l41_bus_config(1, flags, &bus, &before_config);
                if (handoff && !owned_snapshot && disabled.status == ZizhanAmpPass) {
                    if (source_handoff) zizhan_cs35l41_source_handoff(1, flags, &bus, &clock, &disabled, &transferred);
                    else zizhan_cs35l41_handoff(1, flags, &bus, &clock, &disabled, &transferred);
                }
                if (config_observe && disabled.status == ZizhanAmpPass)
                    zizhan_cs35l41_bus_config(1, flags, &bus, &after_config);
                if (owned_snapshot && disabled.status == ZizhanAmpPass) {
                    zizhan_cs35l41_owned_snapshot(1, flags, &bus, &clock, &disabled, &ready, &captured);
                    if (b2_patch && captured.status == ZizhanAmpPass)
                        zizhan_cs35l41_b2_patch(1, flags, &bus, &clock, &captured, &patched);
                    if (trim && patched.status == ZizhanAmpPass)
                        zizhan_cs35l41_trim(1, flags, &bus, &clock, &captured, &patched, &calibrated);
                    if (ram && calibrated.status == ZizhanAmpPass) {
                        if(mode>=33&&mode<=40)zizhan_cs35l41_ram_native_init(1,flags,&bus,&clock,&captured,&calibrated,input.bytes,ZizhanAmpRamFileBytes,ampHash,&loaded);
                        else if(header)zizhan_cs35l41_ram_header(1,flags,&bus,&clock,&captured,&calibrated,input.bytes,ZizhanAmpRamFileBytes,ampHash,&loaded);
                        else if(burst)zizhan_cs35l41_ram_burst(1,flags,&bus,&clock,&captured,&calibrated,input.bytes,ZizhanAmpRamFileBytes,ampHash,&loaded);
                        else zizhan_cs35l41_ram(1,flags,&bus,&clock,&captured,&calibrated,input.bytes,ZizhanAmpRamFileBytes,ampHash,&loaded);
                    }
                    if(coefficients&&loaded.status==ZizhanAmpPass)
                        zizhan_cs35l41_coefficients(1,flags,&bus,&clock,&loaded,top.bytes,top.size,bottom.bytes,bottom.size,ampHash,&configured);
                    if(mode>=36&&configured.status==ZizhanAmpPass){
                        zizhan_cs35l41_output_setup(1,flags,&bus,&clock,&configured,&output_setup);
                        if(combined)combined->output_configured=output_setup.status==ZizhanAmpPass?1u:0u;
                    }
                    if(run&&configured.status==ZizhanAmpPass&&(mode<36||output_setup.status==ZizhanAmpPass)){
                        if(combined){
                            if(sck.open(0xf179000,0x1000,true)&&zizhan_audio_sck::connect(sck,sck_state)){
                                combined->pin_connected=1;
                                IOLog("ZIZHAN_AUDIO_SILENT: sck pin=121 config=%x io=%x lease_held=1 outputs_off=1\n",sck.read(0),sck.read(4));
                                if(mode>=27){
                                    if(ws.open(0xf17b000,0x1000,true)&&zizhan_audio_sck::connect_ws(ws,ws_state)){
                                        IOLog("ZIZHAN_AUDIO_FRAME: ws pin=123 config=%x io=%x no_data_pads=1\n",ws.read(0),ws.read(4));
                                        if(mode>=35&&mode<=40){
                                            bool both_off=true;for(unsigned c=0;c<2;++c)if((configured.after_state[c][0]&1)||(configured.after_state[c][1]&0x31)||configured.after_state[c][2])both_off=false;
                                            if(both_off&&data_pin.open(0xf17c000,0x1000,true)&&zizhan_audio_data_pin::connect(data_pin,data_state)){
                                                combined->data_connected=1;
                                                IOLog("ZIZHAN_AUDIO_DATA_PIN: connected=1 pin=124 config=%x io=%x original=%x/%x native_ALT1_8mA=1 latch_writes=0 outputs_off=1\n",data_pin.read(0),data_pin.read(4),data_state.config,data_state.io);
                                                if(mode==40&&zizhan_amp_echo){const bool din=din_pin.open(0xf17a000,0x1000,true)&&zizhan_audio_data_pin::connect(din_pin,din_state);
                                                    IOLog("ZIZHAN_AUDIO_ECHO: din pin=122 connected=%u config=%x original=%x\n",din?1u:0u,din?din_pin.read(0):0u,din_state.config);
                                                    if(!din){zizhan_amp_echo=0;}}
                                                if((mode==39||mode==40)&&zizhan_amp_checkpoint)zizhan_amp_checkpoint(0,0);
                                                if(mode==40){zizhan_pin_sample::pads[0]=&sck;zizhan_pin_sample::pads[1]=&ws;zizhan_pin_sample::pads[2]=&data_pin;zizhan_pin_sample::pads[3]=zizhan_amp_echo?&din_pin:nullptr;}
                                                if(mode==40)IOLog("ZIZHAN_AUDIO_PLAY: call play_ms=%u flags=%x transfer=%u stream=%u run_size=%u/%u\n",zizhan_amp_play_ms,zizhan_amp_pulse_flags,transfer?1u:0u,transfer&&transfer->stream?1u:0u,(unsigned)sizeof(ZizhanAmpRun),zizhan_cs35l41_run_size());
                                                if(mode==40){zizhan_cs35l41_native_play_transfer(1,flags,&bus,&clock,&configured,&speaker_cal,transfer,&executed);
                                                if(zizhan_play_pin_sample){using namespace zizhan_pin_sample;
                                                    IOLog("ZIZHAN_PIN_SAMPLE: n=%u ns=%llu sck ones=%u edges=%u ws ones=%u edges=%u dout ones=%u edges=%u first64 sck=%08x%08x ws=%08x%08x dout=%08x%08x\n",
                                                          samples,elapsed_ns,ones[0],edges[0],ones[1],edges[1],ones[2],edges[2],first[0][1],first[0][0],first[1][1],first[1][0],first[2][1],first[2][0]);
                                                    if(pads[3])IOLog("ZIZHAN_PIN_SAMPLE: din122 ones=%u edges=%u first64=%08x%08x\n",ones[3],edges[3],first[3][1],first[3][0]);
                                                    zizhan_play_pin_sample=nullptr;pads[0]=pads[1]=pads[2]=pads[3]=nullptr;}}
                                                else if(mode==39)zizhan_cs35l41_native_pulse_transfer(1,flags,&bus,&clock,&configured,&speaker_cal,transfer,&executed);
                                                else if(mode==38)zizhan_cs35l41_native_iv_transfer(1,flags,&bus,&clock,&configured,&speaker_cal,transfer,&executed);
                                                else if(mode==37)zizhan_cs35l41_native_mailbox_transfer(1,flags,&bus,&clock,&configured,&speaker_cal,transfer,&executed);
                                                else zizhan_cs35l41_native_core_transfer(1,flags,&bus,&clock,&configured,&speaker_cal,transfer,&executed);
                                            }else IOLog("ZIZHAN_AUDIO_DATA_PIN: FAIL claim both_off=%u outputs_off=1\n",both_off?1u:0u);
                                        }
                                        else if(mode==34)zizhan_cs35l41_native_core_run(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==33)zizhan_cs35l41_native_core_capture(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==32)zizhan_cs35l41_asp_format_run(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==31)zizhan_cs35l41_asp_rx_run(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==30)zizhan_cs35l41_native_rates_run(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==29)zizhan_cs35l41_native_protection_run(1,flags,&bus,&clock,&configured,&speaker_cal,&executed);
                                        else if(mode==28)zizhan_cs35l41_native_debug_run(1,flags,&bus,&clock,&configured,&executed);
                                        else zizhan_cs35l41_native_frame_run(1,flags,&bus,&clock,&configured,&executed);
                                    }else IOLog("ZIZHAN_AUDIO_FRAME: FAIL ws-claim core_start=0\n");
                                }else if(mode>=22){
                                    unsigned high=0,edges=0,last=sck.read(4)&1;
                                    for(unsigned sample=0;sample<256;++sample){const unsigned value=sck.read(4)&1;high+=value;if(value!=last)++edges;last=value;IODelay(1+(sample&3));}
                                    IOLog("ZIZHAN_AUDIO_SCK: samples=256 high=%u changes=%u alias_possible=1 frequency_measured=0\n",high,edges);
                                    if(mode==26)zizhan_cs35l41_global_observed_run(1,flags,&bus,&clock,&configured,&executed);
                                    else if(mode==25)zizhan_cs35l41_global_mpu_capture(1,flags,&bus,&clock,&configured,&executed);
                                    else if(mode==24)zizhan_cs35l41_global_silent_run(1,flags,&bus,&clock,&configured,&executed);
                                    else if(mode==23)zizhan_cs35l41_global_pll_capture(1,flags,&bus,&clock,&configured,&executed);
                                    else zizhan_cs35l41_clock_monitor_capture(1,flags,&bus,&clock,&configured,&executed);
                                }else if(mode==21)zizhan_cs35l41_pll_capture(1,flags,&bus,&clock,&configured,&executed);
                                else zizhan_cs35l41_clocked_run(1,flags,&bus,&clock,&configured,&executed);
                            }else IOLog("ZIZHAN_AUDIO_SILENT: FAIL sck-claim no-core-start=1\n");
                        }else if(capture_mpu)zizhan_cs35l41_mpu_capture(1,flags,&bus,&clock,&configured,&executed);
                        else if(observed_mpu)zizhan_cs35l41_observed_run(1,flags,&bus,&clock,&configured,&executed);
                        else zizhan_cs35l41_run(1,flags,&bus,&clock,&configured,&executed);
                    }
                    zizhan_cs35l41_bus_config(1, flags, &bus, &after_config);
                    observed = captured.identity;
                } else if (snapshot && (!safeoff || disabled.status == ZizhanAmpPass) &&
                    (!handoff || transferred.status == ZizhanAmpPass)) {
                    zizhan_cs35l41_snapshot(1, flags, &bus, &clock, &captured);
                    observed = captured.identity;
                } else if (!safeoff) zizhan_cs35l41_boot_identify(1, flags, &bus, &clock, &observed);
            }
            const bool held_reset = zizhan_amp_reset::assert_and_retain(reset, reset_state);
            if(combined){
                combined->reset_asserted=reset_state.claimed&&held_reset;
                /* Never intentionally remove SCK while shared reset could
                 * still be released. Unknown cleanup retains clock lease. */
                combined->pin_restored=held_reset&&zizhan_audio_data_pin::restore(din_pin,din_state,held_reset)&&zizhan_audio_data_pin::restore(data_pin,data_state,held_reset)&&zizhan_audio_sck::restore(ws,ws_state)&&zizhan_audio_sck::restore(sck,sck_state);
                if(mode>=35&&mode<=40){combined->data_restored=held_reset&&data_state.restored;
                    IOLog("ZIZHAN_AUDIO_DATA_PIN: restored=%u reset_asserted=%u claimed=%u config=%x latch_writes=0 outputs_off=1\n",combined->data_restored,held_reset?1u:0u,data_state.claimed?1u:0u,data_state.claimed?data_pin.read(0):0u);
                    for(unsigned c=0;c<2;++c){combined->transfer_attempts[c]=executed.transfer_attempts[c];combined->transfer_acks[c]=executed.transfer_acks[c];
                        IOLog("ZIZHAN_AUDIO_TRANSFER: chip=%u attempted=%u acknowledged=%u halo=%x/%x heartbeat=%x/%x outputs_off=%u\n",c,executed.transfer_attempts[c],executed.transfer_acks[c],executed.transfer_halo[c][0],executed.transfer_halo[c][1],executed.transfer_heartbeat[c][0],executed.transfer_heartbeat[c][1],mode>=39?0u:1u);}}
                if(mode>=37){combined->mailbox_verified=executed.status==ZizhanAmpPass&&executed.mailbox_completed[0]==2&&executed.mailbox_completed[1]==2;
                    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<2;++i)IOLog("ZIZHAN_AUDIO_MAILBOX: chip=%u command=%u status=%x irq=%x polls=%u mask_before=%x mask_after=%x restored=%u attempted=%u acknowledged=%u completed=%u outputs_off=1\n",c,i+1,executed.mailbox_status[c][i],executed.mailbox_irq[c][i],executed.mailbox_polls[c][i],executed.mailbox_mask_before[c][i],executed.mailbox_mask_after[c][i],executed.mailbox_mask_restored[c][i],executed.mailbox_attempted[c],executed.mailbox_acknowledged[c],executed.mailbox_completed[c]);}
                if(mode>=38){combined->iv_verified=executed.status==ZizhanAmpPass;
                    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<2;++i){if(executed.iv_reads[c][i]!=3)combined->iv_verified=0;
                        IOLog("ZIZHAN_AUDIO_IV: chip=%u window=%u reads=%u pwr2=%x rx3=%x rx4=%x output_enables=0 route_only=1\n",c,i,executed.iv_reads[c][i],executed.iv_values[c][i][0],executed.iv_values[c][i][1],executed.iv_values[c][i][2]);}}
                if(mode==40)IOLog("ZIZHAN_AUDIO_PLAY: %s play_ms=%u ready=%u/%u streamed=%u polls=%u reason=%u stage=%u chip=%u torn=%u/%u dry=%u\n",
                    executed.status==ZizhanAmpPass?"PASS":"FAIL",executed.play_ms,executed.play_ready[0],executed.play_ready[1],executed.play_streamed,executed.play_polls,
                    executed.play_reason?executed.play_reason:executed.reason,executed.play_stage,executed.play_chip,executed.play_torn[0],executed.play_torn[1],zizhan_amp_pulse_flags&1u);
                if(mode==40&&(executed.echo_on||zizhan_amp_echo))IOLog("ZIZHAN_AUDIO_ECHO: amp on=%u restored=%u src=%x/%x/%x en=%x/%x/%x slots=%x din_restored=%u\n",
                    executed.echo_on,executed.echo_restored,executed.echo_src[0],executed.echo_src[1],executed.echo_src[2],executed.echo_en[0],executed.echo_en[1],executed.echo_en[2],executed.echo_slots,din_state.claimed?(din_state.restored?1u:0u):2u);
                if(mode==40&&zizhan_amp_dump_on){for(unsigned c=0;c<2;++c){IOLog("ZIZHAN_AMP_DUMP: chip=%u reads=%u\n",c,zizhan_amp_dump_reads[c]);
                    for(unsigned i=0;i+8<=228u;i+=8){const uint32_t *v=&zizhan_amp_dump_values[c][i];
                        IOLog("ZIZHAN_AMP_DUMP: c=%u i=%u %x %x %x %x %x %x %x %x\n",c,i,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);}
                    {const uint32_t *v=&zizhan_amp_dump_values[c][224];IOLog("ZIZHAN_AMP_DUMP: c=%u i=224 %x %x %x %x\n",c,v[0],v[1],v[2],v[3]);}}}
                if(mode==40){for(unsigned c=0;c<2;++c){const uint32_t *v=executed.play_snap[c];IOLog("ZIZHAN_AUDIO_PLAY_SNAP: chip=%u reads=%u 2014=%x 2018=%x 2c0c=%x 4800=%x 4808=%x 4820=%x 4c00=%x 6000=%x 6c04=%x 10018=%x 10098=%x 1009c=%x 201c=%x 2020=%x 2024=%x 4804=%x\n",c,executed.play_snap_reads[c],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]);}}
                if(mode==39||mode==40){combined->pulse_verified=executed.status==ZizhanAmpPass;
                    for(unsigned c=0;c<2;++c){const auto &p=executed.pulse[c];if(!p.complete||!p.patch_off||!p.disabled||!p.data_ack)combined->pulse_verified=0;
                        IOLog("ZIZHAN_AUDIO_PULSE: chip=%u attempted=%u acknowledged=%u pup=%u pdn=%u enabled=%u disabled=%u data_ack=%u complete=%u reason=%u enabled_us=%llu faults=%x/%x raw=%x/%x reset_asserted=%u attended=1\n",c,p.attempted,p.acknowledged,p.patch_on,p.patch_off,p.enabled,p.disabled,p.data_ack,p.complete,p.reason,p.enabled_us,p.fault_sticky[0],p.fault_sticky[1],p.fault_raw[0],p.fault_raw[1],held_reset?1u:0u);}}
                combined->release_safe=held_reset&&combined->pin_restored;
                const bool complete=executed.status==ZizhanAmpPass&&executed.completed==2&&combined->reset_asserted&&combined->pin_restored;
                combined->amp_pass=(mode==20||mode==24||mode==26||mode==27||mode==28||(mode>=29&&mode<=32)||mode==34||mode==35||mode==36||mode==37||mode==38||mode==39||mode==40)&&complete;
                combined->diagnostic_capture=((mode>=21&&mode<=23)||mode==25||mode==33)&&complete;
                if(mode==28){bool captured=executed.stopped[0]&&combined->reset_asserted&&combined->pin_restored;
                    for(unsigned slot=0;slot<6;++slot)if(executed.debug_count[0][slot]!=15)captured=false;
                    combined->diagnostic_capture=captured?1u:0u;
                    for(unsigned c=0;c<2;++c)for(unsigned slot=0;slot<6;++slot)for(unsigned i=0;i<executed.debug_count[c][slot];++i)
                        IOLog("ZIZHAN_AUDIO_COREDEBUG: chip=%u slot=%u reg=%x value=%x read_only=1\n",c,slot,
                            zizhan_cs35l41_debug_register(i),executed.debug_values[c][slot][i]);
                }
                combined->completed=executed.completed;
                for(unsigned c=0;c<2;++c){combined->samples[c]=executed.samples[c];combined->heartbeat_changes[c]=executed.heartbeat_changes[c];combined->pll_steps[c]=executed.pll_steps[c];}
                IOLog("ZIZHAN_AUDIO_SILENT: amp=%u completed=%u reset_asserted=%u pin_restored=%u release_safe=%u audio_output=0\n",
                    combined->amp_pass,combined->completed,combined->reset_asserted,combined->pin_restored,combined->release_safe);
                for(unsigned c=0;c<2;++c)IOLog("ZIZHAN_AUDIO_SILENT: chip=%u pll_before=%x pll_after=%x steps=%u samples=%u heartbeat_changes=%u\n",
                    c,executed.pll_before[c],executed.pll_current[c],executed.pll_steps[c],executed.samples[c],executed.heartbeat_changes[c]);
                if(mode>=21)for(unsigned c=0;c<2;++c){
                    IOLog("ZIZHAN_AUDIO_PLL: chip=%u samples=%u lock_samples=%u capture_only=%u core_start=%u irq_clear=0\n",c,executed.pll_samples[c],executed.pll_lock_samples[c],executed.pll_capture,executed.started[c]);
                    for(unsigned i=0;i<4;++i)IOLog("ZIZHAN_AUDIO_PLL: chip=%u index=%u sticky=%x raw=%x/%x or=%x and=%x\n",
                        c,i,executed.pll_sticky[c][i],executed.pll_raw_first[c][i],executed.pll_raw_last[c][i],executed.pll_raw_or[c][i],executed.pll_raw_and[c][i]);
                    if(mode>=22)IOLog("ZIZHAN_AUDIO_FSMON: chip=%u before=%x after=%x writes=%u detect=%x/%x or=%x\n",c,
                        executed.monitor_before[c],executed.monitor_after[c],executed.monitor_writes[c],executed.clock_detect_first[c],executed.clock_detect_last[c],executed.clock_detect_or[c]);
                    if(mode>=23)IOLog("ZIZHAN_AUDIO_GLOBAL: chip=%u on=%u off=%u active=%u asp_checks=%u asp_enable=%x amp_boost=0 core_start=%u\n",c,
                        executed.global_on[c],executed.global_off[c],executed.global_active[c],executed.asp_checks[c],executed.asp_enables[c],executed.started[c]);
                    if(mode>=27)IOLog("ZIZHAN_AUDIO_FRAME: chip=%u fs=%x/%x lrclk_delay=%u writes=%u ws_restored=%u sck_restored=%u\n",c,
                        executed.global_fs_before[c],executed.global_fs_after[c],executed.lrclk_delay[c],executed.frame_writes[c],ws_state.restored?1u:0u,sck_state.restored?1u:0u);
                    if(mode>=29)IOLog("ZIZHAN_AUDIO_PRERUN: chip=%u r=%u checksum=%u status=%u top_bypass=%u writes=%u readbacks=%u volatile_copy=1 calibration_run=0\n",c,
                        executed.calibration_r[c],executed.calibration_checksum[c],executed.calibration_status[c],executed.us_bypass[c],executed.prerun_writes[c],executed.prerun_readbacks[c]);
                }
            }
            if(mode>=30)for(unsigned c=0;c<2;++c)for(unsigned i=0;i<executed.native_rate_readbacks[c];++i)
                IOLog("ZIZHAN_AUDIO_RATE: chip=%u index=%u before=%x after=%x mask=1f native_default=0\n",c,i,executed.native_rate_before[c][i],executed.native_rate_after[c][i]);
            if(mode>=31)for(unsigned c=0;c<2;++c)
                IOLog("ZIZHAN_AUDIO_RX: chip=%u on=%u off=%u active=%u asp=%x tx_enable=%x amp_boost=0 data_pads=%u audio_output=0\n",c,
                    executed.asp_rx_on[c],executed.asp_rx_off[c],executed.asp_rx_active[c],executed.asp_enables[c],executed.asp_enables[c]&0xfu,(mode>=35&&mode<=40)?combined->data_connected:0u);
            if(mode>=32)for(unsigned c=0;c<2;++c)
                IOLog("ZIZHAN_AUDIO_FORMAT: chip=%u before=%x after=%x reads=%u mask=ff000755 value=20000000 no_output=1\n",c,
                    executed.asp_format_before[c],executed.asp_format_after[c],executed.asp_format_reads[c]);
            if(mode>=33)for(unsigned c=0;c<2;++c)
                IOLog("ZIZHAN_AUDIO_COREINIT: chip=%u before=%x/%x/%x after=%x/%x/%x reads=%u writes_total=%u ack_total=%u native_full0=%u no_core=%u\n",c,
                    loaded.core_init_before[c][0],loaded.core_init_before[c][1],loaded.core_init_before[c][2],loaded.core_init_after[c][0],loaded.core_init_after[c][1],loaded.core_init_after[c][2],
                    loaded.core_init_reads[c],loaded.core_init_attempted,loaded.core_init_acknowledged,loaded.native_core_init,mode==33?1u:0u);
            if(mode>=36){
                IOLog("ZIZHAN_AUDIO_OUTPUT_CFG: %s reason=%u completed=%u attempted=%u acknowledged=%u readbacks=%u/%u amp_boost_enable=0 volume_index=0 gain_code=5 reset_asserted=%u finished=1\n",
                    output_setup.status==ZizhanAmpPass?"PASS":"FAIL",output_setup.reason,output_setup.completed,output_setup.attempted,output_setup.acknowledged,output_setup.readbacks[0],output_setup.readbacks[1],held_reset?1u:0u);
                for(unsigned c=0;c<2;++c)for(unsigned i=0;i<output_setup.readbacks[c];++i)IOLog("ZIZHAN_AUDIO_OUTPUT_CFG_ROW: chip=%u index=%u reg=%x mask=%x before=%x after=%x output_enables=0\n",c,i,zizhan_output_fields[i].reg,zizhan_output_fields[i].mask,output_setup.before[c][i],output_setup.after[c][i]);
            }
            if(run){
                IOLog("ZIZHAN_AMP_RUN: observed_mpu_profile=%u stable_readbacks=%u/%u mpu_rights_verified=0\n",
                    executed.observed_mpu_profile,executed.mpu_stable_readbacks[0],executed.mpu_stable_readbacks[1]);
                IOLog("ZIZHAN_AMP_RUN: readback reg=%x expected=%x observed=%x capture_only=%u mismatches=%u/%u\n",
                    executed.failed_register,executed.expected_readback,executed.observed_readback,executed.mpu_capture_only,executed.mpu_mismatches[0],executed.mpu_mismatches[1]);
                if(capture_mpu)for(unsigned chip=0;chip<2;++chip)for(unsigned i=0;i<executed.mpu_readbacks[chip];++i)
                    IOLog("ZIZHAN_AMP_MPU: chip=%u index=%u observed=%08x\n",chip,i,executed.mpu_values[chip][i]);
                IOLog("ZIZHAN_AMP_RUN: %s reason=%u completed=%u attempted=%u acknowledged=%u elapsed_us=%llu reset_asserted=%u finished=1\n",
                    executed.status==ZizhanAmpPass&&held_reset?"PASS":"FAIL",executed.reason,executed.completed,executed.attempted_writes,
                    executed.acknowledged_writes,executed.elapsed_us,unsigned(reset_state.claimed&&held_reset));
                IOLog("ZIZHAN_AMP_RUN: position chip=%u stage=%u bus_error=%u uncertain=%u global_cycles=%u amp_boost_enable=0 asp_tx_enable=0 audio_output=0\n",
                    executed.failed_chip,executed.failed_stage,executed.transport.bus_error,unsigned(executed.transport.controller_uncertain),executed.global_on[0]+executed.global_on[1]);
                for(unsigned chip=0;chip<2;++chip){
                    IOLog("ZIZHAN_AMP_RUN: chip=%u rates=%u mpu=%u started=%u stopped=%u core=%x/%x/%x samples=%u heartbeat_changes=%u\n",
                        chip,executed.rate_readbacks[chip],executed.mpu_readbacks[chip],executed.started[chip],executed.stopped[chip],
                        executed.core_before[chip],executed.core_running[chip],executed.core_after[chip],executed.samples[chip],executed.heartbeat_changes[chip]);
                    IOLog("ZIZHAN_AMP_RUN: chip=%u halo=%x heartbeat=%x/%x cspl=%x mailbox=%x pll=%x dspclk=%x vio=%x/%x/%x watchdog=%x\n",
                        chip,executed.halo_state[chip],executed.heartbeat_first[chip],executed.heartbeat_last[chip],executed.cspl_state[chip],executed.mailbox[chip],
                        executed.pll[chip],executed.dsp_clock[chip],executed.violations[chip][0],executed.violations[chip][1],executed.violations[chip][2],executed.watchdog[chip]);
                    IOLog("ZIZHAN_AMP_RUN: chip=%u states=%u pwr1=%x pwr2=%x halo_core=%x\n",chip,executed.state_count[chip],
                        executed.after_state[chip][0],executed.after_state[chip][1],executed.after_state[chip][2]);
                }
            }
            if(coefficients){
                IOLog("ZIZHAN_AMP_COEFF: %s reason=%u completed=%u sources=%u attempted=%u acknowledged=%u elapsed_us=%llu reset_asserted=%u finished=1\n",
                    configured.status==ZizhanAmpPass&&held_reset?"PASS":"FAIL",configured.reason,configured.completed,configured.source_verified,
                    configured.attempted_writes,configured.acknowledged_writes,configured.elapsed_us,unsigned(reset_state.claimed&&held_reset));
                IOLog("ZIZHAN_AMP_COEFF: position chip=%u segment=%u offset=%u bus_error=%u uncertain=%u dsp_start=0 output_enable=0 audio_output=0\n",
                    configured.failed_chip,configured.failed_segment,configured.failed_offset,configured.transport.bus_error,unsigned(configured.transport.controller_uncertain));
                for(unsigned chip=0;chip<2;++chip){
                    IOLog("ZIZHAN_AMP_COEFF: chip=%u identity_words=%u verified=%u readbacks=%u states=%u pwr1=%x pwr2=%x halo=%x\n",
                        chip,configured.identity_words[chip],configured.segments_verified[chip],configured.readbacks[chip],configured.state_count[chip],
                        configured.after_state[chip][0],configured.after_state[chip][1],configured.after_state[chip][2]);
                    IOLog("ZIZHAN_AMP_COEFF: chip=%u fw=%x version=%x algorithms=%u first=%x/%x xm=%x/%x ym=%x/%x second=%x/%x\n",
                        chip,configured.identity[chip][3],configured.identity[chip][4],configured.identity[chip][9],configured.identity[chip][10],configured.identity[chip][11],
                        configured.identity[chip][12],configured.identity[chip][13],configured.identity[chip][14],configured.identity[chip][15],configured.identity[chip][16],configured.identity[chip][17]);
                }
            }
            if(ram){
                if(burst)IOLog("ZIZHAN_AMP_RAM: burst_reads=%u xm_ym_bytes=12 pm_bytes=20 mismatches=%u\n",loaded.burst_reads,loaded.mismatches);
                if(header){
                    IOLog("ZIZHAN_AMP_HEADER: %s reason=%u source=%u attempted=%u acknowledged=%u packed=%u unpacked=%u mismatches=%u reset_asserted=%u finished=1\n",
                        loaded.status==ZizhanAmpPass&&held_reset?"PASS capture":"FAIL capture",loaded.reason,loaded.source_verified,
                        loaded.attempted_writes,loaded.acknowledged_writes,loaded.packed_count,loaded.unpacked_count,loaded.mismatches,unsigned(reset_state.claimed&&held_reset));
                    for(unsigned i=0;i<loaded.packed_count;++i)IOLog("ZIZHAN_AMP_HEADER: packed=%u expected=%08x observed=%08x\n",i,loaded.header_expected[i],loaded.header_observed[i]);
                    for(unsigned i=0;i<loaded.unpacked_count;++i)IOLog("ZIZHAN_AMP_HEADER: unpacked=%u observed=%08x\n",i,loaded.header_unpacked[i]);
                }
                IOLog("ZIZHAN_AMP_RAM: %s reason=%u completed=%u source_verified=%u attempted=%u acknowledged=%u elapsed_us=%llu reset_asserted=%u finished=1\n",
                    loaded.status==ZizhanAmpPass&&held_reset?"PASS":"FAIL",loaded.reason,loaded.completed,loaded.source_verified,
                    loaded.attempted_writes,loaded.acknowledged_writes,loaded.elapsed_us,unsigned(reset_state.claimed&&held_reset));
                IOLog("ZIZHAN_AMP_RAM: failure chip=%u segment=%u offset=%u bus_error=%u uncertain=%u coefficients=0 dsp_start=0 output_enable=0 audio_output=0\n",
                    loaded.failed_chip,loaded.failed_segment,loaded.failed_offset,loaded.transport.bus_error,unsigned(loaded.transport.controller_uncertain));
                for(unsigned chip=0;chip<2;++chip)IOLog("ZIZHAN_AMP_RAM: chip=%u written=%u verified=%u readbacks=%u states=%u pwr1=%x pwr2=%x halo=%x\n",
                    chip,loaded.segments_written[chip],loaded.segments_verified[chip],loaded.readbacks[chip],loaded.state_count[chip],
                    loaded.after_state[chip][0],loaded.after_state[chip][1],loaded.after_state[chip][2]);
            }
            if (config_observe) {
                const ZizhanAmpBusConfig *configs[] = {&before_config, &after_config};
                const char *stages[] = {"before", "after"};
                for (unsigned j = 0; j < 2; ++j) {
                    const auto &c = *configs[j];
                    IOLog("ZIZHAN_AMP_CONFIG: stage=%s valid=%u phase=%u se=%x fw=%x/%x master=%x/%x slave=%x/%x route=%x\n",
                          stages[j], c.valid, c.phase, c.values[0], c.values[1], c.values[2], c.values[3], c.values[4], c.values[5], c.values[6], c.values[7]);
                    IOLog("ZIZHAN_AMP_CONFIG: stage=%s dma=%x gran=%x pack=%x/%x txfifo=%x rxfifo=%x watermark=%x rfr=%x\n",
                          stages[j], c.values[8], c.values[9], c.values[10], c.values[11], c.values[12], c.values[13], c.values[14], c.values[15]);
                    IOLog("ZIZHAN_AMP_CONFIG: stage=%s dmatx=%x/%x dmarx=%x/%x gsi=%x general=%x ios=%x debug=%x\n",
                          stages[j], c.values[16], c.values[17], c.values[18], c.values[19], c.values[20], c.values[21], c.values[22], c.values[23]);
                }
            }
            if (safeoff) {
                const auto &entry = disabled.identity;
                IOLog("ZIZHAN_AMP_BUS: stage=safe-off valid=%u phase=%u busy_sources=%u se=%x irq=%x rx=%x reason=%u\n",
                      entry.prerequisite_valid, entry.entry_phase, entry.busy_sources,
                      entry.entry_se_status, entry.entry_irq, entry.entry_rx_fifo, entry.reason);
            }
            if (snapshot && (!safeoff || disabled.status == ZizhanAmpPass) && (owned_snapshot ? ready.status == ZizhanAmpPass : !handoff || transferred.status == ZizhanAmpPass)) {
                const auto &entry = captured.identity;
                IOLog("ZIZHAN_AMP_BUS: stage=snapshot valid=%u phase=%u busy_sources=%u se=%x irq=%x rx=%x reason=%u\n",
                      entry.prerequisite_valid, entry.entry_phase, entry.busy_sources,
                      entry.entry_se_status, entry.entry_irq, entry.entry_rx_fifo, entry.reason);
            }
            if (safeoff) {
                for (unsigned chip = 0; chip < 2; ++chip)
                    IOLog("ZIZHAN_AMP_OFF: chip=%u before_count=%u after_count=%u pwr1=%x/%x pwr2=%x/%x halo=%x/%x\n",
                          chip, disabled.before_count[chip], disabled.after_count[chip],
                          disabled.before[chip][0], disabled.after[chip][0],
                          disabled.before[chip][1], disabled.after[chip][1], disabled.before[chip][2], disabled.after[chip][2]);
                IOLog("ZIZHAN_AMP_OFF: %s safe-off reason=%u completed=%u attempted=%u acknowledged=%u bus_error=%u irq=%x uncertain=%u reset_asserted=%u key_writes=0 otp_writes=0 output_enable_writes=0 audio_output=0 finished=1\n",
                      reset_result == zizhan_amp_reset::Ok && disabled.status == ZizhanAmpPass && held_reset ? "PASS" : "FAIL",
                      disabled.reason, disabled.completed, disabled.attempted_writes, disabled.acknowledged_writes,
                      disabled.identity.bus_error, disabled.identity.irq_error, unsigned(disabled.identity.controller_uncertain),
                      unsigned(reset_state.claimed && held_reset));
            }
            if (trim) {
                IOLog("ZIZHAN_AMP_TRIM: %s reason=%u completed=%u attempted=%u acknowledged=%u keys=%u fields=%u readbacks=%u/%u close_acks=%u/%u reset_asserted=%u finished=1\n",
                    calibrated.status == ZizhanAmpPass && held_reset ? "PASS volatile" : "FAIL volatile", calibrated.reason, calibrated.completed,
                    calibrated.attempted_writes, calibrated.acknowledged_writes, calibrated.key_attempts, calibrated.trim_attempts,
                    calibrated.readbacks[0], calibrated.readbacks[1], calibrated.close_acknowledged[0], calibrated.close_acknowledged[1], unsigned(reset_state.claimed && held_reset));
                IOLog("ZIZHAN_AMP_TRIM: scope otp_programmed=0 output_enable_writes=0 dsp_start=0 audio_output=0 lock_readback_verified=0 bus_error=%u uncertain=%u\n",
                    calibrated.transport.bus_error, unsigned(calibrated.transport.controller_uncertain));
                for(unsigned chip=0;chip<2;++chip) IOLog("ZIZHAN_AMP_TRIM: chip=%u count=%u pwr1=%x pwr2=%x halo=%x\n",chip,calibrated.state_count[chip],calibrated.after_state[chip][0],calibrated.after_state[chip][1],calibrated.after_state[chip][2]);
            }
            if (b2_patch) {
                IOLog("ZIZHAN_AMP_B2: %s reason=%u completed=%u attempted=%u acknowledged=%u readbacks=%u/%u close_acks=%u/%u bus_error=%u uncertain=%u reset_asserted=%u finished=1\n",
                      patched.status == ZizhanAmpPass && held_reset ? "PASS patch" : "FAIL patch", patched.reason, patched.completed,
                      patched.attempted_writes, patched.acknowledged_writes, patched.patch_readbacks[0], patched.patch_readbacks[1],
                      patched.close_acknowledged[0], patched.close_acknowledged[1], patched.transport.bus_error,
                      unsigned(patched.transport.controller_uncertain), unsigned(reset_state.claimed && held_reset));
                IOLog("ZIZHAN_AMP_B2: scope lock_readback_verified=0 otp_writes=0 trim_writes=0 output_enable_writes=0 audio_output=0\n");
                for (unsigned chip=0;chip<2;++chip)
                    IOLog("ZIZHAN_AMP_B2: chip=%u count=%u pwr1=%x pwr2=%x halo=%x\n", chip, patched.state_count[chip],
                          patched.after_state[chip][0], patched.after_state[chip][1], patched.after_state[chip][2]);
            }
            if (owned_snapshot) {
                IOLog("ZIZHAN_AMP_READY: status=%u reason=%u samples=%u slave_clear_writes=%u raw_irq=%x enable=%x enabled_pending=%x masked_status=%u reset_asserted=%u finished=1\n",
                      ready.status, ready.reason, ready.samples, ready.slave_clear_writes, ready.stable.values[3], ready.stable.values[4],
                      ready.stable.values[3] & ready.stable.values[4], ready.masked_status, unsigned(reset_state.claimed && held_reset));
                if (ready.status != ZizhanAmpPass) { IOLog("ZIZHAN_AMP_OTP: SKIP readiness-prerequisite finished=1\n"); return; }
            }
            if (handoff && !owned_snapshot) {
                IOLog("ZIZHAN_AMP_HANDOFF: %s reason=%u samples=%u phase=%u se=%x/%x irq=%x/%x rx=%x/%x clear_writes=%u mask=%x watermark=%x rfr=%x slave_irq=%x dma_rx_irq=%x reset_asserted=%u finished=1\n",
                      reset_result == zizhan_amp_reset::Ok && transferred.status == ZizhanAmpPass && held_reset ? "PASS" : "FAIL",
                      transferred.reason, transferred.samples, transferred.phase,
                      transferred.before_se, transferred.after_se, transferred.before_irq, transferred.after_irq,
                      transferred.before_rx, transferred.after_rx, transferred.clear_writes, transferred.acknowledged_mask,
                      transferred.rx_watermark, transferred.rx_rfr_watermark, transferred.slave_irq, transferred.dma_rx_irq, unsigned(reset_state.claimed && held_reset));
                if (source_handoff)
                    IOLog("ZIZHAN_AMP_SOURCE: status=%u reason=%u slave=%x/%x dma=%x/%x slave_clear_writes=%u master_clear_writes=%u reset_asserted=%u finished=1\n",
                          transferred.status, transferred.reason, transferred.slave_irq, transferred.slave_after,
                          transferred.dma_rx_irq, transferred.dma_rx_after, transferred.slave_clear_writes, transferred.clear_writes, unsigned(reset_state.claimed && held_reset));
                if (transferred.status != ZizhanAmpPass) {
                    IOLog("ZIZHAN_AMP_OTP: SKIP handoff-prerequisite finished=1\n"); return;
                }
            }
            if (safeoff && disabled.status != ZizhanAmpPass) {
                IOLog("ZIZHAN_AMP_OTP: SKIP safe-off-prerequisite finished=1\n"); return;
            }
            if (snapshot) {
                for (unsigned chip = 0; chip < 2; ++chip) {
                    IOLog("ZIZHAN_AMP_OTP: chip=%u id=%x before_count=%u after_count=%u pwr1=%x/%x pwr2=%x/%x halo=%x/%x output_disabled=%u boost_disabled=%u dsp_stopped=%u words=%u\n",
                          chip, captured.otp_id[chip], captured.before_count[chip], captured.after_count[chip],
                          captured.state_before[chip][0], captured.state_after[chip][0],
                          captured.state_before[chip][1], captured.state_after[chip][1],
                          captured.state_before[chip][2], captured.state_after[chip][2],
                          captured.output_disabled[chip], captured.boost_disabled[chip], captured.dsp_stopped[chip], captured.word_count[chip]);
                    // Raw per-chip calibration belongs only in private klog;
                    // emit only successfully read words, including on failure.
                    for (unsigned word = 0; word < captured.word_count[chip]; ++word)
                        IOLog("ZIZHAN_AMP_OTP: chip=%u word=%u value=%08x\n", chip, word, captured.words[chip][word]);
                }
                IOLog("ZIZHAN_AMP_OTP: %s snapshot reason=%u completed=%u bus_error=%u irq=%x uncertain=%u reset_asserted=%u codec_value_writes=%u audio_output=0 finished=1\n",
                      reset_result == zizhan_amp_reset::Ok && captured.status == ZizhanAmpPass && held_reset ? "PASS" : "FAIL",
                      captured.reason, captured.completed, observed.bus_error, observed.irq_error,
                      unsigned(observed.controller_uncertain), unsigned(reset_state.claimed && held_reset), disabled.attempted_writes);
                return;
            }
            IOLog("ZIZHAN_AMP: %s otp-identity reason=%u digital_ready=%u completed=%u id=%x/%x rev=%x/%x otp=%x/%x error=%x/%x bus_error=%u irq=%x uncertain=%u reset_asserted=%u codec_value_writes=0 audio_output=0 finished=1\n",
                  reset_result == zizhan_amp_reset::Ok && observed.status == ZizhanAmpPass && held_reset ? "PASS" : "FAIL",
                  observed.reason, observed.digital_ready, observed.completed, observed.devid[0], observed.devid[1],
                  observed.revid[0], observed.revid[1], observed.otp_status[0], observed.otp_status[1],
                  observed.otp_error[0], observed.otp_error[1], observed.bus_error, observed.irq_error,
                  unsigned(observed.controller_uncertain), unsigned(reset_state.claimed && held_reset));
        }
    } else if (timing) IOLog("ZIZHAN_GENI: FAIL bus-prerequisite timing-writes=0 finished=1\n");
}
void worker(void *context,wait_result_t){prepare(static_cast<unsigned>(reinterpret_cast<uintptr_t>(context)));}
} // namespace
static void owned_clock_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,unsigned mode,const ZizhanAudioTransfer *transfer=nullptr){
    if(!receipt){return;}memset(receipt,0,sizeof *receipt);receipt->release_safe=1;
    auto dry_arg=[]{unsigned d=0;return PE_parse_boot_argn("zizhan_pulse_dry",&d,sizeof d)&&d==1;};
    /* zizhan_audio_noclk=1 (dry diagnostics only): no clock vote of our own, so accept the lease
     * without it; the amp's own 64-sample PLL lock check still refuses to run without a BCLK. */
    const bool own=lease&&!lease->skip_clock&&lease->clock_held&&lease->clock_request_ack&&lease->sent==3&&lease->acknowledged==3;
    const bool endpoint=lease&&lease->skip_clock&&!lease->clock_held&&!lease->clock_request_ack&&lease->sent==2&&lease->acknowledged==2&&
                        mode==40&&dry_arg();
    if(!lease||lease->phase!=ZizhanPrmClockReleaseReady||!lease->native_framework||!lease->native_wire||!lease->clock_mode||
       !lease->framework_ready||!lease->lease_held||!lease->request_ack||!(own||endpoint)||lease->reason||lease->uncertain){
        IOLog("ZIZHAN_AUDIO_SILENT: SKIP no-actual-owned-clock writes=0\n");return;}
    if(endpoint){IOLog("ZIZHAN_AUDIO_SILENT: noclk lease accepted dry=1 pll_lock_required=1\n");}
    static uint32_t started;if(__atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL)){receipt->release_safe=0;return;}
    receipt->invoked=1;prepare(mode,receipt,transfer);
}
extern "C" void zizhan_amp_silent_clock_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,20);}
extern "C" void zizhan_amp_pll_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,21);}
extern "C" void zizhan_amp_monitor_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,22);}
extern "C" void zizhan_amp_global_clock_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,23);}
extern "C" void zizhan_amp_global_silent_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,24);}
extern "C" void zizhan_amp_global_mpu_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,25);}
extern "C" void zizhan_amp_global_observed_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,26);}
extern "C" void zizhan_amp_native_frame_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,27);}
extern "C" void zizhan_amp_native_debug_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,28);}
extern "C" void zizhan_amp_native_core_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,34);}
extern "C" void zizhan_amp_native_data_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,35,transfer);}
extern "C" void zizhan_amp_native_output_off_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,36,transfer);}
extern "C" void zizhan_amp_native_core_capture(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,33);}
extern "C" void zizhan_amp_asp_format_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,32);}
extern "C" void zizhan_amp_asp_rx_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,31);}
extern "C" void zizhan_amp_native_rates_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,30);}
extern "C" void zizhan_amp_native_protection_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt){owned_clock_run(lease,receipt,29);}
extern "C" void zizhan_se15_prepare_probe(void) {
    unsigned mode = 0;
    if (!PE_parse_boot_argn("zizhan_se15_prepare", &mode, sizeof mode) || mode < 1 || mode > 19) return;
    unsigned audio = 0, observe = 0;
    PE_parse_boot_argn("pong_audio", &audio, sizeof audio);
    PE_parse_boot_argn("zizhan_se15_observe", &observe, sizeof observe);
    auto node = IORegistryEntry::fromPath("/arm-io/pong-touch", gIODTPlane);
    auto data = node ? OSDynamicCast(OSData, node->getProperty("compatible")) : nullptr;
    const char identity[] = "st,fts-zizhan-pri";
    const bool board = data && data->getLength() == sizeof identity && !memcmp(data->getBytesNoCopy(), identity, sizeof identity);
    if (node) node->release();
    if (!board || audio || observe) { IOLog("ZIZHAN_SE15_PREP: SKIP board-or-mode-conflict writes=0 finished=1\n"); return; }
    static uint32_t started;
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    IOLog("ZIZHAN_SE15_PREP: RUNNING timing=%u identity=%u reset-write=%u i2c-command=%u\n",
          mode >= 2 ? 1u : 0u, mode >= 3 ? 1u : 0u, mode >= 3 ? 1u : 0u, mode >= 3 ? 1u : 0u);
    thread_t thread = nullptr;
    if (kernel_thread_start(worker, reinterpret_cast<void *>(uintptr_t(mode)), &thread) != KERN_SUCCESS) {
        IOLog("ZIZHAN_SE15_PREP: FAIL thread-create finished=1\n"); return;
    }
    thread_deallocate(thread);
}

extern "C" void zizhan_amp_native_mailbox_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,37,transfer);}

extern "C" void zizhan_amp_native_iv_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,38,transfer);}

extern "C" void zizhan_amp_native_pulse_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,39,transfer);}

extern "C" void zizhan_amp_native_play_run(const ZizhanPrmState *lease,ZizhanAudioSilentReceipt *receipt,const ZizhanAudioTransfer *transfer){owned_clock_run(lease,receipt,40,transfer);}
