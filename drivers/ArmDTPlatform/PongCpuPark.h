// SPDX-License-Identifier: MIT
// Big-core preference for zizhan (SM8475: cpu0-3 Cortex-A510, cpu4-6 A710, cpu7 X2). The kernel sees one cluster of equal cores
// ("ncpuclusters (1)") and hands work to the lowest idle core number first, which are the little cores. As in UniXNU-sheng 19960fa
// (one parked A510: compositing 40 -> 55 fps), a lowest-priority thread bound to each little core keeps it looking busy while any
// big core is idle, and gives way to real work when none is. boot-arg pong_park_cpus=<mask> (default 0 = off since g67: four spinning cores add heat
// to a port with no thermal management; 0xf parks cpu0-3).
#pragma once
#include <kern/thread.h>

extern "C" processor_t cpu_to_processor(int cpu);
extern "C" processor_t thread_bind(processor_t processor);
extern "C" void thread_set_thread_name(thread_t th, const char *name);
extern "C" struct vnode *rootvnode;
extern "C" kern_return_t kernel_thread_start_priority(thread_continue_t continuation, void *parameter, integer_t priority,
                                                     thread_t *new_thread);

namespace pong_cpu_park {
static void park(void *arg, wait_result_t) {
  thread_set_thread_name(current_thread(), "pong_cpu_park");
  thread_bind(cpu_to_processor((int)(uintptr_t)arg));
  thread_block(THREAD_CONTINUE_NULL);   // takes effect on the bound core from here on
  for (;;) __asm__ volatile("yield");
}

// The platform expert configures before the secondary cores are registered, so the parks start once the root is mounted
// (every core is up by then, and nothing before it is compositing).
static void launch(void *, wait_result_t) {
  uint32_t mask = 0;
  PE_parse_boot_argn("pong_park_cpus", &mask, sizeof(mask));
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i) IOSleep(100);
  unsigned parked = 0;
  for (int cpu = 0; cpu < 8; ++cpu) {
    if (!(mask & (1u << cpu)) || !cpu_to_processor(cpu)) continue;
    thread_t thread;
    if (kernel_thread_start_priority(park, (void *)(uintptr_t)cpu, 1, &thread) == KERN_SUCCESS) {
      thread_deallocate(thread);
      ++parked;
    }
  }
  IOLog("PONG_CPU_PARK: mask 0x%x, %u little cores parked at priority 1\n", mask, parked);
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

// Called once from the platform expert.
static void start(void) {
  uint32_t mask = 0;
  PE_parse_boot_argn("pong_park_cpus", &mask, sizeof(mask));
  if (!mask) return;
  thread_t thread;
  if (kernel_thread_start(launch, nullptr, &thread) == KERN_SUCCESS) thread_deallocate(thread);
}
}  // namespace pong_cpu_park
