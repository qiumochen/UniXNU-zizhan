/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Explicit boot-arg zizhan_ipcc_route=1 only. Callback/context are permanent
// boot-lifetime objects and must be IRQ-safe (no waits or allocation).
int zizhan_ipcc_routing_enabled(void);
int zizhan_ipcc_subscribe(uint32_t id, void (*callback)(void *, uint32_t), void *context);
// ADSP worker calls this after service(), never from IRQ. At most eight
// snapshots >=10s apart. Reads software counters only, no new hardware access.
void zizhan_ipcc_observe(uint64_t now_ns, uint32_t adsp_callbacks);
#ifdef __cplusplus
}
#endif
