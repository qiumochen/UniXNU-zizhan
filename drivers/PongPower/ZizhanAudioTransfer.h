/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_TRANSFER_H
#define ZIZHAN_AUDIO_TRANSFER_H
/* Same-thread bounded zero-data callback, never an output-enable API.
 * Return1 only after a real finite-buffer completion; otherwise the core
 * stops and the caller retains every uncertain DMA/graph dependency. */
/* stream (playback window, profile 19 only; NULL elsewhere): send data continuously for up to ms,
 * calling poll(poll_context) between periods and stopping as soon as it returns nonzero. Return 1
 * only if every submitted period returned; the caller then disables output and tears down. */
typedef struct {void *context;int (*once)(void *,unsigned chip);
 int (*stream)(void *,unsigned ms,int (*poll)(void *),void *poll_context);} ZizhanAudioTransfer;
/* Persistent multi-session CoreAudio HAL amplifier lifecycle (PRM 42 + attended or dry):
 * chips are prepared once before READY, armed on StartIO, polled every 50 ms while streaming,
 * and disarmed to 0x3300 on StopIO before final shared-reset teardown. */
typedef struct {
 void *context;
 int (*arm)(void *);
 int (*poll)(void *);
 int (*disarm)(void *,int data_ok);
} ZizhanAudioAmpSession;
typedef int (*ZizhanAudioAmpDaemonFn)(void *,const ZizhanAudioAmpSession *);
#endif
