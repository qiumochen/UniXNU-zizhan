/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <string.h>
#include <array>
#include <string>
#include <vector>
#include "ZizhanDspResources.h"

int main() {
    using namespace zizhan_dsp;
    alignas(8) uint32_t ram[256] = {};
    ram[0] = 0x4d41494c; ram[1] = 1;
    for (unsigned i : {9u, 10u, 11u, 5u, 12u}) ram[i] = 0xffff;
    ram[13] = 0x100; ram[14] = 0x100;
    unsigned kicks = 0, waits = 0;
    QcomQmp qmp;
    qcom_qmp_init(&qmp, ram, sizeof ram,
                  [](void *p) { ++*static_cast<unsigned *>(p); }, &kicks);
    assert(qcom_qmp_step(&qmp) == QcomQmpOpen);
    LoadStateOwner owner;
    std::vector<std::string> sent;
    auto ack = [&](uint32_t ms, auto done) {
        assert(ms == 1000 && ram[64] == QCOM_QMP_MESSAGE);
        ++waits;
        sent.emplace_back(reinterpret_cast<const char *>(&ram[65]));
        assert(!done());
        ram[64] = 0; // remote consumes this actual QcomQmp.c message
        return done();
    };
    assert(!owner.send(qmp, Processor::Adsp, true, ack));
    assert(!owner.send(qmp, Processor::Slpi, true, ack));
    assert(!owner.send(qmp, Processor::Slpi, false, ack));
    assert(sent == std::vector<std::string>({
        "{class: image, res: load_state, name: adsp, val: on}",
        "{class: image, res: load_state, name: slpi, val: on}",
        "{class: image, res: load_state, name: slpi, val: off}"}));
    assert(waits == 3 && kicks == 3);
    auto before = std::array<uint32_t, 256>{};
    memcpy(before.data(), ram, sizeof ram);
    assert(owner.send(qmp, static_cast<Processor>(99), true, ack) == BadProcessor);
    assert(!memcmp(before.data(), ram, sizeof ram) && waits == 3 && kicks == 3);
    ram[64] = 100; // busy remote mailbox even though cached state is open
    assert(owner.send(qmp, Processor::Slpi, true, ack) == QcomQmpBusy);
    assert(ram[64] == 100 && waits == 3 && kicks == 3);
    ram[64] = 0;
    qmp.state = QcomQmpChannelWait;
    assert(owner.send(qmp, Processor::Slpi, true, ack) == MailboxUnavailable);
    assert(!ram[64] && waits == 3 && kicks == 3);
    qmp.state = QcomQmpOpen;
    assert(owner.send(qmp, Processor::Slpi, true, [&](uint32_t ms, auto done) {
        assert(ms == 1000 && !done()); ++waits; return false;
    }) == AckTimeout);
    assert(owner.timed_out && !ram[64]);
    assert(qcom_qmp_step(&qmp) == QcomQmpOpen); // includes a possible late ack
    memcpy(before.data(), ram, sizeof ram);
    assert(owner.send(qmp, Processor::Adsp, true, ack) == MailboxUnavailable);
    assert(!memcmp(before.data(), ram, sizeof ram) && waits == 4 && kicks == 4);

    alignas(8) uint8_t bytes[0x5000] = {};
    QcomSmem smem = {bytes, sizeof bytes};
    auto wr = [&](unsigned at, uint32_t value) { qcom_smem_write32(&smem, at, value); };
    wr(0x4000, 0x434f5424); wr(0x4004, 1); wr(0x4008, 2);
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned base = 0x1000 * (i + 1), toc = 0x4020 + 48 * i;
        wr(toc, base); wr(toc + 4, 0x1000); wr(toc + 12, (i + 2) << 16); wr(toc + 16, 8);
        wr(base, 0x54525024); wr(base + 4, (i + 2) << 16); wr(base + 8, 0x1000);
        wr(base + 12, 32); wr(base + 16, 0x1000);
    }
    QcomSmemPartition adsp = {}, slpi = {};
    assert(!qcom_smem_partition(&smem, 0, 2, &adsp));
    assert(!qcom_smem_partition(&smem, 0, 3, &slpi));
    bool held = false, deny = false;
    unsigned locks = 0, unlocks = 0;
    auto lock = [&] { assert(!held); ++locks; return held = !deny; };
    auto unlock = [&] { assert(held); held = false; ++unlocks; };
    assert(!allocate(smem, adsp, 478, 32, lock, unlock));
    assert(!allocate(smem, slpi, 478, 32, lock, unlock));
    uint32_t a = 0, s = 0, size = 0;
    assert(!qcom_smem_find(&smem, &adsp, 478, &a, &size) && size == 32);
    assert(!qcom_smem_find(&smem, &slpi, 478, &s, &size) && size == 32 && a != s);
    bytes[a] = 0x22; bytes[s] = 0x33;
    std::array<uint8_t, sizeof bytes> saved;
    memcpy(saved.data(), bytes, sizeof bytes);
    assert(!allocate(smem, slpi, 478, 32, lock, unlock)); // existing allocation
    assert(allocate(smem, slpi, 478, 64, lock, unlock) == QcomSmemCorrupt);
    assert(!memcmp(saved.data(), bytes, sizeof bytes));
    deny = true;
    assert(allocate(smem, slpi, 479, 32, lock, unlock) == SmemLockTimeout);
    assert(!held && locks == unlocks + 1 && !memcmp(saved.data(), bytes, sizeof bytes));
    deny = false;
    assert(allocate(smem, slpi, 479, 0, lock, unlock) == QcomSmemCorrupt);
    assert(allocate(smem, slpi, 479, 0x1000, lock, unlock) == QcomSmemNoSpace);
    assert(!held && !memcmp(saved.data(), bytes, sizeof bytes));
    wr(slpi.offset + 32, 0); // corrupt canary: never turn malformed state into success
    assert(allocate(smem, slpi, 479, 32, lock, unlock) == QcomSmemCorrupt);
    assert(!held && locks == unlocks + 1);
}
