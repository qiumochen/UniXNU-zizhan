// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../../../drivers/PongPower/ZizhanPanelProgram.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
using namespace zizhan_panel;

static std::vector<uint8_t> packet(unsigned type, unsigned length, unsigned wait = 0) {
    std::vector<uint8_t> out = {uint8_t(type), 0, 0, 0x40, uint8_t(wait),
                                uint8_t(length >> 8), uint8_t(length)};
    for (unsigned i = 0; i < length; ++i) out.push_back(uint8_t(i));
    if (type == 0x05 && length == 2) out[8] = 0;
    return out;
}
int main(int argc, char **argv) {
    auto fifo = [](const Packet &p) { return p.fits_fifo(); };
    auto all = [](const Packet &) { return true; };
    auto short_cmd = packet(0x05, 2, 10);
    auto long_cmd = packet(0x39, 90, 120);
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        assert(input.good());
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(input)), {});
        Summary s;
        assert(inspect(raw.data(), raw.size(), s) == Result::Ok);
        printf("packets=%u waits=%u dma=%u maximum=%zu\n",
               s.packets, s.waits_ms, s.dma_packets, s.maximum_payload);
        return 0;
    }
    assert(preflight_pair(short_cmd.data(), short_cmd.size(),
                          long_cmd.data(), long_cmd.size(), fifo) == Result::Unsupported);
    assert(preflight_pair(short_cmd.data(), short_cmd.size(),
                          long_cmd.data(), long_cmd.size(), all) == Result::Ok);
    {
        std::vector<uint8_t> bytesIn(90);
        for (unsigned i = 0; i < bytesIn.size(); ++i) bytesIn[i] = uint8_t(i);
        Packet p{0x39, 0, 0x40, 0, bytesIn.data(), bytesIn.size()};
        uint8_t memory[100]{}; size_t used = 0;
        assert(encode_memory(p, memory, sizeof memory, used) == Result::Ok);
        assert(used == 96 && memory[0] == 90 && memory[1] == 0 && memory[2] == 0x39 && memory[3] == 0xc0);
        for (unsigned i = 0; i < 90; ++i) assert(memory[4 + i] == uint8_t(i));
        for (unsigned i = 94; i < used; ++i) assert(memory[i] == 0xff);
        assert(encode_memory(p, memory, 95, used) == Result::Invalid && used == 0);
    }
    for (unsigned length = 1; length <= 252; ++length) {
        auto raw = packet(0x39, length);
        Packet p{}; size_t offset = 0;
        assert(next(raw.data(), raw.size(), offset, p) == Result::Ok);
        uint32_t words[64] = {}; size_t wire = 0, padded = 0;
        assert(encode(p, words, 64, wire, padded) == Result::Ok);
        assert(words[0] == (0xc0390000u | length));
        assert(wire == 1 + (length + 3) / 4 && padded % 2 == 0);
        assert(p.fits_fifo() == (length <= 60));
        for (unsigned i = 0; i < length; ++i)
            assert(uint8_t(words[1 + i / 4] >> (i % 4 * 8)) == uint8_t(i));
        size_t ignored_a, ignored_b;
        assert(encode(p, words, padded - 1, ignored_a, ignored_b) == Result::Invalid);
    }
    for (unsigned kind : {0x05u, 0x15u, 0x39u}) {
        auto raw = packet(kind, 2); raw[7] = 0x28;
        Packet p{}; size_t offset = 0, wire, padded;
        uint32_t words[4]{};
        assert(next(raw.data(), raw.size(), offset, p) == Result::Ok);
        assert(encode(p, words, 4, wire, padded) == Result::Ok);
        assert((words[0] >> 16 & 0x3f) == kind);
        if (kind == 0x05) assert(words[0] == 0x80050028 && wire == 1 && padded == 2);
    }
    // A malformed tail / late oversize packet must cause zero writes.
    for (unsigned bad = 0; bad < 2; ++bad) {
        auto raw = short_cmd;
        if (bad) raw.insert(raw.end(), long_cmd.begin(), long_cmd.end());
        else raw.push_back(0x39);
        unsigned writes = 0, done = 99;
        assert(run(raw.data(), raw.size(), fifo,
                   [&](const Packet &) { ++writes; return Result::Ok; },
                   [](uint32_t) { assert(false); }, [] { return 0ull; }, 1000, done) != Result::Ok);
        assert(!writes && !done);
    }
    // Trigger uncertainty stops all later commands and does not invent ack.
    auto sequence = short_cmd;
    sequence.insert(sequence.end(), short_cmd.begin(), short_cmd.end());
    unsigned sends = 0, done = 0, delays = 0;
    uint64_t now = 0;
    assert(run(sequence.data(), sequence.size(), all,
        [&](const Packet &) { return ++sends == 2 ? Result::Uncertain : Result::Ok; },
        [&](uint32_t ms) { now += ms; ++delays; }, [&] { return now; },
        1000, done) == Result::Uncertain);
    assert(sends == 2 && done == 1 && delays == 1 && now == 10);
    sends = done = delays = 0; now = 0;
    assert(run(short_cmd.data(), short_cmd.size(), all,
        [&](const Packet &) { ++sends; now = 999; return Result::Ok; },
        [&](uint32_t ms) { now += ms; ++delays; }, [&] { return now; },
        1000, done) == Result::Deadline);
    assert(sends == 1 && done == 1 && delays == 1 && now == 1009);
    sends = 0; now = 0;
    assert(run(short_cmd.data(), short_cmd.size(), all,
        [&](const Packet &) { ++sends; return Result::Ok; },
        [](uint32_t) { assert(false); }, [&] { return now; }, 10, done) == Result::Deadline);
    assert(sends == 0 && done == 0);
    puts("PASS: packet types, 252 payload sizes, FIFO/DMA admission, malformed tails, uncertainty and mandatory delays");
}
