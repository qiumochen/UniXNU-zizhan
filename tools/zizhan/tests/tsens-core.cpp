// SPDX-License-Identifier: MIT
// Execute the production decoder and access gate with a register-read witness.
#include "../../../drivers/ArmDTPlatform/ZizhanTsensCore.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <utility>
using namespace zizhan_tsens;

int main() {
  for (unsigned v = 0; v < 4096; ++v) {
    Sample s = decode((1u << 21) | v | (0x1fu << 16));
    int signed_v = v >= 2048 ? int(v) - 4096 : int(v);
    assert(s.state == State::Pass && s.milli_celsius == signed_v * 100);
    assert(decode(v).state == State::Invalid);
  }
  struct Case { uint32_t version, control, ready; State expected; unsigned reads; };
  for (const Case c : {
      Case{0x10000000, 1, 1, State::UnsupportedVersion, 1},
      Case{0xffffffff, 1, 1, State::UnsupportedVersion, 1},
      Case{0x20000000, 0, 1, State::Disabled, 2},
      Case{0x20000000, 3, 1, State::InReset, 2},
      Case{0x20000000, 1, 0, State::Pass, 19},
      Case{0x20030001, 1, 1, State::Pass, 19},
      Case{0x20080000, 0x9d67fff9, 8, State::Pass, 19}}) {
    std::vector<std::pair<bool, uint32_t>> reads;
    auto out = observe([&](bool srot, uint32_t offset) {
      reads.emplace_back(srot, offset);
      if (srot) {
        assert(offset == 0 || offset == 4);
        return offset == 0 ? c.version : c.control;
      }
      if (offset == 0xe4) return c.ready;
      assert(offset >= 0xa0 && offset <= 0xdc && !(offset % 4));
      return (1u << 21) | 0xf9cu; // -10 C
    });
    assert(out.state == c.expected && reads.size() == c.reads);
    if (c.reads < 19) {
      for (const auto &s : out.sensors) assert(s.state == State::NotRead);
    } else {
      for (const auto &s : out.sensors) assert(s.milli_celsius == -10000);
      for (unsigned i = 0; i < 16; ++i) assert(reads[i+3].second == 0xa0 + 4*i);
    }
  }
  unsigned reads = 0;
  Observation invalid = observe([&](bool srot, uint32_t off) {
    ++reads;
    if (srot) return off == 0 ? 0x20000000u : 1u;
    if (off == 0xe4) return 1u;
    return off == 0xa4 ? 250u : (1u << 21) | 250u;
  });
  assert(reads == 19 && invalid.state == State::Invalid);
  assert(invalid.sensors[1].state == State::Invalid);
  assert(invalid.sensors[0].milli_celsius == 25000);
  Inputs in = {1, 2, {{0xc263000, 511, 0xc222000, 511, 0xffff},
                      {0xc265000, 511, 0xc223000, 511, 0xffff}}};
  assert(inputs_ok(in));
  in.bank[0].tm_bytes = 0xe7; assert(!inputs_ok(in)); in.bank[0].tm_bytes = 511;
  in.bank[0].sensor_mask = 1; assert(!inputs_ok(in)); in.bank[0].sensor_mask = 0xffff;
  in.bank[0].tm = in.bank[1].srot; assert(!inputs_ok(in));
  in.bank[0].tm = UINT64_MAX - 3; assert(!inputs_ok(in));
  puts("PASS TSENS: 4096 signed/invalid encodings, prerequisite read gates, bounded sampling, input rejection");
}
