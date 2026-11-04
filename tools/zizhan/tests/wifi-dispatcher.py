#!/usr/bin/env python3
"""Replay the pinned dispatcher's actual timer pass before/after adaptation."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
START = "  u64 now = lx_now_ms();\n"
END = "  /* one item at a time off the shared list"


def timer_pass(source):
    pump = source.index("int lx_pump(void)")
    start = source.index(START, pump)
    return source[start:source.index(END, start)]


HARNESS = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint64_t u64;
struct list_head { struct list_head *next, *prev; };
struct work_struct { struct list_head entry; void (*func)(struct work_struct *); };
struct delayed_work { struct work_struct work; int armed; u64 due; };
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static struct list_head g_timers = { &g_timers, &g_timers };
static const void *g_running[32];
static int g_nrunning, calls[3], scenario;
static struct delayed_work timers[3];
static u64 lx_now_ms(void) { return 100; }
static int is_running(const void *p) {
    for (int i=0; i<g_nrunning; i++) if (g_running[i]==p) return 1;
    return 0;
}
static void where(int k, const void *p) { (void)k; (void)p; }
static void list_del_init(struct list_head *p) {
    p->prev->next=p->next; p->next->prev=p->prev; p->next=p->prev=p;
}
static void arm(int i, u64 due) {
    struct delayed_work *d=&timers[i];
    assert(!d->armed);
    struct list_head *p=&d->work.entry;
    p->prev=g_timers.prev; p->next=&g_timers;
    p->prev->next=p; g_timers.prev=p;
    d->armed=1; d->due=due;
}
static int pump(void) {
    int ran=0;
    /* ACTUAL_TIMER_PASS */
    return ran;
}
static void callback(struct work_struct *w) {
    int i=(int)(container_of(w,struct delayed_work,work)-timers);
    calls[i]++;
    if (scenario==0 && i==0) {
        assert(pump()); /* Inner pump removes outer traversal's next node. */
        assert(calls[1]==1 && calls[2]==1);
    } else if (scenario==1 && i==0) {
        timers[1].armed=0;
        list_del_init(&timers[1].work.entry); /* Cancel the cached successor. */
    } else if (scenario==2 && i==0 && calls[0]==1) {
        arm(0,100);
        assert(pump()); /* Running A is skipped; B/C still execute. */
        assert(calls[0]==1 && calls[1]==1 && calls[2]==1);
    } else if (scenario==3 && i==0) {
        arm(0,100); /* Continually ready work must yield to the board loop. */
    }
}
int main(int argc,char **argv) {
    assert(argc==2); scenario=atoi(argv[1]);
    for(int i=0;i<3;i++) {
        timers[i].work.entry.next=timers[i].work.entry.prev=&timers[i].work.entry;
        timers[i].work.func=callback;
    }
    arm(0,100);
    if(scenario!=3) { arm(1,scenario==4 ? 101 : 100); arm(2,100); }
    assert(pump());
    assert(g_nrunning==0);
    if(scenario==0) assert(calls[0]==1 && calls[1]==1 && calls[2]==1);
    if(scenario==1) assert(calls[0]==1 && calls[1]==0 && calls[2]==1);
    if(scenario==2) assert(calls[0]==2 && calls[1]==1 && calls[2]==1);
    if(scenario==3) assert(calls[0]==256 && timers[0].armed);
    if(scenario==4) assert(calls[0]==1 && calls[1]==0 && calls[2]==1 && timers[1].armed);
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shared", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("ath11k_build", ROOT / "scripts/build-pong-ath11k.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    source = (args.shared / "shim/src/lx_rt.c").read_text()
    adapted = build.adapt_dispatcher_timers(source)
    with tempfile.TemporaryDirectory(prefix="zizhan-dispatcher-") as td:
        for name, content in (("original", source), ("adapted", adapted)):
            c = Path(td) / (name + ".c")
            binary = Path(td) / name
            c.write_text(HARNESS.replace("/* ACTUAL_TIMER_PASS */", timer_pass(content)))
            subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            str(c), "-o", str(binary)], check=True)
            if name == "original":
                try:
                    subprocess.run([str(binary), "0"], check=True, timeout=1)
                except subprocess.TimeoutExpired:
                    print("PASS: original timer pass reproduces the recursive-pump hang")
                else:
                    raise AssertionError("original traversal unexpectedly returned")
            else:
                for case in range(5):
                    subprocess.run([str(binary), str(case)], check=True, timeout=2)
                print("PASS: adapted timer pass handles recursive removal, cancellation, "
                      "running rearm, bounded ready work and future deadlines")
    try:
        build.adapt_dispatcher_timers(source.replace(START, "  /* changed ABI */\n"))
    except RuntimeError:
        print("PASS: dispatcher adaptation rejects an unexpected upstream source")
    else:
        raise AssertionError("unexpected dispatcher ABI was accepted")


if __name__ == "__main__":
    main()
