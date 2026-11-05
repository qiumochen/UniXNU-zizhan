#!/usr/bin/env python3
"""Replay the actual initialization helper, not an Apple framework or panel."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (ROOT / "userland/ui_compat18.c").read_text()
    start = source.index("static float brightness18_slider(void *display,float slider)")
    begin = source.index("{", start)
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    function = source[start:end]
    harness = r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned original_calls;
static void *original_display;
static float original_input;
static float original(void *p,float v) {
    original_calls++; original_display=p; original_input=v; return 42.f;
}
static float (*brightness18_slider_original)(void *,float)=original;
/* Arbitrary test units, not measured or claimed panel photometry. */
static float brightness18_defaults[3]={1.f,3.f,9.f};
/* ACTUAL_HELPER */
int main(void) {
    struct { unsigned char prefix[0x48]; uintptr_t curve; } state={0}, before=state;
    assert(brightness18_slider(&state,0.f)==1.f);
    assert(brightness18_slider(&state,.25f)==2.f);
    assert(brightness18_slider(&state,.5f)==3.f);
    assert(brightness18_slider(&state,.75f)==6.f);
    assert(brightness18_slider(&state,1.f)==9.f);
    assert(brightness18_slider(&state,-1.f)==1.f);
    assert(brightness18_slider(&state,2.f)==9.f);
    assert(brightness18_slider(&state,NAN)==1.f);
    assert(!memcmp(&state,&before,sizeof state) && original_calls==0);
    state.curve=1;
    assert(brightness18_slider(&state,.625f)==42.f);
    assert(original_calls==1 && original_display==&state && original_input==.625f);
    assert(brightness18_slider(NULL,.125f)==42.f);
    assert(original_calls==2 && !original_display && original_input==.125f);
    puts("PASS: actual brightness helper handles the null initialization map, preserves state and forwards initialized/native paths");
}
"""
    with tempfile.TemporaryDirectory(prefix="zizhan-curve-host-") as td:
        c, binary = Path(td) / "curve.c", Path(td) / "curve"
        c.write_text(harness.replace("/* ACTUAL_HELPER */", function))
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(c), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    main()
