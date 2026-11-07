// SPDX-License-Identifier: MIT
// zizhan POWER-02: finite owner transaction model, with no hardware executor.
// Call only under the coordinator's serial gate. A capability declaration is
// not hardware qualification. Unknown/uncertain completion never becomes Awake.
#pragma once
#include <stdint.h>

namespace zizhan_power {
enum class Phase { Awake, Preparing, Quiescing, Committing, Ready, Sleeping,
                   Resuming, Verifying, RollingBack, Failed };
enum class Step { Prepare, Quiesce, LowPower, Resume, Verify, Rollback, None };
enum class Reply { Success, Rejected, Uncertain };
enum class Result { Ok, Invalid, Busy, Constrained, MissingOwner, MissingCapability,
                    DependencyCycle, Stale, Deadline, NoExecutor, WakeChanged, Uncertain };
struct Ticket {
    uint64_t generation = 0, sequence = 0;
    unsigned owner = 0;
    Step step = Step::None;
    bool valid() const { return generation && sequence && step != Step::None; }
};
struct Snapshot {
    Phase phase;
    uint64_t generation, sequence;
    uint32_t required, touched, uncertain, completed[6];
    bool pending, canceled;
};

class Transaction {
public:
    static constexpr unsigned OwnerCount = 16;
    static constexpr uint32_t AllCapabilities = 0x1f; // prepare/quiesce/low/resume/verify
private:
    struct Owner { uint32_t dependencies = 0, capabilities = 0; };
    Owner owners_[OwnerCount]{};
    Phase phase_ = Phase::Awake;
    uint64_t generation_ = 0, sequence_ = 0, deadline_ = 0, wake_ = 0;
    uint32_t registered_ = 0, required_ = 0, touched_ = 0, uncertain_ = 0;
    uint32_t completed_[6]{};
    Ticket pending_{};
    bool canceled_ = false;

    bool graph_ok(uint32_t mask) const {
        uint32_t left = mask;
        for (unsigned pass = 0; pass < OwnerCount && left; ++pass) {
            uint32_t ready = 0;
            for (unsigned i = 0; i < OwnerCount; ++i)
                if ((left & (1u << i)) && !(owners_[i].dependencies & left))
                    ready |= 1u << i;
            if (!ready) return false;
            left &= ~ready;
        }
        return !left;
    }
    Step current_step() const {
        switch (phase_) {
            case Phase::Preparing: return Step::Prepare;
            case Phase::Quiescing: return Step::Quiesce;
            case Phase::Committing: return Step::LowPower;
            case Phase::Resuming: return Step::Resume;
            case Phase::Verifying: return Step::Verify;
            case Phase::RollingBack: return Step::Rollback;
            default: return Step::None;
        }
    }
    void rollback() {
        phase_ = touched_ ? Phase::RollingBack : Phase::Awake;
    }
    void advance(Step step) {
        if (canceled_ && step != Step::Rollback) { rollback(); return; }
        const uint32_t target = step == Step::Rollback ? touched_ : required_;
        if ((completed_[unsigned(step)] & target) != target) return;
        switch (step) {
            case Step::Prepare: phase_ = Phase::Quiescing; break;
            case Step::Quiesce: phase_ = Phase::Committing; break;
            case Step::LowPower: phase_ = Phase::Ready; break;
            case Step::Resume: phase_ = Phase::Verifying; break;
            case Step::Verify:
            case Step::Rollback: phase_ = Phase::Awake; break;
            default: break;
        }
    }
public:
    Result owner(unsigned id, uint32_t dependencies, uint32_t capabilities) {
        if (phase_ != Phase::Awake) return Result::Busy;
        if (id >= OwnerCount || dependencies >> OwnerCount ||
            (dependencies & (1u << id)) || capabilities & ~AllCapabilities)
            return Result::Invalid;
        owners_[id] = {dependencies, capabilities};
        registered_ |= 1u << id;
        return Result::Ok;
    }
    Result begin(uint32_t required, uint64_t now, uint64_t deadline,
                 uint64_t wake_epoch, uint32_t constraints) {
        if (phase_ != Phase::Awake) return Result::Busy;
        if (!required || required >> OwnerCount || deadline <= now ||
            generation_ == UINT64_MAX) return Result::Invalid;
        if (constraints) return Result::Constrained;
        if (required & ~registered_) return Result::MissingOwner;
        for (unsigned i = 0; i < OwnerCount; ++i) if (required & (1u << i)) {
            if (owners_[i].dependencies & ~required) return Result::MissingOwner;
            if (owners_[i].capabilities != AllCapabilities) return Result::MissingCapability;
        }
        if (!graph_ok(required)) return Result::DependencyCycle;
        ++generation_; sequence_ = 0; deadline_ = deadline; wake_ = wake_epoch;
        required_ = required; touched_ = uncertain_ = 0; canceled_ = false;
        for (auto &v : completed_) v = 0;
        pending_ = {}; phase_ = Phase::Preparing;
        return Result::Ok;
    }
    Result expire(uint64_t now) {
        if (phase_ == Phase::Awake || phase_ == Phase::Failed) return Result::Invalid;
        if (now < deadline_) return Result::Ok;
        // A callback that may still be doing DMA cannot be canceled by clearing
        // a software ticket. Quarantine it and retain every resource/owner.
        if (pending_.valid()) uncertain_ |= 1u << pending_.owner;
        phase_ = Phase::Failed;
        return Result::Deadline;
    }
    Ticket issue(uint64_t now) {
        if (phase_ == Phase::Awake || phase_ == Phase::Failed) return {};
        if (expire(now) != Result::Ok) return {};
        const Step step = current_step();
        if (step == Step::None || pending_.valid()) return {};
        const bool restoring = step == Step::Resume || step == Step::Verify || step == Step::Rollback;
        const uint32_t target = step == Step::Rollback ? touched_ : required_;
        const uint32_t left = target & ~completed_[unsigned(step)];
        for (unsigned i = 0; i < OwnerCount; ++i) if (left & (1u << i)) {
            bool blocked = false;
            if (restoring) blocked = (owners_[i].dependencies & left) != 0;
            else for (unsigned j = 0; j < OwnerCount; ++j)
                if ((left & (1u << j)) && (owners_[j].dependencies & (1u << i))) blocked = true;
            if (!blocked) {
                if (sequence_ == UINT64_MAX) { phase_ = Phase::Failed; return {}; }
                pending_ = {generation_, ++sequence_, i, step};
                return pending_;
            }
        }
        phase_ = Phase::Failed; // graph/protocol invariant, never fake completion
        return {};
    }
    Result acknowledge(Ticket ticket, Reply reply, uint64_t now) {
        if (!pending_.valid() || phase_ == Phase::Failed ||
            ticket.generation != pending_.generation || ticket.sequence != pending_.sequence ||
            ticket.owner != pending_.owner || ticket.step != pending_.step)
            return Result::Stale;
        if (expire(now) != Result::Ok) return Result::Deadline;
        const Step step = ticket.step;
        const uint32_t bit = 1u << ticket.owner;
        if (reply != Reply::Success && reply != Reply::Rejected && reply != Reply::Uncertain) {
            uncertain_ |= bit; phase_ = Phase::Failed;
            return Result::Invalid;
        }
        if (reply == Reply::Uncertain) {
            uncertain_ |= bit; phase_ = Phase::Failed;
            return Result::Uncertain;
        }
        pending_ = {};
        if (reply == Reply::Rejected) {
            // Rejected means the failed step made no change. Prepared owners
            // are still touched and require verified rollback.
            if (step == Step::Resume || step == Step::Verify || step == Step::Rollback)
                phase_ = Phase::Failed;
            else { canceled_ = true; rollback(); }
            return Result::Invalid;
        }
        completed_[unsigned(step)] |= bit;
        if (step == Step::Prepare) touched_ |= bit;
        advance(step);
        return Result::Ok;
    }
    Result cancel(uint64_t now) {
        if (phase_ == Phase::Awake || phase_ == Phase::Failed || phase_ == Phase::Sleeping ||
            phase_ == Phase::Resuming || phase_ == Phase::Verifying)
            return Result::Invalid;
        if (expire(now) != Result::Ok) return Result::Deadline;
        canceled_ = true;
        if (!pending_.valid()) rollback();
        return Result::Ok;
    }
    Result enter(uint64_t generation, uint64_t wake_epoch, uint32_t constraints,
                 bool executor_ready, uint64_t now) {
        if (phase_ != Phase::Ready || generation != generation_) return Result::Invalid;
        if (expire(now) != Result::Ok) return Result::Deadline;
        if (wake_epoch != wake_) { canceled_ = true; rollback(); return Result::WakeChanged; }
        if (constraints) { canceled_ = true; rollback(); return Result::Constrained; }
        if (!executor_ready) return Result::NoExecutor;
        phase_ = Phase::Sleeping;
        return Result::Ok; // model event only; this class never invokes firmware
    }
    Result wake(uint64_t generation, uint64_t now) {
        if (phase_ != Phase::Sleeping || generation != generation_) return Result::Stale;
        if (expire(now) != Result::Ok) return Result::Deadline;
        phase_ = Phase::Resuming;
        return Result::Ok;
    }
    Snapshot snapshot() const {
        Snapshot s{phase_, generation_, sequence_, required_, touched_, uncertain_, {},
                   pending_.valid(), canceled_};
        for (unsigned i = 0; i < 6; ++i) s.completed[i] = completed_[i];
        return s;
    }
};
} // namespace zizhan_power
