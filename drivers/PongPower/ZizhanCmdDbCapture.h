/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_CMD_DB_CAPTURE_H
#define ZIZHAN_CMD_DB_CAPTURE_H
#include "ZizhanSlpiPower.h"
#include <string.h>
namespace zizhan_cmd_db {
constexpr unsigned Bytes = 0x20000;
enum Result { Ok, Off, Argument, Deadline, Changed, Invalid };
struct Receipt {
    unsigned status = Off, words = 0, copies = 0;
    int parser_result = 0;
    ZizhanSlpiPowerPlan plan = {};
};
/* Reader only exposes read32: no hardware write/vote/PAS path exists.
 * Fixed live DT region80860000/20000. Two full ordered copies must match;
 * this is a stability check, not an atomic hardware snapshot. Exactly65536
 * maximum reads, deadline checked every64words even with a frozen clock.
 * Buffers are private heap allocations owned by the platform worker. */
template <typename Reader, typename Clock>
Result capture(unsigned enabled, Reader &reader, Clock now, uint8_t *a,
               uint8_t *b, size_t bytes, Receipt &out) {
    out = Receipt{};
    if (enabled != 1) return Off;
    if (!a || !b || a == b || bytes != Bytes) {
        out.status = Argument; return Argument;
    }
    const uint64_t begin = now();
    uint8_t *buffers[] = {a, b};
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned word = 0; word < Bytes / 4; ++word) {
            if (!(word & 63) && now() - begin >= 2000000) {
                out.status = Deadline; return Deadline;
            }
            const uint32_t value = reader.read32(word * 4);
            for (unsigned byte = 0; byte < 4; ++byte)
                buffers[pass][word * 4 + byte] = uint8_t(value >> (byte * 8));
            ++out.words;
        }
        ++out.copies;
    }
    if (memcmp(a, b, Bytes)) { out.status = Changed; return Changed; }
    out.parser_result = zizhan_slpi_power_plan(a, Bytes, &out.plan);
    if (out.parser_result) { out.status = Invalid; return Invalid; }
    out.status = Ok; return Ok;
}
} // namespace zizhan_cmd_db
#endif
