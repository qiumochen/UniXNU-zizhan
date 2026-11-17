/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_BATCH_H
#define ZIZHAN_AUDIO_BATCH_H
#include "ZizhanAudioWrite.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One zero-initialized instance per qualified channel/mapping epoch, held
 * by one actor. At most four distinct period buffers, no token wrap/reinit.
 * The qualified default is four 11520-byte/960-frame 96kHz/3ch/32-bit
 * buffers. A frontend may declare another exact period (for example
 * 2880 bytes/480 frames for the original 48kHz/2ch/24-bit HAL format).
 * This core does not convert formats, access DMA or enable output. The
 * caller proves live map, staged contents and protection separately.
 * Returned buffers do NOT authorize hardware shutdown/unmap/lease release. */
enum {ZizhanBatchReady,ZizhanBatchWaiting,ZizhanBatchDone,ZizhanBatchFailed};
typedef struct {
 uint32_t magic,handle;unsigned phase,total,submitted,completed,prepared,stopping,uncertain,reason;
 unsigned bytes,frames,stride;uint64_t base,began_us,confirmed_frames;ZizhanWriteState write;
} ZizhanAudioBatch;
int zizhan_batch_init(ZizhanAudioBatch *,uint64_t,uint32_t,unsigned,unsigned,uint64_t);
int zizhan_batch_init_ex(ZizhanAudioBatch *,uint64_t,uint32_t,unsigned,unsigned,unsigned,unsigned,uint64_t);
int zizhan_batch_prepared(ZizhanAudioBatch *,unsigned,unsigned);
unsigned zizhan_batch_next(ZizhanAudioBatch *,uint64_t,uint8_t[512]);
int zizhan_batch_receive(ZizhanAudioBatch *,const uint8_t *,unsigned,uint64_t);
void zizhan_batch_tick(ZizhanAudioBatch *,uint64_t);
void zizhan_batch_stop(ZizhanAudioBatch *);
void zizhan_batch_send_failed(ZizhanAudioBatch *);
int zizhan_batch_buffers_returned(const ZizhanAudioBatch *);
#ifdef __cplusplus
}
#endif
#endif
