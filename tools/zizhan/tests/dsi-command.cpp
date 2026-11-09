// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../../../drivers/PongPower/ZizhanDsiCommand.h"
#include <cassert>
#include <array>
#include <cstdio>
#include <map>
#include <vector>
using namespace zizhan_dsi;
using zizhan_panel::Result;
struct Io {
    enum Mode { Success, PreBusy, DoneBusy, TimeoutIdle, ErrorIdle } mode=Success;
    std::map<uint32_t,uint32_t> regs{{Version,0x20060000},{Control,0x1f1},
        {DmaControl,0x1234},{DmaOffset,0x87650000},{DmaLength,0x78},
        {DmaFifoControl,0x4000},{TriggerControl,0x55},{PatternControl,0},
        {InterruptControl,Enables|DmaDone}};
    std::vector<uint32_t> fifo;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    uint64_t now=0;
    bool triggered=false, frozen=false, flushed=false;
    uint64_t flushed_address=0;
    unsigned flushed_bytes=0;
    unsigned resets=0;
    uint32_t read(uint32_t reg) {
        if(reg==Status) {
            if(mode==PreBusy)return MdpBusy;
            return triggered && mode==DoneBusy?uint32_t(DmaBusy):0u;
        }
        if(reg==InterruptControl && triggered) {
            if(mode==Success || mode==DoneBusy)return Enables|DmaDone;
            if(mode==ErrorIdle)return Enables|ErrorPending;
            return Enables;
        }
        return regs[reg];
    }
    void write(uint32_t reg,uint32_t value) {
        writes.emplace_back(reg,value);
        if(reg==PatternData)fifo.push_back(value);
        if(reg==DmaTrigger) {
            assert(!(regs[InterruptControl]&DmaDone));
            triggered=true;
        }
        if(reg==FifoReset && value)++resets;
        // Interrupt control combines mask bits and W1C status bits.
        regs[reg]=reg==InterruptControl?(value&Enables):value;
    }
    void delay_us(uint32_t value) {if(!frozen)now+=value;}
    uint64_t now_us() {return now;}
    void flush_buffer(uint8_t *,uint64_t address,unsigned bytes) {
        flushed=true;flushed_address=address;flushed_bytes=bytes;
    }
};
int main() {
    assert(may_release(false,0));
    assert(!may_release(false,DmaBusy));
    assert(!may_release(true,DmaBusy));
    assert(!may_release(true,0)); // idle after timeout does not reset saved state
    uint8_t payload[90]{};payload[0]=0x28;
    for(uint8_t type : {uint8_t(5),uint8_t(0x15),uint8_t(0x39)}) {
        Io io;
        const auto saved=io.regs;
        zizhan_panel::Packet packet{type,0,0,0,payload,2};
        auto o=transfer(io,packet,true);
        assert(o.result==Result::Ok && o.triggered && !o.retain_lease);
        assert((io.fifo[0]>>16&0x3f)==type && io.resets==1);
        assert(io.regs[Control]==saved.at(Control));
        assert(io.regs[DmaControl]==saved.at(DmaControl));
        assert(io.regs[DmaLength]==saved.at(DmaLength));
        assert(io.regs[TriggerControl]==saved.at(TriggerControl));
        assert(io.regs[PatternControl]==saved.at(PatternControl));
    }
    zizhan_panel::Packet packet{0x39,0,0,0,payload,3};
    for(auto mode : {Io::PreBusy,Io::DoneBusy,Io::TimeoutIdle,Io::ErrorIdle}) {
        Io io;io.mode=mode;
        auto o=transfer(io,packet,false);
        if(mode==Io::PreBusy) {
            assert(o.result==Result::Rejected && !o.triggered && io.writes.empty());
        } else {
            assert(o.result==Result::Uncertain && o.triggered);
            assert(o.retain_lease==(mode==Io::DoneBusy));
            assert(io.resets==(mode==Io::DoneBusy?0u:1u));
        }
        assert(io.now<=100020);
    }
    {
        Io io;io.mode=Io::DoneBusy;io.frozen=true;
        auto o=transfer(io,packet,false);
        assert(o.result==Result::Uncertain && o.retain_lease && io.now==0);
    }
    for(unsigned bad=0;bad<4;++bad) {
        Io io;auto p=packet;
        if(bad==0)p.length=90;
        if(bad==1)p.type=0xff;
        if(bad==2)io.regs[Version]=0;
        if(bad==3)io.regs[Control]=0;
        const auto o=transfer(io,p,false);
        assert(o.result!=Result::Ok && !o.triggered && !o.retain_lease && io.writes.empty());
    }
    {
        Io io;
        std::array<uint8_t,128> buffer{};
        const auto saved=io.regs;
        CommandMemory memory{buffer.data(),0x9fff0000,buffer.size(),true};
        auto dmaPacket=packet;dmaPacket.length=90;
        auto o=transfer_memory(io,dmaPacket,false,memory);
        assert(o.result==Result::Ok && o.triggered && !o.retain_lease);
        assert(io.flushed && io.flushed_address==memory.device_address && io.flushed_bytes==96);
        assert(buffer[0]==90 && buffer[1]==0 && buffer[2]==0x39 && buffer[3]==0xc0);
        assert(io.regs[DmaOffset]==saved.at(DmaOffset));
        assert(io.regs[DmaLength]==saved.at(DmaLength));
        assert(io.regs[DmaFifoControl]==saved.at(DmaFifoControl));
        assert(io.regs[Control]==saved.at(Control));
    }
    for(unsigned bad=0;bad<4;++bad) {
        Io io; std::array<uint8_t,95> buffer{};
        CommandMemory memory{buffer.data(),0x9fff0000,buffer.size(),true};
        auto dmaPacket=packet;dmaPacket.length=90;
        if(bad==0)memory.identity_context=false;
        if(bad==1)memory.device_address=0x100000000ull;
        if(bad==2)memory.capacity=buffer.size();
        if(bad==3)memory.virtual_address=nullptr;
        auto o=transfer_memory(io,dmaPacket,false,memory);
        assert(o.result!=Result::Ok && !o.triggered && !io.flushed && io.writes.empty());
    }
    {
        Io io;io.mode=Io::DoneBusy;std::array<uint8_t,128> buffer{};
        CommandMemory memory{buffer.data(),0x9fff0000,buffer.size(),true};
        auto dmaPacket=packet;dmaPacket.length=90;
        auto o=transfer_memory(io,dmaPacket,false,memory);
        assert(o.result==Result::Uncertain && o.triggered && o.retain_lease);
        assert(io.regs[DmaOffset]==memory.device_address);
    }
    puts("PASS: FIFO and low-address identity-DMA command completion, bounds, busy quarantine and register restoration");
}
