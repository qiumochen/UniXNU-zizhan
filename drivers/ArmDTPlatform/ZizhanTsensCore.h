// SPDX-License-Identifier: MIT
// zizhan TSENS-v2 bounded observation. Register protocol is documented in
// docs/progress/2026-11-07-tsens-readonly.md; no Linux implementation is copied.
#pragma once
#include <stdint.h>

namespace zizhan_tsens {
struct Bank { uint64_t tm, tm_bytes, srot, srot_bytes, sensor_mask; };
struct Inputs { uint64_t format, count; Bank bank[2]; };
static_assert(sizeof(Inputs) == 96, "zizhan TSENS ADT v1 wire size");
inline bool range_ok(uint64_t base, uint64_t bytes, uint64_t needed) {
  return base && !(base & 3) && bytes >= needed && bytes <= 0x1000 &&
         base <= UINT64_MAX - bytes;
}
inline bool inputs_ok(const Inputs &in) {
  if (in.format != 1 || in.count != 2) return false;
  uint64_t bases[4], sizes[4];
  for (unsigned i = 0; i < 2; ++i) {
    const Bank &b = in.bank[i];
    if (b.sensor_mask != 0xffff || !range_ok(b.tm, b.tm_bytes, 0xe8) ||
        !range_ok(b.srot, b.srot_bytes, 8)) return false;
    bases[2*i] = b.tm; sizes[2*i] = b.tm_bytes;
    bases[2*i+1] = b.srot; sizes[2*i+1] = b.srot_bytes;
  }
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = i + 1; j < 4; ++j)
      if (bases[i] < bases[j] + sizes[j] && bases[j] < bases[i] + sizes[i])
        return false;
  return true;
}
enum class State { NotRead, Pass, UnsupportedVersion, Disabled, InReset, Invalid };
struct Sample {
  uint32_t raw;
  int32_t milli_celsius;
  State state;
};
struct Observation {
  uint32_t version, control, raw_trdy;
  State state;
  Sample sensors[16];
};

// One status word supplies BOTH validity and temperature: do not validate one
// read and decode a different conversion. No invalid reading becomes zero C.
inline Sample decode(uint32_t status) {
  if (!(status & (1u << 21))) return {status, 0, State::Invalid};
  int32_t value = static_cast<int32_t>(status & 0xfffu);
  if (value & 0x800) value -= 0x1000;
  return {status, value * 100, State::Pass};
}

// read(srot, offset) must read one aligned 32-bit device register.
// Exactly one finite pass: version, enabled/reset, raw TRDY, then 16 statuses.
// Failed prerequisites prevent all dependent reads, including TM reads.
// v2 get_temp_tsens_valid uses each sensor's VALID; TRDY is NOT its gate.
template <typename Read>
Observation observe(Read read) {
  Observation out = {};
  out.version = read(true, 0x0);
  if ((out.version >> 28) != 2) {
    out.state = State::UnsupportedVersion;
    return out;
  }
  out.control = read(true, 0x4);
  if (!(out.control & 1)) { out.state = State::Disabled; return out; }
  if (out.control & 2) { out.state = State::InReset; return out; }
  out.raw_trdy = read(false, 0xe4);
  out.state = State::Pass;
  for (unsigned i = 0; i < 16; ++i) {
    out.sensors[i] = decode(read(false, 0xa0 + 4 * i));
    if (out.sensors[i].state != State::Pass) out.state = State::Invalid;
  }
  return out;
}
} // namespace zizhan_tsens
