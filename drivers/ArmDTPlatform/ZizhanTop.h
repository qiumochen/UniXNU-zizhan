/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* POWER-03 diagnostic (boot-arg zizhan_top=1): every 30 s, the processes that used the most CPU time in that window
 * (user + system, from the task's recount totals; pid 0 is all kernel threads together). Read-only: proc_iterate and
 * recount_task_times only; nothing is scheduled or throttled. */
#pragma once
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <sys/proc.h>

extern "C" {
struct zizhan_recount_times { uint64_t user, system; };
struct zizhan_recount_times recount_task_times(task_t task);
}

namespace zizhan_top {
constexpr unsigned kSlots = 640, kShow = 10;
struct Slot { int pid; uint64_t total, delta; char name[17]; bool seen; };
static Slot gSlots[kSlots];

static int visit(proc_t p, void *) {
  const int pid = proc_pid(p);
  task_t task = proc_task(p);
  if (!task) return 0 /* PROC_RETURNED */;
  const zizhan_recount_times t = recount_task_times(task);
  const uint64_t total = t.user + t.system;
  Slot *free_slot = nullptr;
  for (auto &s : gSlots) {
    if (s.pid == pid && s.name[0]) {
      s.delta = total >= s.total ? total - s.total : 0;
      s.total = total; s.seen = true;
      return 0;
    }
    if (!s.name[0] && !free_slot) free_slot = &s;
  }
  if (free_slot) {
    free_slot->pid = pid; free_slot->total = total; free_slot->delta = 0; free_slot->seen = true;
    proc_name(pid, free_slot->name, sizeof free_slot->name);
    if (!free_slot->name[0]) { free_slot->name[0] = '?'; free_slot->name[1] = 0; }
  }
  return 0;
}

static void worker(void *, wait_result_t) {
  for (unsigned round = 0;; ++round) {
    IOSleep(30000);
    for (auto &s : gSlots) s.seen = false;
    proc_iterate(1 /* PROC_ALLPROCLIST */, visit, nullptr, nullptr, nullptr);
    uint64_t sum = 0;
    for (auto &s : gSlots) {
      if (s.name[0] && !s.seen) { s.name[0] = 0; continue; }   // exited
      sum += s.delta;
    }
    if (!round) continue;   // first window only primes the totals
    char line[384]; unsigned k = 0;
    bool used[kSlots] = {};
    for (unsigned n = 0; n < kShow; ++n) {
      int best = -1;
      for (unsigned i = 0; i < kSlots; ++i)
        if (gSlots[i].name[0] && !used[i] && gSlots[i].delta && (best < 0 || gSlots[i].delta > gSlots[best].delta)) best = int(i);
      if (best < 0) break;
      used[best] = true;
      uint64_t ns = 0; absolutetime_to_nanoseconds(gSlots[best].delta, &ns);
      k += snprintf(line + k, sizeof line - k, " %s[%d]=%llu", gSlots[best].name, gSlots[best].pid, ns / 1000000);
      if (k >= sizeof line - 40) break;
    }
    uint64_t sumNs = 0; absolutetime_to_nanoseconds(sum, &sumNs);
    IOLog("ZIZHAN_TOP: 30s cpu-ms total=%llu%s\n", sumNs / 1000000, line);
  }
}

static void start() {
  uint32_t on = 0;
  if (!PE_parse_boot_argn("zizhan_top", &on, sizeof on) || !on) return;
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
  IOLog("ZIZHAN_TOP: RUNNING (30 s windows)\n");
}
}  // namespace zizhan_top
