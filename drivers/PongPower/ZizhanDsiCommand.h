// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan POWER-02: finite FIFO command executor, shared with MMIO replay.
// Caller owns the DSI lease across this call and any retained busy failure.
#pragma once
#include "ZizhanPanelProgram.h"
namespace zizhan_dsi {
enum : uint32_t {
    Version=0, Control=4, Status=8, DmaControl=0x3c, DmaOffset=0x48,
    DmaLength=0x4c, DmaFifoControl=0x50,
    TriggerControl=0x84, DmaTrigger=0x90, InterruptControl=0x110,
    PatternControl=0x15c, PatternData=0x17c, FifoReset=0x1ec,
    DmaBusy=1u<<1, MdpBusy=1u<<2, DmaDone=1u, ErrorPending=1u<<24,
    Enables=(1u<<1)|(1u<<9)|(1u<<11)|(1u<<13)|(1u<<15)|(1u<<17)|
            (1u<<21)|(1u<<25)|(1u<<29)|(1u<<31)
};
struct CommandMemory {
    uint8_t *virtual_address;
    uint64_t device_address;
    size_t capacity;
    bool identity_context;
};
struct Outcome {
    zizhan_panel::Result result;
    bool triggered, retain_lease;
};
// A retained transfer remains quarantined even if a later status read has
// gone idle: its saved registers still need an explicit recovery transaction.
inline bool may_release(bool quarantined, uint32_t status) {
    return !quarantined && !(status&DmaBusy);
}
// Io read/write/delay_us/now_us must access the already verified DSI0
// controller. No mapper, allocation, mode/PLL or panel reset is performed.
template <typename Io>
Outcome transfer(Io &io, const zizhan_panel::Packet &packet, bool low_power) {
    using zizhan_panel::Result;
    uint32_t words[16]{};
    size_t length=0, padded=0;
    if (!packet.fits_fifo()) return {Result::Unsupported,false,false};
    if (zizhan_panel::encode(packet,words,16,length,padded)!=Result::Ok)
        return {Result::Invalid,false,false};
    if ((io.read(Version)>>28)!=2 || !(io.read(Control)&1u))
        return {Result::Rejected,false,false};
    const uint64_t start=io.now_us();
    // A bounded iteration cap backs the clock budget. Neither a stuck timer
    // nor a continuously busy auto-refresh transfer permits an infinite wait.
    unsigned tries=0;
    while (io.read(Status)&(DmaBusy|MdpBusy)) {
        if (++tries>=2500 || io.now_us()-start>=50000)
            return {Result::Rejected,false,false};
        io.delay_us(20);
    }
    if ((io.read(PatternControl)&1u) || (io.read(InterruptControl)&ErrorPending))
        return {Result::Rejected,false,false};
    const uint32_t control=io.read(Control), dma=io.read(DmaControl),
        bytes=io.read(DmaLength), trigger=io.read(TriggerControl), pattern=io.read(PatternControl);
    io.write(Control,control|4u);
    io.write(InterruptControl,(io.read(InterruptControl)&Enables)|DmaDone);
    io.write(TriggerControl,(trigger&~0xfu)|4u);
    io.write(PatternControl,0x30006);
    for (size_t i=0;i<padded;++i) io.write(PatternData,words[i]);
    io.write(DmaControl,(dma&~((3u<<30)|(1u<<29)|(1u<<26)|(1u<<24)))|
                       (1u<<28)|(low_power?1u<<26:0u));
    io.write(DmaLength,uint32_t(length*4));
    io.write(DmaTrigger,1);
    bool complete=false, error=false;
    tries=0;
    do {
        const uint32_t irq=io.read(InterruptControl), status=io.read(Status);
        error=(irq&ErrorPending)!=0;
        complete=(irq&DmaDone) && !(status&DmaBusy);
        if (error || complete) break;
        io.delay_us(20);
    } while (++tries<2500 && io.now_us()-start<100000);
    // DONE is insufficient while DMA_BUSY remains asserted. No FIFO reset,
    // register restoration, or DSI lease release until actual DMA idleness.
    if (io.read(Status)&DmaBusy) return {Result::Uncertain,true,true};
    io.write(InterruptControl,(io.read(InterruptControl)&Enables)|DmaDone);
    io.write(PatternControl,0);
    io.write(FifoReset,1); io.delay_us(1); io.write(FifoReset,0);
    io.write(DmaControl,dma); io.write(DmaLength,bytes);
    io.write(TriggerControl,trigger); io.write(PatternControl,pattern);
    io.write(Control,control);
    return {complete&&!error?Result::Ok:Result::Uncertain,true,false};
}

// Long packets use a board-reserved low physical page only while the caller
// has freshly verified that the fixed display context is identity/bypass.
// No SMMU programming or implicit IOVA is performed here.
template <typename Io>
Outcome transfer_memory(Io &io, const zizhan_panel::Packet &packet,
                        bool low_power, const CommandMemory &memory) {
    using zizhan_panel::Result;
    if (!packet.long_packet() || packet.fits_fifo() || !memory.identity_context ||
        !memory.virtual_address || !memory.device_address ||
        memory.device_address > UINT32_MAX || (memory.device_address & 3u))
        return {Result::Rejected,false,false};
    size_t length = 0;
    if (zizhan_panel::encode_memory(packet, memory.virtual_address,
                                    memory.capacity, length) != Result::Ok ||
        !length || length > 0x00ffffffu)
        return {Result::Invalid,false,false};
    if ((io.read(Version)>>28)!=2 || !(io.read(Control)&1u))
        return {Result::Rejected,false,false};
    const uint64_t start=io.now_us();
    unsigned tries=0;
    while (io.read(Status)&(DmaBusy|MdpBusy)) {
        if (++tries>=2500 || io.now_us()-start>=50000)
            return {Result::Rejected,false,false};
        io.delay_us(20);
    }
    if ((io.read(PatternControl)&1u) || (io.read(InterruptControl)&ErrorPending))
        return {Result::Rejected,false,false};

    const uint32_t control=io.read(Control), dma=io.read(DmaControl),
        offset=io.read(DmaOffset), bytes=io.read(DmaLength),
        fifo=io.read(DmaFifoControl), trigger=io.read(TriggerControl);
    io.flush_buffer(memory.virtual_address, memory.device_address,
                    static_cast<unsigned>(length));
    io.write(Control,control|4u);
    io.write(InterruptControl,(io.read(InterruptControl)&Enables)|DmaDone);
    io.write(TriggerControl,(trigger&~0xfu)|4u);
    io.write(DmaFifoControl,fifo|(1u<<20)|(1u<<16));
    io.write(DmaControl,(dma&~((3u<<30)|(1u<<29)|(1u<<26)|(1u<<24)))|
                       (1u<<28)|(low_power?1u<<26:0u));
    io.write(DmaOffset,static_cast<uint32_t>(memory.device_address));
    io.write(DmaLength,static_cast<uint32_t>(length));
    io.write(DmaTrigger,1);
    bool complete=false, error=false;
    tries=0;
    do {
        const uint32_t irq=io.read(InterruptControl), status=io.read(Status);
        error=(irq&ErrorPending)!=0;
        complete=(irq&DmaDone) && !(status&DmaBusy);
        if (error || complete) break;
        io.delay_us(20);
    } while (++tries<2500 && io.now_us()-start<100000);
    if (io.read(Status)&DmaBusy) return {Result::Uncertain,true,true};

    io.write(InterruptControl,(io.read(InterruptControl)&Enables)|DmaDone);
    io.write(DmaOffset,offset); io.write(DmaLength,bytes);
    io.write(DmaFifoControl,fifo); io.write(DmaControl,dma);
    io.write(TriggerControl,trigger); io.write(Control,control);
    return {complete&&!error?Result::Ok:Result::Uncertain,true,false};
}
} // namespace zizhan_dsi
