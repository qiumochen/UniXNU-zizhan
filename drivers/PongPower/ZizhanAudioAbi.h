/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_ABI_H
#define ZIZHAN_AUDIO_ABI_H
#include <stdint.h>
#include <stdbool.h>
/* Zizhan-only control page for the future system audio service. It keeps
 * the proven 48k/2ch/65536-frame/10ms ring contract and adds a producer
 * watermark. out_frames is service-consumed; out_written is the highest
 * sample time the HAL has actually written. written_seq makes the 64-bit
 * watermark publish/read tear-free (odd while updating). */
#define ZIZHAN_AUDIO_MAGIC 0x4455415au
#define ZIZHAN_AUDIO_VERSION 2u
#define ZIZHAN_AUDIO_RATE 48000u
#define ZIZHAN_AUDIO_OUT_CHANNELS 2u
#define ZIZHAN_AUDIO_IN_CHANNELS 0u
#define ZIZHAN_AUDIO_PERIOD_FRAMES 480u
#define ZIZHAN_AUDIO_RING_FRAMES 65536u
#define ZIZHAN_AUDIO_LEAD_FRAMES (30u * ZIZHAN_AUDIO_PERIOD_FRAMES)
enum {ZizhanAudioOutputRunning=1u<<0,ZizhanAudioInputRunning=1u<<1,
      ZizhanAudioOutputReady=1u<<2,ZizhanAudioInputReady=1u<<3};
typedef struct {
 uint32_t magic,version;
 uint32_t rate,out_channels,in_channels,period_frames,ring_frames,lead_frames;
 volatile uint32_t state;
 uint32_t reserved0;
 uint64_t ticks_per_frame_q32;
 volatile uint64_t out_anchor_host,in_anchor_host;
 volatile uint64_t out_frames,in_frames;
 volatile uint64_t out_written;
 volatile uint32_t written_seq,out_late,in_late,out_seq,in_seq;
} ZizhanAudioControl;
static inline int zizhan_audio_control_valid(const volatile ZizhanAudioControl *c){
 return c&&c->magic==ZIZHAN_AUDIO_MAGIC&&c->version==ZIZHAN_AUDIO_VERSION&&
        c->rate==ZIZHAN_AUDIO_RATE&&c->out_channels==ZIZHAN_AUDIO_OUT_CHANNELS&&
        c->in_channels==ZIZHAN_AUDIO_IN_CHANNELS&&c->period_frames==ZIZHAN_AUDIO_PERIOD_FRAMES&&
        c->ring_frames==ZIZHAN_AUDIO_RING_FRAMES&&c->lead_frames>0&&
        c->lead_frames<c->ring_frames&&c->ticks_per_frame_q32;
}
static inline int zizhan_audio_ready(const volatile ZizhanAudioControl *c){
 return zizhan_audio_control_valid(c)&&(c->state&ZizhanAudioOutputReady);
}
static inline void zizhan_audio_publish_written(volatile ZizhanAudioControl *c,uint64_t written){
 if(!c){return;}c->written_seq=c->written_seq+1;
 __atomic_thread_fence(__ATOMIC_RELEASE);c->out_written=written;
 __atomic_thread_fence(__ATOMIC_RELEASE);c->written_seq=c->written_seq+1;
}
static inline int zizhan_audio_written_stable(const volatile ZizhanAudioControl *c,uint64_t *written){
 if(!c||!written){return 0;}const uint32_t first=c->written_seq;
 if(first&1u){return 0;}const uint64_t value=c->out_written;const uint32_t second=c->written_seq;
 if(first!=second||(second&1u)){return 0;}*written=value;return 1;
}
static inline int zizhan_audio_has_frames(const volatile ZizhanAudioControl *c,uint64_t frame,unsigned frames){
 uint64_t written;if(!frames||frame>UINT64_MAX-frames)return 0;
 return zizhan_audio_written_stable(c,&written)&&written>=frame+frames;
}
#endif
