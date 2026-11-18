/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_SILENT_H
#define ZIZHAN_AUDIO_SILENT_H
#include "ZizhanAudioPrm.h"
#include "ZizhanAudioTransfer.h"
/* Same-thread,one-shot executor. Called only after actual APM/core/clock
 * ACKs;never starts another worker or enables GLOBAL/AMP/BST/ASP output.
 * release_safe means reset asserted AND our SCK restored,or no hardware
 * was claimed. A failed heartbeat is distinct from safe lease cleanup. */
typedef struct {
 unsigned invoked,amp_pass,release_safe,reset_asserted,pin_connected,pin_restored;
 unsigned completed,heartbeat_changes[2],samples[2],pll_steps[2],diagnostic_capture;
 unsigned data_connected,data_restored,transfer_attempts[2],transfer_acks[2];
 unsigned output_configured,mailbox_verified,iv_verified,pulse_verified;
} ZizhanAudioSilentReceipt;
#ifdef __cplusplus
extern "C" {
#endif
void zizhan_amp_silent_clock_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_pll_clock_capture(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_monitor_clock_capture(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_global_clock_capture(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_global_silent_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_global_mpu_capture(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_global_observed_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_frame_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_debug_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_core_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_data_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
void zizhan_amp_native_pulse_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
/* PRM 41: profile 19 dual-amp playback window; transfer->stream required. */
void zizhan_amp_native_play_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
void zizhan_amp_native_iv_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
void zizhan_amp_native_mailbox_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
void zizhan_amp_native_output_off_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *,const ZizhanAudioTransfer *);
void zizhan_amp_native_core_capture(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_asp_format_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_asp_rx_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_rates_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
void zizhan_amp_native_protection_run(const ZizhanPrmState *,ZizhanAudioSilentReceipt *);
#ifdef __cplusplus
}
#endif
#endif
