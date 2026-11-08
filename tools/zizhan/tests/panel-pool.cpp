#include "../../../drivers/PongPower/ZizhanPanelPool.h"
#include <cassert>
#include <cstdint>

int main()
{
    constexpr uint64_t MiB = 1024ull * 1024;
    constexpr uint64_t stride = 0x1100000ull;
    zizhan_panel_pool::Layout layout{};
    assert(zizhan_panel_pool::plan(96 * MiB, stride, 2, 8, 8, &layout));
    assert(layout.total_strides == 5 && layout.buffers == 2 && layout.slots == 3);
    assert(zizhan_panel_pool::plan(128 * MiB, stride, 2, 8, 8, &layout));
    assert(layout.total_strides == 7 && layout.buffers == 2 && layout.slots == 5);
    assert(zizhan_panel_pool::plan(128 * MiB, stride, 3, 8, 8, &layout));
    assert(layout.buffers == 3 && layout.slots == 4);
    assert(zizhan_panel_pool::plan(256 * MiB, stride, 3, 8, 8, &layout));
    assert(layout.buffers + layout.slots + 1 == 8);
    assert(!zizhan_panel_pool::plan(1, stride, 2, 8, 8, &layout));
    assert(!zizhan_panel_pool::plan(128 * MiB, 0, 2, 8, 8, &layout));
    assert(!zizhan_panel_pool::plan(128 * MiB, stride, 2, 3, 8, &layout));

    const uint64_t addresses[5] = {0x10000000, 0x11100000, 0x12200000, 0x13300000, 0x14400000};
    bool used[5] = {};
    uint64_t address = 0;
    for (unsigned i = 0; i < 5; ++i) assert(zizhan_panel_pool::take(addresses, used, 5, &address));
    assert(zizhan_panel_pool::active(used, 5) == 5);
    assert(!zizhan_panel_pool::take(addresses, used, 5, &address) && address == 0);
    assert(zizhan_panel_pool::give(addresses, used, 5, addresses[2]));
    assert(zizhan_panel_pool::active(used, 5) == 4);
    assert(zizhan_panel_pool::take(addresses, used, 5, &address) && address == addresses[2]);
    assert(!zizhan_panel_pool::give(addresses, used, 5, 0xdeadbeef));
    assert(zizhan_panel_pool::give(addresses, used, 5, addresses[0]));
    assert(!zizhan_panel_pool::give(addresses, used, 5, addresses[0]));
    return 0;
}
