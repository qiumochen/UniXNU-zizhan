// SPDX-License-Identifier: MIT
#include "../../../drivers/PongPower/ZizhanDisplayOwner.h"
#include <cassert>
#include <cstdio>
using namespace zizhan_display;
int main() {
    Owner o;
    o.initialize();
    assert(o.present_queued() == Result::Ok);
    assert(o.request_off() == Result::Busy);
    assert(o.take_present() == Result::Ok);
    assert(o.complete_present(true) == Result::Ok);
    assert(o.present_queued() == Result::Ok);
    assert(o.cancel_queued() == Result::Ok);
    assert(o.take_present() == Result::NotReady);
    assert(o.request_off() == Result::Ok);
    auto s = o.snapshot();
    assert(s.state == State::Draining && s.generation == 1 && !s.first_frame);
    assert(o.present_queued() == Result::Busy);
    assert(o.quiesced(s.generation - 1) == Result::Stale);
    assert(o.quiesced(s.generation) == Result::Ok);
    assert(o.off_complete(s.generation, false) == Result::Uncertain);
    assert(o.request_on() == Result::Busy);
    Owner poisoned;
    poisoned.initialize();
    assert(poisoned.present_queued() == Result::Ok);
    assert(poisoned.take_present() == Result::Ok);
    assert(poisoned.uncertain_present() == Result::Uncertain);
    assert(poisoned.request_off() == Result::Busy);
    Owner r;
    r.initialize();
    assert(r.request_off() == Result::Ok);
    auto g = r.snapshot().generation;
    assert(r.quiesced(g) == Result::Ok);
    assert(r.off_complete(g, true) == Result::Ok);
    assert(r.request_on() == Result::Ok);
    auto wake = r.snapshot().generation;
    assert(r.on_complete(wake, true) == Result::Ok);
    assert(r.first_frame_presented(wake - 1, true) == Result::Uncertain);
    assert(r.first_frame_presented(wake, false) == Result::Uncertain);
    assert(r.first_frame_presented(wake, true) == Result::Ok);
    assert(r.snapshot().first_frame);
    puts("PASS: display owner blocks off with pending work, rejects stale generations, requires confirmed off/on and visible first frame");
}
