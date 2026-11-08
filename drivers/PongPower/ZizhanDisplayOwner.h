// SPDX-License-Identifier: MIT
// POWER-02 display owner contract. This model never touches DSI/DPU registers.
#pragma once
#include <stdint.h>

namespace zizhan_display {
enum class State { On = 0, Draining, OffPending, Off, OnPending, Failed };
enum class Result { Ok, Busy, Invalid, Stale, NotReady, Uncertain };
struct Snapshot { State state; uint64_t generation; unsigned queued, in_flight; bool first_frame; uint64_t software_completed; };

class Owner {
    State state_;
    uint64_t generation_;
    unsigned queued_, in_flight_;
    bool first_frame_;
    uint64_t software_completed_;
public:
    // Keep the kernel's static instance trivially zero-initialized; no C++
    // global constructor is assumed for a member of the static driver archive.
    void initialize() { state_ = State::On; generation_ = 0; queued_ = in_flight_ = 0; first_frame_ = false; software_completed_ = 0; }
    Result present_queued() {
        if (state_ != State::On) return Result::Busy;
        if (queued_ == UINT32_MAX) return Result::Invalid;
        ++queued_; return Result::Ok;
    }
    Result cancel_queued() {
        if (state_ != State::On || !queued_) return Result::NotReady;
        --queued_; return Result::Ok;
    }
    Result take_present() {
        if ((state_ != State::On && state_ != State::Draining) || !queued_ || in_flight_ == UINT32_MAX)
            return Result::NotReady;
        --queued_; ++in_flight_; return Result::Ok;
    }
    // Used by the real framebuffer presenter after show() and surface-release.
    // Software return is not proof of DPU latch, visibility or DMA retirement.
    Result software_done() {
        if ((state_ != State::On && state_ != State::Draining) || !in_flight_ || software_completed_ == UINT64_MAX)
            return Result::NotReady;
        --in_flight_; ++software_completed_; return Result::Ok;
    }
    Result complete_present(bool hardware_confirmed) {
        if (state_ != State::On || !in_flight_) return Result::NotReady;
        if (!hardware_confirmed) { state_ = State::Failed; return Result::Uncertain; }
        --in_flight_; first_frame_ = true; return Result::Ok;
    }
    Result abandon_present() {
        if (state_ != State::On || !in_flight_) return Result::NotReady;
        --in_flight_; return Result::Ok;
    }
    Result uncertain_present() {
        if (state_ != State::On || !in_flight_) return Result::NotReady;
        --in_flight_; state_ = State::Failed; return Result::Uncertain;
    }
    Result request_off() {
        if (state_ != State::On || queued_ || in_flight_) return Result::Busy;
        if (generation_ == UINT64_MAX) return Result::Invalid;
        ++generation_; first_frame_ = false; state_ = State::Draining; return Result::Ok;
    }
    Result quiesced(uint64_t generation) {
        if (state_ != State::Draining || generation != generation_) return Result::Stale;
        state_ = State::OffPending; return Result::Ok;
    }
    Result off_complete(uint64_t generation, bool hardware_confirmed) {
        if (state_ != State::OffPending || generation != generation_) return Result::Stale;
        if (!hardware_confirmed) { state_ = State::Failed; return Result::Uncertain; }
        state_ = State::Off; return Result::Ok;
    }
    Result request_on() {
        if (state_ != State::Off) return Result::Busy;
        if (generation_ == UINT64_MAX) return Result::Invalid;
        ++generation_; state_ = State::OnPending; first_frame_ = false; return Result::Ok;
    }
    Result on_complete(uint64_t generation, bool hardware_confirmed) {
        if (state_ != State::OnPending || generation != generation_) return Result::Stale;
        if (!hardware_confirmed) { state_ = State::Failed; return Result::Uncertain; }
        state_ = State::On; return Result::Ok;
    }
    Result first_frame_presented(uint64_t generation, bool visible) {
        if (state_ != State::On || generation != generation_ || !visible) return Result::Uncertain;
        first_frame_ = true; return Result::Ok;
    }
    Result fail() { state_ = State::Failed; return Result::Ok; }
    Snapshot snapshot() const { return {state_, generation_, queued_, in_flight_, first_frame_, software_completed_}; }
};
} // namespace zizhan_display
