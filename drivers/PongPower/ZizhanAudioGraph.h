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
      ZizhanMfcIid=0x6182,ZizhanSalIid=0x6183,ZizhanPcmDecIid=0x6184,ZizhanPcmCnvIid=0x6185,
      ZizhanPcmEncIid=0x6186,ZizhanPcmRdIid=0x6187,ZizhanLoopMfcIid=0x6188,ZizhanLoopSalIid=0x6189};
uint32_t zizhan_mfc_device_open(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_mfc_device_config(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_open_to_mfc(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_media_packed48(uint8_t *,uint32_t,uint32_t);
/* zizhan_audio_dec=1: stream WR_SHMEM -> PCM_DEC 0x07001005 -> PCM_CNV 0x07001003 -> MFC, as the
 * Android AGM speaker graph (tags c000000e/c0000010); both outputs deinterleaved-unpacked 24-in-32 Q27. */
uint32_t zizhan_pcm_open_dec_to_mfc(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_pcm_dec_cnv_config(uint8_t *,uint32_t,uint32_t);
/* zizhan_audio_loopback=3 keeps the real stream graph (WR_SHMEM->DEC->CNV->MFC) and the device
 * MFC->SAL, but the device graph ends in PCM_ENC (16-bit 3ch) -> RD_SHMEM instead of the TDM sink.
 * zizhan_audio_loopback=1|2 (silent diagnostic, no device link): stream WR_SHMEM -> [PCM_DEC] -> PCM_CNV
 * -> PCM_ENC -> RD_SHMEM; 1 includes the decoder, 2 omits it (control). ENC output is 16-bit
 * interleaved FL/FR so the read-back can be compared with the int16 selftest pattern. */
uint32_t zizhan_pcm_open_loopback(uint8_t *,uint32_t,uint32_t,unsigned dec);
uint32_t zizhan_pcm_loopback_config(uint8_t *,uint32_t,uint32_t,unsigned dec);
uint32_t zizhan_pcm_read(uint8_t *,uint32_t,uint32_t,uint64_t,uint32_t,uint32_t);
uint32_t zizhan_mfc_loop_device_open(uint8_t *,uint32_t,uint32_t);
/* zizhan_audio_echo=1 (mode 41, dry only): a capture graph on the tertiary TDM TX lane 0 (GPIO122,
 * the amps' SDOUT): TDM source 0x0700100f (96k/24/4 slots) -> PCM_ENC 16-bit 4ch -> RD_SHMEM. */
enum {ZizhanCapPort=0x2106,ZizhanCapSg=0x4107,ZizhanCapContainer=0x5107,
      ZizhanCapTdmIid=0x618a,ZizhanCapEncIid=0x618b,ZizhanCapRdIid=0x618c,ZizhanCapCnvIid=0x618d};
uint32_t zizhan_cap_open(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_cap_config(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_cap_read(uint8_t *,uint32_t,uint32_t,uint64_t,uint32_t,uint32_t);
uint32_t zizhan_mfc_loop_device_config(uint8_t *,uint32_t,uint32_t);
uint32_t zizhan_graph_command(uint8_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
