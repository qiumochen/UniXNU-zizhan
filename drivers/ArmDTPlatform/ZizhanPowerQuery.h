// SPDX-License-Identifier: MIT
// zizhan POWER-02 firmware capability observation. Only VERSION and FEATURES
// are invoked. The queried suspend/mode identifiers are never invoked.
#pragma once
#include <stdint.h>
namespace zizhan_power_query {
constexpr uint32_t Version = 0x84000000u, Features = 0x8400000au;
constexpr uint32_t CpuSuspend64 = 0xc4000001u, SystemSuspend64 = 0xc400000eu;
constexpr uint32_t SetSuspendMode = 0x8400000fu;
enum class State { Pass, SkipVersion, SkipFeatureApi, FailFeatureReply };
struct Feature { uint32_t function; uint64_t raw; int32_t result; };
struct Observation {
    uint64_t raw_version;
    uint32_t version, queries;
    State state;
    Feature api, cpu, system, mode;
};
inline int32_t signed_result(uint64_t value) { return static_cast<int32_t>(uint32_t(value)); }
template<typename Call>
Observation observe(Call call) {
    Observation o{};
    o.raw_version = call(Version, 0); ++o.queries;
    o.version = uint32_t(o.raw_version);
    // FEATURES exists from PSCI 1.0. Do not assume a future major's contract.
    if (signed_result(o.raw_version) < 0 || (o.version >> 16) != 1) {
        o.state = State::SkipVersion; return o;
    }
    o.api = {Features, call(Features, Features), 0}; ++o.queries;
    o.api.result = signed_result(o.api.raw);
    if (o.api.result < 0) {
        o.state = o.api.result == -1 ? State::SkipFeatureApi : State::FailFeatureReply;
        return o;
    }
    Feature *out[] = {&o.cpu, &o.system, &o.mode};
    const uint32_t ids[] = {CpuSuspend64, SystemSuspend64, SetSuspendMode};
    o.state = State::Pass;
    for (unsigned i = 0; i < 3; ++i) {
        out[i]->function = ids[i];
        out[i]->raw = call(Features, ids[i]); ++o.queries;
        out[i]->result = signed_result(out[i]->raw);
        // NOT_SUPPORTED is a complete negative capability observation.
        // Other errors remain errors, not "supported" or a silent zero.
        if (out[i]->result < -1) o.state = State::FailFeatureReply;
    }
    return o;
}
} // namespace zizhan_power_query
