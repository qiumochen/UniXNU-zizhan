#!/usr/bin/env python3
"""Production batch core plus both actual owner API branches; synthetic MMIO."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TEST = ROOT / "tools/zizhan/tests/slpi-rpmh-batch.cpp"
harness = r'''
#define ZIZHAN_RPMH_OWNER_TEST
#include "slpi-rpmh-batch.cpp"
#include "ZizhanRpmhReceipt.h"
static FakeWindow rsc;
static void *powerLock = reinterpret_cast<void *>(1);
static unsigned locks, opens_at_lock, legacy;
static bool held;
static zizhan_rpmh::BatchState batchState;
static zizhan_rpmh::CompletionJournal completionJournal;
static void IOLockLock(void *p) { assert(p && !held); held=true; ++locks; opens_at_lock=rsc.opens; }
static void IOLockUnlock(void *p) { assert(p && held); held=false; }
static void IOLog(const char *, ...) { assert(held); }
static void IODelay(unsigned us) { assert(held); rsc.pause(us); }
static uint64_t now() { assert(held); return rsc.now(); }
static bool rawVote(uint32_t,uint32_t) { assert(held); ++legacy; return true; }
/* ACTUAL_OWNER */
int main() {
 zizhan_rpmh::CompletedRequest receipt={1,2,3};
 assert(!pong_rpmh_completed_request(0x50100,&receipt)&&!receipt.address&&!receipt.value&&!receipt.sequence&&locks==1);
 locks=0;
 uint32_t a[3]={0x50100,0x50104,0x50108}, v[3]={1,2,3};
 powerLock=nullptr;
 assert(!pong_rpmh_batch(a,v,3) && !rsc.reads && !locks && rsc.writes.empty());
 powerLock=reinterpret_cast<void *>(1);
 assert(!pong_rpmh_batch(nullptr,v,3) && !rsc.reads && !locks);
 assert(pong_rpmh_batch(a,v,3) && locks==1 && !held && !batchState.uncertain);
 assert(pong_rpmh_vote(0x30100,3) && legacy==1 && locks==2);
 assert(pong_rpmh_completed_request(0x50104,&receipt)&&receipt.address==0x50104&&receipt.value==2&&receipt.sequence==1);
 assert(pong_rpmh_completed_request(0x30100,&receipt)&&receipt.value==3&&receipt.sequence==2);
 assert(!pong_rpmh_completed_request(0x99900,&receipt)&&!receipt.address&&!receipt.value&&!receipt.sequence);
 assert(!pong_rpmh_completed_request(0x30100,nullptr));
 rsc=FakeWindow{}; rsc.v=nullptr; rsc.map_failure=true;
 assert(!pong_rpmh_batch(a,v,3) && rsc.opens==1 && !held && !batchState.uncertain);
 rsc=FakeWindow{}; rsc.stale=true;
 assert(!pong_rpmh_batch(a,v,3) && batchState.uncertain && !held);
 assert(!pong_rpmh_completed_request(0x50104,&receipt)&&!receipt.address&&!receipt.sequence);
 auto writes=rsc.writes; unsigned reads=rsc.reads, before=legacy;
 assert(!pong_rpmh_vote(0x30100,3) && legacy==before && rsc.writes==writes && rsc.reads==reads);
 assert(!pong_rpmh_batch(a,v,3) && rsc.writes==writes && rsc.reads==reads && !held);
}
'''
with tempfile.TemporaryDirectory(prefix="zizhan-rpmh-batch-") as temp:
    temp = Path(temp)
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
             "-I" + str(TEST.parent)]
    binary = temp / "core"
    subprocess.run([*flags, str(TEST), "-o", str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
    for name in ("PongPower/PongRpmh.cpp", "PongWifi/PongWifiPlatform.cpp"):
        text = (ROOT / "drivers" / name).read_text()
        begin = text.index('extern "C" bool pong_rpmh_vote(')
        end = text.index('extern "C" bool pong_smmu_lease_lock(', begin)
        source = temp / "owner.cpp"
        source.write_text(harness.replace("/* ACTUAL_OWNER */", text[begin:end]))
        binary = temp / "owner"
        subprocess.run([*flags, str(source), "-o", str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
        print("PASS actual owner:", name)
