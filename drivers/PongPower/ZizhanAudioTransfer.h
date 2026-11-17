/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_TRANSFER_H
#define ZIZHAN_AUDIO_TRANSFER_H
/* Same-thread bounded zero-data callback, never an output-enable API.
 * Return1 only after a real finite-buffer completion; otherwise the core
 * stops and the caller retains every uncertain DMA/graph dependency. */
typedef struct {void *context;int (*once)(void *,unsigned chip);} ZizhanAudioTransfer;
#endif
