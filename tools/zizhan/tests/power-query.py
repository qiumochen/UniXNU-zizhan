#!/usr/bin/env python3
"""Run the production query whitelist and display observer with mocked I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class PowerQueryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="zizhan-power-query-")
        td = Path(cls.temp.name)
        cls.query = td / "query"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                   "-Werror", "-fsanitize=address,undefined", "-pthread"]
        subprocess.run([*command, str(ROOT / "tools/zizhan/tests/power-query.cpp"),
                        "-o", str(cls.query)], check=True, timeout=30)
        text = (ROOT / "drivers/ArmDTPlatform/ZizhanPowerAudit.h").read_text()
        helper = text[text.index('extern "C" void zizhan_display_power_observe('):]
        harness = r'''
#include "ZizhanPowerRequest.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
namespace zizhan_power_audit {static volatile uint32_t request_slots;}
static unsigned mode;
static std::vector<std::string> logs;
static bool PE_parse_boot_argn(const char *name,void *out,size_t n) {
    assert(!strcmp(name,"zizhan_power_observe") && n==4);
    memcpy(out,&mode,4);return true;
}
static void IOLog(const char *format,...) {
    char line[1024];va_list args;va_start(args,format);
    vsnprintf(line,sizeof line,format,args);va_end(args);logs.emplace_back(line);
}
/* ACTUAL_OBSERVER */
int main(int argc,char **argv) {
    assert(argc==2);
    uint64_t values[]={1,2,3};
    if(!strcmp(argv[1],"off")) {
        zizhan_display_power_observe(3,values,32);
        assert(logs.empty() && !zizhan_power_audit::request_slots);
    } else if(!strcmp(argv[1],"observe")) {
        mode=1;
        for(unsigned i=0;i<100;++i)zizhan_display_power_observe(3,values,32);
        assert(logs.size()==33 && zizhan_power_audit::request_slots==33);
        for(unsigned i=0;i<32;++i)
            assert(logs[i].find("SKIP observe-only=1")!=std::string::npos &&
                   logs[i].find("raw=0x1/0x2/0x3 hardware-transition=UNIMPLEMENTED")!=std::string::npos);
        assert(logs[32].find("output-limit=32")!=std::string::npos);
        assert(values[0]==1 && values[1]==2 && values[2]==3);
    } else if(!strcmp(argv[1],"malformed")) {
        mode=1;zizhan_display_power_observe(2,nullptr,64);
        assert(logs.size()==1 && logs[0].find("FAIL malformed-scalars")!=std::string::npos);
    } else if(!strcmp(argv[1],"concurrent")) {
        volatile uint32_t count=0;
        std::atomic<unsigned> seen[33]{};
        std::vector<std::thread> workers;
        for(unsigned t=0;t<8;++t)workers.emplace_back([&] {
            for(unsigned i=0;i<2000;++i) {
                uint32_t slot=zizhan_power_request::reserve(&count);
                if(slot<33)seen[slot].fetch_add(1);
            }
        });
        for(auto &t:workers)t.join();
        assert(count==33);
        for(auto &s:seen)assert(s==1);
        assert(zizhan_power_request::reserve(&count)==33 && count==33);
    } else assert(false);
}
'''
        source = td / "observer.cpp"
        source.write_text(harness.replace("/* ACTUAL_OBSERVER */", helper))
        cls.observer = td / "observer"
        subprocess.run([*command, "-I" + str(ROOT / "drivers/ArmDTPlatform"),
                        str(source), "-o", str(cls.observer)], check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, binary, case):
        subprocess.run([str(binary), case], check=True, timeout=15)

    def test_only_queries_issued_with_complete_negative_capability(self):
        self.check(self.query, "supported-negative")

    def test_version_prerequisite_skips_all_features(self):
        self.check(self.query, "version-gate")

    def test_signed_extension_and_errors_not_converted_to_support(self):
        self.check(self.query, "errors")

    def test_observer_disabled_has_no_log_or_counter(self):
        self.check(self.observer, "off")

    def test_actual_observer_only_logs_and_saturates(self):
        self.check(self.observer, "observe")

    def test_actual_observer_rejects_null_scalars(self):
        self.check(self.observer, "malformed")

    def test_concurrent_slot_reservation_never_wraps_or_duplicates(self):
        self.check(self.observer, "concurrent")


if __name__ == "__main__":
    unittest.main()
