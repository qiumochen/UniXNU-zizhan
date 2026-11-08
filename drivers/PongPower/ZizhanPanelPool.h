#ifndef ZIZHAN_PANEL_POOL_H
#define ZIZHAN_PANEL_POOL_H

#include <stdint.h>

namespace zizhan_panel_pool {

struct Layout {
    unsigned buffers;
    unsigned slots;
    unsigned total_strides;
};

inline bool plan(uint64_t pool_bytes, uint64_t stride, unsigned requested_buffers,
                 unsigned db_capacity, unsigned slot_capacity, Layout *layout)
{
    if (!layout || !stride || db_capacity < 4 || !slot_capacity) return false;
    const uint64_t total64 = pool_bytes / stride;
    if (total64 < 2 || total64 > UINT32_MAX) return false;
    unsigned buffers = requested_buffers < 2 ? 2 : requested_buffers;
    if (buffers > db_capacity - 1) buffers = db_capacity - 1;
    if (buffers > 3) buffers = 3;
    if (buffers > total64) buffers = static_cast<unsigned>(total64);
    unsigned slots = static_cast<unsigned>(total64) - buffers;
    if (slots > slot_capacity) slots = slot_capacity;
    const unsigned db_room = db_capacity - 1 - buffers;
    if (slots > db_room) slots = db_room;
    *layout = {buffers, slots, static_cast<unsigned>(total64)};
    return true;
}

template <unsigned Capacity>
inline unsigned active(const bool (&used)[Capacity], unsigned count)
{
    if (count > Capacity) return Capacity;
    unsigned result = 0;
    for (unsigned i = 0; i < count; ++i) if (used[i]) ++result;
    return result;
}

template <unsigned Capacity>
inline bool take(const uint64_t (&physical)[Capacity], bool (&used)[Capacity],
                 unsigned count, uint64_t *address)
{
    if (address) *address = 0;
    if (!address || !count || count > Capacity) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (used[i]) continue;
        used[i] = true;
        *address = physical[i];
        return *address != 0;
    }
    return false;
}

template <unsigned Capacity>
inline bool give(const uint64_t (&physical)[Capacity], bool (&used)[Capacity],
                 unsigned count, uint64_t address)
{
    if (!address || !count || count > Capacity) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (physical[i] != address) continue;
        if (!used[i]) return false;
        used[i] = false;
        return true;
    }
    return false;
}

} // namespace zizhan_panel_pool

#endif
