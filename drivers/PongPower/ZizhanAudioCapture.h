/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_CAPTURE_H
#define ZIZHAN_AUDIO_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ZizhanAudioAbi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional v1 capture extension block stored at byte offset 128 inside the 4 KiB
 * ZizhanAudio control page (leaving the 112-byte v2 ZizhanAudioControl playback
 * ABI byte-for-byte unchanged for existing ZizhanAudio.driver bundles). */
#define ZIZHAN_AUDIO_CAPTURE_MAGIC 0x4341505au /* "ZAPC" */
#define ZIZHAN_AUDIO_CAPTURE_VERSION 1u
#define ZIZHAN_AUDIO_CAPTURE_OFFSET 128u
#define ZIZHAN_AUDIO_CAPTURE_CHANNELS 1u
#define ZIZHAN_AUDIO_CAPTURE_RING_BYTES (ZIZHAN_AUDIO_RING_FRAMES * ZIZHAN_AUDIO_CAPTURE_CHANNELS * 2u)
#define ZIZHAN_AUDIO_CAP_96K_STEREO_FRAMES 960u
#define ZIZHAN_AUDIO_CAP_96K_PERIOD_BYTES (ZIZHAN_AUDIO_CAP_96K_STEREO_FRAMES * 2u * 2u)

enum {
  ZizhanAudioMemoryCapture = 2u,
};

enum {
  ZizhanAudioCapRunning = 1u << 0,
  ZizhanAudioCapReady = 1u << 1,
};

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t rate;
  uint32_t channels;
  uint32_t period_frames;
  uint32_t ring_frames;
  volatile uint32_t state;
  volatile uint32_t cap_seq;
  volatile uint64_t anchor_host;
  volatile uint64_t cap_written;
  volatile uint32_t overflows;
  volatile uint32_t underruns;
} ZizhanAudioCaptureControl;

/* ADSP TX capture graph builders implemented in ShengAudioReach.c. */
uint32_t zizhan_cap_open(uint8_t *out, uint32_t capacity, uint32_t token);
uint32_t zizhan_cap_config(uint8_t *out, uint32_t capacity, uint32_t token);
uint32_t zizhan_cap_read(uint8_t *out, uint32_t capacity, uint32_t token,
                         uint64_t address, uint32_t handle, uint32_t bytes);

static inline volatile ZizhanAudioCaptureControl *zizhan_audio_capture_slot(volatile ZizhanAudioControl *page) {
  if (!page) {
    return NULL;
  }
  return (volatile ZizhanAudioCaptureControl *)((volatile uint8_t *)page + ZIZHAN_AUDIO_CAPTURE_OFFSET);
}

static inline const volatile ZizhanAudioCaptureControl *zizhan_audio_capture_slot_const(const volatile ZizhanAudioControl *page) {
  if (!page) {
    return NULL;
  }
  return (const volatile ZizhanAudioCaptureControl *)((const volatile uint8_t *)page + ZIZHAN_AUDIO_CAPTURE_OFFSET);
}

static inline int zizhan_audio_capture_valid(const volatile ZizhanAudioCaptureControl *cap) {
  return cap &&
         cap->magic == ZIZHAN_AUDIO_CAPTURE_MAGIC &&
         cap->version == ZIZHAN_AUDIO_CAPTURE_VERSION &&
         cap->rate == ZIZHAN_AUDIO_RATE &&
         cap->channels == ZIZHAN_AUDIO_CAPTURE_CHANNELS &&
         cap->period_frames == ZIZHAN_AUDIO_PERIOD_FRAMES &&
         cap->ring_frames == ZIZHAN_AUDIO_RING_FRAMES;
}

static inline int zizhan_audio_capture_init(volatile ZizhanAudioControl *page, size_t page_bytes, uint64_t anchor_host) {
  if (!page || page_bytes < ZIZHAN_AUDIO_CAPTURE_OFFSET + sizeof(ZizhanAudioCaptureControl)) {
    return 0;
  }
  volatile ZizhanAudioCaptureControl *cap = zizhan_audio_capture_slot(page);
  memset((void *)cap, 0, sizeof(*cap));
  cap->magic = ZIZHAN_AUDIO_CAPTURE_MAGIC;
  cap->version = ZIZHAN_AUDIO_CAPTURE_VERSION;
  cap->rate = ZIZHAN_AUDIO_RATE;
  cap->channels = ZIZHAN_AUDIO_CAPTURE_CHANNELS;
  cap->period_frames = ZIZHAN_AUDIO_PERIOD_FRAMES;
  cap->ring_frames = ZIZHAN_AUDIO_RING_FRAMES;
  cap->anchor_host = anchor_host;
  return zizhan_audio_capture_valid(cap);
}

static inline void zizhan_audio_capture_publish_written(volatile ZizhanAudioCaptureControl *cap, uint64_t written) {
  if (!cap) {
    return;
  }
  cap->cap_seq = cap->cap_seq + 1u;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  cap->cap_written = written;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  cap->cap_seq = cap->cap_seq + 1u;
}

static inline int zizhan_audio_capture_written_stable(const volatile ZizhanAudioCaptureControl *cap, uint64_t *written_out) {
  if (!cap || !written_out) {
    return 0;
  }
  const uint32_t first = cap->cap_seq;
  if ((first & 1u) != 0u) {
    return 0;
  }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  const uint64_t value = cap->cap_written;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  const uint32_t second = cap->cap_seq;
  if (first != second || (second & 1u) != 0u) {
    return 0;
  }
  *written_out = value;
  return 1;
}

/* Kernel producer: 2:1 box-filter decimation of 96 kHz / 16-bit / 2-channel ADSP READ_DONE frames
 * into the 48 kHz mono int16 shared capture ring, then tear-free watermark advance. */
static inline uint32_t zizhan_audio_capture_push_96k_stereo(volatile ZizhanAudioCaptureControl *cap,
                                                            int16_t *ring,
                                                            const int16_t *src_96k_lr,
                                                            uint32_t stereo_frames_96k,
                                                            int stereo_mix) {
  if (!zizhan_audio_capture_valid(cap) || !ring || !src_96k_lr || stereo_frames_96k < 2u) {
    return 0u;
  }
  const uint32_t out_frames = stereo_frames_96k / 2u;
  uint64_t t = cap->cap_written;
  for (uint32_t i = 0; i < out_frames; ++i, ++t) {
    const uint32_t s = 4u * i;
    int32_t acc = 0;
    if (stereo_mix) {
      acc = (int32_t)src_96k_lr[s] + (int32_t)src_96k_lr[s + 1u] +
            (int32_t)src_96k_lr[s + 2u] + (int32_t)src_96k_lr[s + 3u];
      acc = (acc >= 0) ? ((acc + 2) / 4) : ((acc - 2) / 4);
    } else {
      acc = (int32_t)src_96k_lr[s] + (int32_t)src_96k_lr[s + 2u];
      acc = (acc >= 0) ? ((acc + 1) / 2) : ((acc - 1) / 2);
    }
    if (acc > 32767) {
      acc = 32767;
    } else if (acc < -32768) {
      acc = -32768;
    }
    ring[t % ZIZHAN_AUDIO_RING_FRAMES] = (int16_t)acc;
  }
  zizhan_audio_capture_publish_written(cap, t);
  return out_frames;
}

/* HAL consumer: read 48 kHz mono frames into CoreAudio Float32 [-1.0, +1.0], zero-filling
 * any frame outside the valid [written - ring_frames, written) window. */
static inline uint32_t zizhan_audio_capture_read_f32(volatile ZizhanAudioCaptureControl *cap,
                                                     const int16_t *ring,
                                                     uint64_t sample_time,
                                                     uint32_t frames,
                                                     int active,
                                                     float *dst) {
  if (!dst || frames == 0u) {
    return 0u;
  }
  if (!zizhan_audio_capture_valid(cap) || !ring || !active) {
    memset(dst, 0, (size_t)frames * sizeof(float));
    return 0u;
  }
  uint64_t written = 0;
  if (!zizhan_audio_capture_written_stable(cap, &written)) {
    memset(dst, 0, (size_t)frames * sizeof(float));
    cap->underruns = cap->underruns + 1u;
    return 0u;
  }
  const uint64_t oldest = (written > ZIZHAN_AUDIO_RING_FRAMES) ? (written - ZIZHAN_AUDIO_RING_FRAMES) : 0ull;
  uint32_t valid_copied = 0u;
  int saw_underrun = 0, saw_overflow = 0;
  for (uint32_t i = 0; i < frames; ++i) {
    const uint64_t f = sample_time + i;
    if (f >= written) {
      dst[i] = 0.0f;
      saw_underrun = 1;
    } else if (f < oldest) {
      dst[i] = 0.0f;
      saw_overflow = 1;
    } else {
      dst[i] = (float)ring[f % ZIZHAN_AUDIO_RING_FRAMES] * (1.0f / 32768.0f);
      ++valid_copied;
    }
  }
  if (saw_underrun) {
    cap->underruns = cap->underruns + 1u;
  }
  if (saw_overflow) {
    cap->overflows = cap->overflows + 1u;
  }
  return valid_copied;
}

#ifdef __cplusplus
}
#endif
#endif
