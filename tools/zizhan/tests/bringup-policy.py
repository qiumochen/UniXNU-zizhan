#!/usr/bin/env python3
"""Exercise production preference planning and actual UI application/rollback."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def function(text, name):
    start = text.index("static bool " + name + "(void)")
    begin = text.index("{", start)
    depth, end = 1, begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


@unittest.skipUnless(sys.platform == "darwin", "actual CoreFoundation replay needs macOS")
class BringupPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="zizhan-bringup-")
        td = Path(cls.temp.name)
        helper = function((ROOT / "userland/ui_compat18.c").read_text(), "zizhan_bringup_preferences")
        harness = r'''
#include "zizhan-bringup18.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CFTypeRef value, marker;
static bool bringup, legacy, fail_sync;
static unsigned syncs;
static void replace(CFTypeRef *slot,CFTypeRef next) {
    if(next)CFRetain(next);
    if(*slot)CFRelease(*slot);
    *slot=next;
}
static bool boot_option(const char *s) {
    return !strcmp(s,"zizhan_bringup=1")?bringup:
        !strcmp(s,"zizhan_autodim_restore=1")?legacy:false;
}
static CFPropertyListRef TestCopy(CFStringRef key,CFStringRef app) {
    (void)app;CFTypeRef v=CFEqual(key,ZIZHAN_AUTODIM_KEY)?value:marker;
    return v?CFRetain(v):NULL;
}
static void TestSet(CFStringRef key,CFPropertyListRef next,CFStringRef app) {
    (void)app;
    replace(CFEqual(key,ZIZHAN_AUTODIM_KEY)?&value:&marker,next);
}
static Boolean TestSync(CFStringRef app) {
    (void)app;++syncs;
    if(fail_sync && syncs==1)return false;
    return true;
}
#define CFPreferencesCopyAppValue TestCopy
#define CFPreferencesSetAppValue TestSet
#define CFPreferencesAppSynchronize TestSync
/* ACTUAL_UI_APPLY */
static void reset(CFTypeRef prior) {
    replace(&value,prior);replace(&marker,NULL);
    bringup=false;legacy=false;fail_sync=false;syncs=0;
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"default")) {
        reset(kCFBooleanTrue);
        assert(!zizhan_bringup_preferences() && value==kCFBooleanTrue && !marker && !syncs);
    } else if(!strcmp(argv[1],"legacy")) {
        reset(kCFBooleanTrue);legacy=true;
        assert(!zizhan_bringup_preferences() && !value && !marker && syncs==1);
        reset(kCFBooleanFalse);legacy=true;
        assert(!zizhan_bringup_preferences() && value==kCFBooleanFalse && !syncs);
        reset(CFSTR("native-value"));legacy=true;
        assert(!zizhan_bringup_preferences() && CFEqual(value,CFSTR("native-value")) && !syncs);
    } else if(!strcmp(argv[1],"owned")) {
        CFTypeRef original[]={NULL,kCFBooleanFalse,kCFBooleanTrue,CFSTR("native-value")};
        for(unsigned i=0;i<4;++i) {
            reset(original[i]);bringup=true;
            assert(zizhan_bringup_preferences() && value==kCFBooleanTrue && zizhan_autodim_marker_valid(marker));
            CFTypeRef same=CFRetain(marker);syncs=0;
            assert(zizhan_bringup_preferences() && marker==same && !syncs);CFRelease(same);
            bringup=false;
            assert(!zizhan_bringup_preferences() && !marker);
            assert(original[i]?value && CFEqual(value,original[i]):!value);
        }
    } else if(!strcmp(argv[1],"external")) {
        reset(NULL);bringup=true;assert(zizhan_bringup_preferences());
        replace(&value,CFSTR("changed-by-native"));bringup=false;
        assert(!zizhan_bringup_preferences() && !marker && CFEqual(value,CFSTR("changed-by-native")));
    } else if(!strcmp(argv[1],"invalid")) {
        reset(kCFBooleanTrue);replace(&marker,CFSTR("foreign"));bringup=true;
        assert(!zizhan_bringup_preferences() && value==kCFBooleanTrue && CFEqual(marker,CFSTR("foreign")) && !syncs);
        reset(NULL);bringup=true;legacy=true;
        assert(!zizhan_bringup_preferences() && !value && !marker && !syncs);
    } else if(!strcmp(argv[1],"failed-sync")) {
        reset(kCFBooleanFalse);bringup=true;fail_sync=true;
        assert(!zizhan_bringup_preferences() && value==kCFBooleanFalse && !marker && syncs==2);
        reset(NULL);bringup=true;assert(zizhan_bringup_preferences());
        CFTypeRef saved=CFRetain(marker);bringup=false;syncs=0;fail_sync=true;
        assert(!zizhan_bringup_preferences() && value==kCFBooleanTrue && CFEqual(marker,saved) && syncs==2);
        CFRelease(saved);
    } else assert(false);
    reset(NULL);
    return 0;
}
'''
        source = td / "bringup.c"
        source.write_text(harness.replace("/* ACTUAL_UI_APPLY */", helper))
        cls.binary = td / "bringup"
        subprocess.run(["xcrun", "--sdk", "macosx", "clang", "-std=c11", "-fblocks",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "userland"),
                        str(source), "-framework", "CoreFoundation", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, case):
        subprocess.run([str(self.binary), case], check=True, timeout=10)

    def test_daily_default_does_not_adopt_unmarked_preference(self):
        self.check("default")

    def test_explicit_legacy_only_removes_boolean_true(self):
        self.check("legacy")

    def test_owned_original_and_repeated_bringup(self):
        self.check("owned")

    def test_external_native_change_preserved(self):
        self.check("external")

    def test_unknown_marker_and_conflicting_modes(self):
        self.check("invalid")

    def test_failed_persistence_rolls_back_both_keys(self):
        self.check("failed-sync")


if __name__ == "__main__":
    unittest.main()
