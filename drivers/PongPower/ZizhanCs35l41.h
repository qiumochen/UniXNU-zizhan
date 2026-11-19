/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_CS35L41_H
#define ZIZHAN_CS35L41_H
#include "ZizhanAudioTransfer.h"
#include "ZizhanAmpOutputPlan.h"
#include "ZizhanAmpPulse.h"
#include "GeniI2c.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { ZizhanAmpSkip, ZizhanAmpPass, ZizhanAmpFail };
enum { ZizhanAmpOff, ZizhanAmpPrerequisite, ZizhanAmpBusy, ZizhanAmpBus,
       ZizhanAmpTimeout, ZizhanAmpWrongIdentity, ZizhanAmpProtocol, ZizhanAmpArgument,
       ZizhanAmpOtpTimeout, ZizhanAmpOtpError, ZizhanAmpOutputActive,
       ZizhanAmpUnsupportedOtp, ZizhanAmpSnapshotTimeout, ZizhanAmpStateChanged, ZizhanAmpReadback };
enum {
    ZizhanAmpExclusiveSe15 = 1u, ZizhanAmpPinsVerified = 2u,
    ZizhanAmpPowerVerified = 4u, ZizhanAmpResetReleased = 8u,
    ZizhanAmpPrerequisites = 15u,
    ZizhanAmpUnmanagedSupplyTopology = 16u
};
typedef struct {
    void *context;
    uint64_t (*now_us)(void *);
    void (*pause_us)(void *, unsigned);
} ZizhanAmpClock;
typedef struct {
    unsigned status, reason, completed;
    uint32_t devid[2], revid[2];
    unsigned bus_error;
    uint32_t irq_error;
    int controller_uncertain;
    uint32_t otp_status[2], otp_error[2];
    unsigned digital_ready;
    /* Entry evidence only, not a readiness override. valid1=software phase,
     * valid2=all3 raw hardware samples. busy_sources:1 phase,2 active SE,
     *4 any IRQ,8 nonempty RX. Unknown/stale bits remain a refusal. */
    unsigned prerequisite_valid, entry_phase, busy_sources;
    uint32_t entry_se_status, entry_irq, entry_rx_fifo;
} ZizhanAmpIdentity;

/* Explicit opt-in candidate on an exclusively leased, already prepared SE15
 * (live zizhan DT:0x880000/0x4000,pins56/57,addresses40/42). The platform
 * owner must verify each prerequisite on real hardware; a DT declaration
 * alone is not power/reset readiness. This function does not map/prepare a
 * bus, write pinmux/reset/GCC or call the Pong TFA/SE13 initialization.
 *
 * Reads only DEVID0/REVID4 via BE32 register + repeated-start BE32 value.
 * Each transfer has20ms, cancel2ms, abort2ms, plus iteration caps. Failed or
 * unacknowledged cancel/abort marks the controller uncertain; caller must
 * retain the lease until reset, never hand it to another consumer. No codec
 * register-value write, OTP key, boost, PCM, ADSP graph or output endpoint. */
int zizhan_cs35l41_identify(unsigned enabled, unsigned verified, GeniI2c *,
                          const ZizhanAmpClock *, ZizhanAmpIdentity *);
/* Phone DT + actual Android module identify no controllable VA/VP rails
 * (dummy suppliers); this is topology, NOT power readiness. With exclusive
 * SE15, verified pins and released reset, poll actual OTP_BOOT_DONE/error on
 * both amps before ID. digital_ready proves digital boot response only, not
 * analogue supply voltage or acoustic safety. At most100ms/64 reads per amp.
 * Never set PowerVerified merely because the topology flag was supplied. */
int zizhan_cs35l41_boot_identify(unsigned enabled, unsigned verified, GeniI2c *,
                               const ZizhanAmpClock *, ZizhanAmpIdentity *);
/* Read-only, opt-in B2 snapshot. Values/partial counts stay available on
 * failure; no test key, trim, codec value or DSP control writes. HALO enable
 * is observed rather than assumed clear after reset, and is not changed.
 * Output/boost must be disabled before OTP reads; states are reread afterward.
 * A PASS means a complete stable snapshot, NOT readiness for initialization.
 * Caller owns final shared-reset assertion on every outcome. */
typedef struct {
    ZizhanAmpIdentity identity;
    unsigned status, reason, completed;
    uint32_t state_before[2][3], state_after[2][3]; /* PWR1/PWR2/HALO */
    unsigned before_count[2], after_count[2], word_count[2];
    uint32_t otp_id[2], words[2][32];
    unsigned output_disabled[2], boost_disabled[2], dsp_stopped[2];
} ZizhanAmpSnapshot;
int zizhan_cs35l41_snapshot(unsigned enabled, unsigned verified, GeniI2c *,
                           const ZizhanAmpClock *, ZizhanAmpSnapshot *);
/* B2 only, real GLOBAL_EN must already be0 on both amps. Clear-only
 * PWR2 AMP/BST mask0x31 and HALO core-enable mask1, preserving every other
 * bit with fresh reads and exact readback. Max4 value writes, all transfers
 *20/2/2ms + iteration caps. Zero key/OTP/trim/output-enable operations.
 * Acknowledge or readback failure never retries and caller retains ownership. */
typedef struct {
    ZizhanAmpIdentity identity;
    unsigned status, reason, completed, attempted_writes, acknowledged_writes;
    uint32_t before[2][3], after[2][3];
    unsigned before_count[2], after_count[2];
} ZizhanAmpSafeOff;
int zizhan_cs35l41_safe_off(unsigned enabled, unsigned verified, GeniI2c *,
                          const ZizhanAmpClock *, ZizhanAmpSafeOff *);
/* Same exclusive owner only, immediately after its successful safe-off.
 * Explicit opt-in; at most ONE W1C of observed RX_IRQ/RX watermark bits.
 * Phase idle,SE inactive,RX empty and no unknown IRQ are required for every
 * sample. Max500us/26 samples, two zero observations separated by20us;
 * no retry write/FIFO drain or command dispatch. Caller finally resets. */
typedef struct {
    unsigned status, reason, samples, clear_writes, phase;
    uint32_t before_se, before_irq, before_rx, after_se, after_irq, after_rx;
    uint32_t acknowledged_mask, rx_watermark, rx_rfr_watermark, slave_irq, dma_rx_irq;
    uint32_t slave_after, dma_rx_after;
    unsigned slave_clear_writes;
} ZizhanAmpHandoff;
int zizhan_cs35l41_handoff(unsigned enabled, unsigned verified, GeniI2c *,
                         const ZizhanAmpClock *, const ZizhanAmpSafeOff *, ZizhanAmpHandoff *);
/* Independent source-first candidate: same prerequisites as handoff,
 * plus DMA RXIRQ0 and slave IRQ limited to observed RX-LAST bit27. At most
 * one exact slave W1C, then (only once slave0) one known master W1C.
 * Both sources must remain0 for two samples; unknown/data/error refused. */
int zizhan_cs35l41_source_handoff(unsigned enabled, unsigned verified, GeniI2c *,
                                const ZizhanAmpClock *, const ZizhanAmpSafeOff *, ZizhanAmpHandoff *);
/* Fixed read-only GENI configuration sample, under the same exclusive
 * platform lease. Never reads FIFO data or clear/control registers. Each
 * entry is an individual ordered read, not an atomic hardware snapshot. */
enum { ZizhanAmpConfigWords = 24 };
typedef struct { unsigned valid, phase; uint32_t values[ZizhanAmpConfigWords]; } ZizhanAmpBusConfig;
int zizhan_cs35l41_bus_config(unsigned enabled, unsigned verified, const GeniI2c *, ZizhanAmpBusConfig *);
/* Narrow evidence-based opt-in classifier. Raw IRQ80 is never rewritten
 * as zero: require exact FW303,mask80 off,no enabled pending/source/DMA/
 * command/FIFO/route/GSI state and two identical ordered config samples.
 * Only a known initial slave RX-LAST may be acknowledged once; no master
 * W1C. Old strict APIs stay unchanged. This proves scoped owned readiness,
 * not general harmlessness of a masked status or playback qualification. */
typedef struct {
    unsigned status, reason, samples, slave_clear_writes, masked_status;
    ZizhanAmpBusConfig before, stable;
} ZizhanAmpReadiness;
int zizhan_cs35l41_owned_snapshot(unsigned enabled, unsigned verified, GeniI2c *,
    const ZizhanAmpClock *, const ZizhanAmpSafeOff *, ZizhanAmpReadiness *, ZizhanAmpSnapshot *);
/* Fixed actual-phone B2 patch only. Requires fresh owned snapshot on this
 * same exclusive bus, both output/boost/HALO stopped, and identity B2.
 * Each of6 fixed patch destinations is read back before close; test-key
 * close ACK is recorded separately and is NOT a verified hardware lock.
 * No OTP/trim/route/output-enable. Caller must finally assert shared reset. */
typedef struct {
    unsigned status, reason, completed, attempted_writes, acknowledged_writes;
    unsigned patch_readbacks[2], close_acknowledged[2];
    uint32_t readback[2][6], after_state[2][3];
    unsigned state_count[2];
    ZizhanAmpIdentity transport;
} ZizhanAmpB2Patch;
int zizhan_cs35l41_b2_patch(unsigned enabled, unsigned verified, GeniI2c *,
                          const ZizhanAmpClock *, const ZizhanAmpSnapshot *, ZizhanAmpB2Patch *);
/* OTP_ID8 only, actual fresh snapshot and fixed B2 success required.
 *98 known volatile fields/chip,masked RMW/full readback,no OTP memory
 *writes. Byte-key55/aa/cc/33 distinct from B2 word keys. Max204 value
 *write attempts,5s checks/fixed fields and bounded per-transfer cleanup.
 *Close ACKs not hardware-lock proof;caller finally asserts reset. */
typedef struct {
 unsigned status,reason,completed,attempted_writes,acknowledged_writes,key_attempts,trim_attempts;
 unsigned readbacks[2],close_acknowledged[2],state_count[2];
 uint32_t after_state[2][3];
 ZizhanAmpIdentity transport;
} ZizhanAmpTrim;
int zizhan_cs35l41_trim(unsigned enabled,unsigned verified,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpSnapshot *,const ZizhanAmpB2Patch *,ZizhanAmpTrim *);
/* Fixed original protection WMFW only, SHA256 verified before any transfer.
 * Caller owns an immutable private byte copy and exclusive bus. Six reviewed
 * RAM spans only; no coefficients/keys/control/DSP enable. Full ordered
 * readback on each chip, disabled-state checks per segment, 45s whole limit
 * plus fixed14938 write/read caps and existing20/2/2ms transfer limits.
 * Always retain/reset on failure; a timeout never authorizes more traffic. */
enum { ZizhanAmpRamFileBytes=34068, ZizhanAmpRamSegments=6, ZizhanAmpRamWords=7469 };
typedef int (*ZizhanAmpHash)(const uint8_t *,unsigned,uint8_t[32]);
typedef struct {
 unsigned status,reason,completed,attempted_writes,acknowledged_writes;
 unsigned readbacks[2],segments_written[2],segments_verified[2],state_count[2];
 unsigned source_verified,failed_chip,failed_segment,failed_offset;
 uint32_t after_state[2][3];uint64_t elapsed_us;
 unsigned native_core_init,core_init_attempted,core_init_acknowledged,core_init_reads[2];uint32_t core_init_before[2][3],core_init_after[2][3];
 unsigned header_only,mismatches,packed_count,unpacked_count,burst_reads;
 uint32_t header_expected[18],header_observed[18],header_unpacked[24];
 ZizhanAmpIdentity transport;
} ZizhanAmpRam;
/* Original probe full CCM=0 before firmware;separate from trim evidence. */
int zizhan_cs35l41_ram_native_init(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *);
int zizhan_cs35l41_ram(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *);
/* Independent header-only diagnostic after mode13 mismatch:chip0 first72
 * bytes only,18 writes/full18 packed reads/24 unpacked24 reads,no second
 * chip or remaining firmware. PASS is capture completeness,not RAM match. */
int zizhan_cs35l41_ram_header(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *);
/* Independent candidate after real header unpacked24 matched:packed XM/YM
 * reads12bytes and PM20bytes per I2C transaction; compare every raw byte.
 * Old mode13 scalar read failure retained as a control. */
int zizhan_cs35l41_ram_burst(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *);
typedef struct {
 unsigned status,reason,completed,identity_words[2],attempted_writes,acknowledged_writes;
 unsigned readbacks[2],segments_verified[2],source_verified,state_count[2];
 unsigned failed_chip,failed_segment,failed_offset;
 uint32_t identity[2][22],after_state[2][3];uint64_t elapsed_us;
 ZizhanAmpIdentity transport;
} ZizhanAmpCoefficients;
/* Same-round full burst RAM PASS required. Read/validate both actual22word
 * algorithm headers BEFORE any coefficient write. Exact original T/B file
 * hashes,three fixed RAM spans,full readback,10s limit;no DSP/control writes. */
int zizhan_cs35l41_coefficients(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpRam *,
 const uint8_t *,unsigned,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpCoefficients *);
typedef struct {
 unsigned status,reason,completed,attempted,acknowledged,readbacks[2],failed_chip,failed_index;
 uint32_t before[2][ZizhanOutputFields],after[2][ZizhanOutputFields],state_before[2][3],state_after[2][3];
 uint64_t elapsed_us;ZizhanAmpIdentity transport;
} ZizhanAmpOutputSetup;
int zizhan_cs35l41_output_setup(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpCoefficients *,ZizhanAmpOutputSetup *);
typedef struct {uint32_t version,bytes,resistance[2];} ZizhanAmpSpeakerCalibration;
typedef struct {
 unsigned status,reason,completed,attempted_writes,acknowledged_writes;
 unsigned rate_readbacks[2],mpu_readbacks[2],started[2],stopped[2],samples[2],heartbeat_changes[2];
 uint32_t core_before[2],core_running[2],core_after[2],halo_state[2],heartbeat_first[2],heartbeat_last[2],cspl_state[2];
 uint32_t mailbox[2],pll[2],dsp_clock[2],violations[2][3],watchdog[2],after_state[2][3];
 unsigned state_count[2],failed_chip,failed_stage;uint64_t elapsed_us;
 unsigned mpu_capture_only,mpu_mismatches[2];uint32_t mpu_values[2][20];
 unsigned observed_mpu_profile,mpu_stable_readbacks[2];
 uint32_t failed_register,expected_readback,observed_readback;
 unsigned clocked_profile,pll_steps[2];uint32_t pll_before[2],pll_current[2];
 unsigned pll_capture,pll_samples[2],pll_lock_samples[2];
 uint32_t pll_sticky[2][4],pll_raw_first[2][4],pll_raw_last[2][4],pll_raw_or[2][4],pll_raw_and[2][4];
 unsigned frequency_monitor,monitor_writes[2];uint32_t monitor_before[2],monitor_after[2],clock_detect_first[2],clock_detect_last[2],clock_detect_or[2];
 unsigned global_profile,global_active[2],global_on[2],global_off[2],asp_checks[2];uint32_t asp_enables[2];
 unsigned native_frame,frame_writes[2];uint32_t global_fs_before[2],global_fs_after[2],lrclk_delay[2];
 unsigned core_debug,debug_count[2][6];uint32_t debug_values[2][6][15];
 unsigned asp_format,asp_format_reads[2];uint32_t asp_format_before[2],asp_format_after[2];
 unsigned asp_rx_profile,asp_rx_active[2],asp_rx_on[2],asp_rx_off[2];
 unsigned native_rates,native_rate_writes[2],native_rate_readbacks[2];uint32_t native_rate_before[2][16],native_rate_after[2][16];
 unsigned native_calibration,prerun_writes[2],prerun_readbacks[2];uint32_t calibration_r[2],calibration_checksum[2],calibration_status[2],us_bypass[2];
 unsigned transfer_attempts[2],transfer_acks[2];uint32_t transfer_halo[2][2],transfer_heartbeat[2][2];
 unsigned pulse_profile;ZizhanPulseReceipt pulse[2];
 /* profile 19 playback window (docs/plan/2026-11-18-m1-playback-amp.md) */
 unsigned play_profile,play_ms,play_ready[2],play_torn[2],play_streamed,play_polls,play_reason,play_stage,play_chip;
 /* Read-only register snapshot at poll 5 of the play window (zizhan_play_snap_regs order). */
 uint32_t play_snap[2][16];unsigned play_snap_reads[2];
 /* zizhan_audio_echo: top chip ASP TX1 source/enables before, after setup, after restore; 4810. */
 uint32_t echo_src[3],echo_en[3],echo_slots;unsigned echo_on,echo_restored;
 unsigned iv_profile,iv_reads[2][2];uint32_t iv_values[2][2][3];
 unsigned mailbox_profile,mailbox_attempted[2],mailbox_acknowledged[2],mailbox_completed[2];
 unsigned mailbox_polls[2][2],mailbox_mask_restored[2][2];
 uint32_t mailbox_mask_before[2][2],mailbox_mask_after[2][2],mailbox_irq[2][2],mailbox_status[2][2];
 ZizhanAmpIdentity transport;
} ZizhanAmpRun;
/* Fixed actual-module16rate/23MPU writes then masked core-enable only.
 * No GLOBAL/AMP/BST/ASP/PLL/clock/output writes. Same-round coefficients
 * PASS required;poll actual metadata HALO_STATE/HEARTBEAT max32x10ms/chip,
 * require heartbeat progress,keep outputs off,clear core and exact readback.
 * 2s total/fixed82writes;uncertain transport forbids cleanup,caller resets. */
int zizhan_cs35l41_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
/* Independent MPU-only raw-capture diagnostic. Fixed writes/read counts,
 * no core enable. Capture PASS does not qualify MPU access masks or run. */
int zizhan_cs35l41_mpu_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
/* Independent empirical profile:exact real mode18 twentyword vector twice,
 * including after key close. No arbitrary mask/ignored bits;not proof that
 * FFFFFFFF writes created access rights. Still requires actual heartbeat. */
int zizhan_cs35l41_observed_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
/* Native SCLK12.288M PLL six masked steps;caller owns actual clock/SCK.
 * Both outputs remain disabled;100 actual samples/chip,HALO2+heartbeat,
 * stop/core readback. 5s/94writes max;uncertain bus forbids I2C cleanup. */
int zizhan_cs35l41_clocked_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_pll_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_clock_monitor_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_global_pll_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_global_silent_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_global_mpu_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_global_observed_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_native_frame_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
int zizhan_cs35l41_native_debug_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,ZizhanAmpRun *);
uint32_t zizhan_cs35l41_debug_register(unsigned);
int zizhan_cs35l41_native_core_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
int zizhan_cs35l41_native_core_transfer(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *,ZizhanAmpRun *);
/* Output-disabled native PAUSE/RESUME handshake; separate24write budget,
 * actual IRQ edge/status and saved exact mask restoration,never output enable. */
/* Diagnostics only; see ZizhanCs35l41.c. */
extern void (*zizhan_amp_checkpoint)(unsigned chip,unsigned stage);
extern unsigned zizhan_amp_pulse_hold_us;
extern unsigned zizhan_amp_pulse_flags;
extern unsigned zizhan_amp_echo;
/* zizhan_audio_pinsample=1 (dry): called once at poll 5 of the play window to sample the TDM pad levels. */
extern void (*zizhan_play_pin_sample)(void);
/* zizhan_audio_ampdump=1 (dry): read every Android-readable config register of both amps after
 * the stream, before disarm; values land in zizhan_amp_dump_values (see ZizhanAmpDumpRegs.h). */
extern unsigned zizhan_amp_dump_on,zizhan_amp_dump_reads[2];
extern uint32_t zizhan_amp_dump_values[2][228]; /* 1: dry play window echoes ASPRX1 on the top chip's ASPTX1 */
extern unsigned zizhan_amp_play_ms; /* 0 unless the kernel sets zizhan_audio_play_ms=<100..2000> */
int zizhan_cs35l41_native_play_transfer(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *,ZizhanAmpRun *);
int zizhan_cs35l41_native_pulse_transfer(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *,ZizhanAmpRun *);
int zizhan_cs35l41_native_iv_transfer(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *,ZizhanAmpRun *);
int zizhan_cs35l41_native_mailbox_transfer(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *,ZizhanAmpRun *);
int zizhan_cs35l41_native_core_capture(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
int zizhan_cs35l41_asp_format_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
int zizhan_cs35l41_asp_rx_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
int zizhan_cs35l41_native_rates_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
int zizhan_cs35l41_native_protection_run(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,
 const ZizhanAmpCoefficients *,const ZizhanAmpSpeakerCalibration *,ZizhanAmpRun *);
#ifdef __cplusplus
}
#endif
#endif
