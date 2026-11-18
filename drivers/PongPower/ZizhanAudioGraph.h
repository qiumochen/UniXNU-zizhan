/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_GRAPH_H
#define ZIZHAN_AUDIO_GRAPH_H
#include "ShengAudioReach.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One owner, two linked subgraphs, one lifecycle:
 * device: MFC 0x07001015 -> SAL 0x07001010 -> TDM sink 0x0700100e
 * stream: WR_SHMEM 0x07001000 -> (cross-graph link) MFC input
 * The MFC performs the original 48k/2ch/24-bit-packed to 96k/3ch/24-bit
 * conversion; the stream graph does not create a third channel. */
enum {ZizhanMfcPort=0x2105,ZizhanMfcSg=0x4106,ZizhanMfcContainer=0x5106,
      ZizhanMfcIid=0x6182,ZizhanSalIid=0x6183,ZizhanPcmDecIid=0x6184,ZizhanPcmCnvIid=0x6185};
uint32_t zizhan_mfc_device_open(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_mfc_device_config(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_open_to_mfc(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_media_packed48(uint8_t *,uint32_t,uint32_t);
/* zizhan_audio_dec=1: stream WR_SHMEM -> PCM_DEC 0x07001005 -> PCM_CNV 0x07001003 -> MFC, as the
 * Android AGM speaker graph (tags c000000e/c0000010); both outputs deinterleaved-unpacked 24-in-32 Q27. */
uint32_t zizhan_pcm_open_dec_to_mfc(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_dec_cnv_config(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_graph_command(uint8_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
