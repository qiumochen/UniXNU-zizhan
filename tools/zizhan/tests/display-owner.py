#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[3]


class DisplayOwner(unittest.TestCase):
    def test_contract(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-display-owner-") as tmp:
            binary = str(Path(tmp) / "owner")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/display-owner.cpp"), "-o", binary],
                           check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)

    def test_actual_framebuffer_patch_and_accounting(self):
        # Build only this file from the real ordered patch chain. No private
        # XNU checkout/headers are needed, and patch regressions cannot hide
        # behind a freshly copied generated source tree.
        with tempfile.TemporaryDirectory(prefix="zizhan-owner-patch-") as tmp:
            for name in (ROOT / "patches/xnu-ipados18/series").read_text().splitlines():
                if not name or name.startswith("#"):
                    continue
                subprocess.run(["git", "apply", "--include=iokit/Kernel/MiniIOMobileFramebuffer.cpp",
                                str(ROOT / "patches/xnu-ipados18" / name)],
                               cwd=tmp, check=True, capture_output=True, timeout=10)
            text = (Path(tmp) / "iokit/Kernel/MiniIOMobileFramebuffer.cpp").read_text()

            def block(marker):
                start = text.index(marker)
                opening = text.index("{", start)
                depth = 1
                at = opening + 1
                while depth:
                    depth += (text[at] == "{") - (text[at] == "}")
                    at += 1
                return text[start:at]

            queue = block("if (fDisplayOwnerEnabled && fDisplayOwner.present_queued()")
            take = block("if (fDisplayOwnerEnabled && fDisplayOwner.take_present()")
            finished = block("if (fDisplayOwnerEnabled) {\n\t\t\tconst auto result = fDisplayOwner.software_done()")
            observe = block("IOMobileFramebuffer::observePowerOwner(void)")
            observe = observe[observe.index("{"):]
            harness = r'''
#include "ZizhanDisplayOwner.h"
#include <cassert>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstdarg>
#include <cstring>
static unsigned errors;
static void IOLog(const char *format, ...) { if(strstr(format,"FAIL"))++errors; }
using IOLock=std::mutex;
static void IOLockLock(IOLock *p) {p->lock();}
static void IOLockUnlock(IOLock *p) {p->unlock();}
struct Framebuffer {
    zizhan_display::Owner fDisplayOwner{};
    bool fDisplayOwnerEnabled=true;
    unsigned fSwapsWaiting=0,fDisplayOwnerReports=0;
    IOLock gate;
    IOLock *fSwapLock=&gate;
    std::condition_variable work;
    void observe() OBSERVE
};
int main() {
    Framebuffer f;
    f.fDisplayOwner.initialize();
    constexpr unsigned Count=10000;
    std::thread producer([&] {
        for(unsigned i=0;i<Count;++i) {
            std::unique_lock<std::mutex> lock(f.gate);
            auto &fDisplayOwner=f.fDisplayOwner;
            const bool fDisplayOwnerEnabled=f.fDisplayOwnerEnabled;
            ++f.fSwapsWaiting;
            QUEUE
            f.work.notify_one();
        }
    });
    for(unsigned i=0;i<Count;++i) {
        {
            std::unique_lock<std::mutex> lock(f.gate);
            f.work.wait(lock,[&] {return f.fSwapsWaiting>0;});
            auto &fDisplayOwner=f.fDisplayOwner;
            const bool fDisplayOwnerEnabled=f.fDisplayOwnerEnabled;
            TAKE
            --f.fSwapsWaiting; // actual show() consumes its queue slot
        }
        {
            std::unique_lock<std::mutex> lock(f.gate);
            auto &fDisplayOwner=f.fDisplayOwner;
            const bool fDisplayOwnerEnabled=f.fDisplayOwnerEnabled;
            auto &fDisplayOwnerReports=f.fDisplayOwnerReports;
            auto &fSwapsWaiting=f.fSwapsWaiting;
            FINISHED
        }
    }
    producer.join();
    f.observe();
    const auto s=f.fDisplayOwner.snapshot();
    assert(!errors && !s.queued && !s.in_flight && s.software_completed==Count);
    assert(!s.first_frame); // software return must never invent visibility
}
'''
            for key, value in (("OBSERVE", observe), ("QUEUE", queue), ("TAKE", take), ("FINISHED", finished)):
                harness = harness.replace(key, value)
            source = Path(tmp) / "accounting.cpp"
            source.write_text(harness)
            binary = str(Path(tmp) / "accounting")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-pthread", "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
                            str(source), "-o", binary], check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
