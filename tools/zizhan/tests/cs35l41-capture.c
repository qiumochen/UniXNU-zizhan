/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host unit tests for ZizhanAudioCapture.h and ADSP TX capture graph packets (ShengAudioReach.c). */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ShengAudioReach.h"
#include "ShengGpr.h"
#include "ZizhanAudioCapture.h"
#include "ZizhanAudioServiceGate.h"

static void test_control_page_coexistence(void) {
  uint8_t page[ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES];
  memset(page, 0xa5, sizeof(page));
  volatile ZizhanAudioControl *c = (volatile ZizhanAudioControl *)page;
  assert(zizhan_service_control_init(c, sizeof(page), 19200000ull, 1000ull));
  assert(sizeof(ZizhanAudioControl) == 112u);
  assert(!zizhan_audio_capture_valid(zizhan_audio_capture_slot_const(c)));

  /* Initializing capture extension at offset 128 preserves v2 playback control validity */
  assert(zizhan_audio_capture_init(c, sizeof(page), 2000ull));
  assert(zizhan_audio_control_valid(c));
  volatile ZizhanAudioCaptureControl *cap = zizhan_audio_capture_slot(c);
  assert(zizhan_audio_capture_valid(cap));
  assert(cap->anchor_host == 2000ull && cap->cap_written == 0ull && cap->cap_seq == 0u);
  assert(!zizhan_audio_capture_init(c, ZIZHAN_AUDIO_CAPTURE_OFFSET + sizeof(ZizhanAudioCaptureControl) - 1u, 0));
}

static void test_decimation_ring_wrap_and_bounds(void) {
  uint8_t page[ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES];
  volatile ZizhanAudioControl *c = (volatile ZizhanAudioControl *)page;
  assert(zizhan_service_control_init(c, sizeof(page), 19200000ull, 0));
  assert(zizhan_audio_capture_init(c, sizeof(page), 0));
  volatile ZizhanAudioCaptureControl *cap = zizhan_audio_capture_slot(c);

  static int16_t ring[ZIZHAN_AUDIO_RING_FRAMES];
  memset(ring, 0, sizeof(ring));

  int16_t src96[ZIZHAN_AUDIO_CAP_96K_STEREO_FRAMES * 2];
  for (unsigned i = 0; i < ZIZHAN_AUDIO_CAP_96K_STEREO_FRAMES; ++i) {
    src96[2 * i] = 16384;      /* L = +0.5 */
    src96[2 * i + 1] = 8192;   /* R = +0.25 */
  }

  /* Push near ring wrap boundary (65536 - 240) to test wrap-around */
  zizhan_audio_capture_publish_written(cap, ZIZHAN_AUDIO_RING_FRAMES - 240u);
  const uint32_t pushed = zizhan_audio_capture_push_96k_stereo(
      cap, ring, src96, ZIZHAN_AUDIO_CAP_96K_STEREO_FRAMES, 1);
  assert(pushed == 480u);
  uint64_t written = 0;
  assert(zizhan_audio_capture_written_stable(cap, &written));
  assert(written == ZIZHAN_AUDIO_RING_FRAMES + 240u);

  /* Stereo mix average of (16384 + 8192 + 16384 + 8192)/4 = 12288 -> 12288 / 32768 = 0.375f */
  float out[480];
  assert(cap->cap_requests == 0u);
  const uint32_t got = zizhan_audio_capture_read_f32(
      cap, ring, ZIZHAN_AUDIO_RING_FRAMES - 240u, 480u, 1, out);
  assert(got == 480u && cap->underruns == 0u && cap->overflows == 0u && cap->cap_requests == 1u);
  assert(fabsf(out[0] - 0.375f) < 1e-4f && fabsf(out[479] - 0.375f) < 1e-4f);

  /* Future read underruns cleanly to 0.0f */
  const uint32_t got_future = zizhan_audio_capture_read_f32(
      cap, ring, written, 120u, 1, out);
  assert(got_future == 0u && cap->underruns == 1u && out[0] == 0.0f && out[119] == 0.0f);

  /* Overwritten history (< written - 65536) zero-fills and counts overflow */
  const uint32_t got_old = zizhan_audio_capture_read_f32(
      cap, ring, 0ull, 16u, 1, out);
  assert(got_old == 0u && cap->overflows == 1u && out[0] == 0.0f);

  /* Odd seqlock (concurrent kernel update) fails closed to silence */
  cap->cap_seq = 1u;
  assert(!zizhan_audio_capture_written_stable(cap, &written));
  assert(zizhan_audio_capture_read_f32(cap, ring, ZIZHAN_AUDIO_RING_FRAMES, 16u, 1, out) == 0u);

  /* Direct 48 kHz mono push + client notify */
  cap->cap_seq = 0u;
  zizhan_audio_capture_publish_written(cap, 0ull);
  int16_t mono48[480];
  for (unsigned i = 0; i < 480u; ++i) mono48[i] = -8192; /* -0.25f */
  uint32_t req_before = cap->cap_requests;
  zizhan_audio_capture_notify_client(cap);
  assert(cap->cap_requests == req_before + 1u);
  assert(zizhan_audio_capture_push_48k_mono(cap, ring, mono48, 480u) == 480u);
  assert(zizhan_audio_capture_read_f32(cap, ring, 0ull, 480u, 1, out) == 480u);
  assert(fabsf(out[0] + 0.25f) < 1e-4f && fabsf(out[479] + 0.25f) < 1e-4f);
}

static void test_adsp_tx_graph_packets(void) {
  uint8_t pkt[1024];
  GprPacket g;
  const uint32_t n_open = zizhan_cap_open(pkt, sizeof(pkt), 0x11u);
  assert(n_open > 0u && gpr_decode(pkt, n_open, &g) == GprOk);
  assert(g.opcode == AR_OP_GRAPH_OPEN && g.token == 0x11u);

  const uint32_t n_cfg = zizhan_cap_config(pkt, sizeof(pkt), 0x12u);
  assert(n_cfg > 0u && gpr_decode(pkt, n_cfg, &g) == GprOk);
  assert(g.opcode == AR_OP_SET_CFG && g.token == 0x12u);

  const uint32_t n_rd = zizhan_cap_read(pkt, sizeof(pkt), 0x13u, 0x9ef00000ull, 0x55aa1234u, ZIZHAN_AUDIO_CAP_96K_PERIOD_BYTES);
  assert(n_rd > 0u && gpr_decode(pkt, n_rd, &g) == GprOk);
  assert(g.opcode == AR_OP_READ && g.token == 0x13u && g.payload_bytes == 32u);

  ArFormat fmt = {48000u, 16u, 1u};
  const uint32_t n_mic = ar_open_mic_device(pkt, sizeof(pkt), 0x2005u, 0x21u);
  assert(n_mic > 0u && gpr_decode(pkt, n_mic, &g) == GprOk && g.opcode == AR_OP_GRAPH_OPEN);
  const uint32_t n_mic_cfg = ar_config_mic_device(pkt, sizeof(pkt), 0x2005u, 0x22u, &fmt, 1u);
  assert(n_mic_cfg > 0u && gpr_decode(pkt, n_mic_cfg, &g) == GprOk && g.opcode == AR_OP_SET_CFG);
  const uint32_t n_cap_str = ar_config_capture_stream(pkt, sizeof(pkt), 0x2005u, 0x23u, &fmt);
  assert(n_cap_str > 0u && gpr_decode(pkt, n_cap_str, &g) == GprOk && g.opcode == AR_OP_SET_CFG);
}

int main(void) {
  test_control_page_coexistence();
  test_decimation_ring_wrap_and_bounds();
  test_adsp_tx_graph_packets();
  puts("PASS zizhan audio capture ring + ADSP TX graph packets: v2 page coexistence, 96k->48k decimation, seqlock, underrun/overflow zero-fill; no hardware");
  return 0;
}
