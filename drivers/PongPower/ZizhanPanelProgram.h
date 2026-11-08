// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan L18 DT command interpreter. No MMIO or implicit DMA mapping.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace zizhan_panel {
constexpr size_t MaximumProgramBytes = 4096, MaximumPayload = 252;
constexpr unsigned MaximumPackets = 128;
enum class Result { Ok, Invalid, Unsupported, Rejected, Uncertain, Deadline };
struct Packet {
    uint8_t type, channel, flags, wait_ms;
    const uint8_t *payload;
    size_t length;
    bool long_packet() const { return type == 0x39; }
    size_t wire_bytes() const { return 4 + (long_packet() ? (length + 3) / 4 * 4 : 0); }
    bool fits_fifo() const { return wire_bytes() <= 64; }
};
struct Summary {
    unsigned packets = 0, waits_ms = 0, dma_packets = 0;
    size_t maximum_payload = 0;
};

// MSM DSI command-DMA memory format (dsi_ctrl_copy_and_pad_cmd):
// header bytes {data0,data1,data-type,last|payload-flag}, then payload, then
// 0xff pad to a DWORD. This is deliberately separate from the FIFO word
// encoding used by the timing/backlight helper.
inline Result encode_memory(const Packet &p, uint8_t *out, size_t capacity, size_t &bytes) {
    bytes = 0;
    if (!out || !p.payload || p.channel || p.type != 0x39 || !p.length || p.length > MaximumPayload)
        return Result::Invalid;
    const size_t padded = (p.length + 3) & ~size_t(3), total = 4 + padded;
    if (capacity < total) return Result::Invalid;
    const uint8_t lo = uint8_t(p.length), hi = uint8_t(p.length >> 8);
    out[0] = lo; out[1] = hi; out[2] = p.type;
    out[3] = 0xc0; // controller computes ECC; LAST_COMMAND | long-payload
    for (size_t i = 0; i < padded; ++i) out[4 + i] = i < p.length ? p.payload[i] : 0xff;
    bytes = total;
    return Result::Ok;
}

// Qualcomm packet header: type, legacy-last, VC, flags, wait-ms, big-endian
// length. Type 0x05 may carry the vendor's zero dummy second byte; it remains
// a short *no parameter* packet. Never infer packet type from payload length.
inline Result next(const uint8_t *program, size_t bytes, size_t &offset, Packet &p) {
    if (!program || offset > bytes || bytes - offset < 7) return Result::Invalid;
    const uint8_t *h = program + offset;
    const size_t length = size_t(h[5]) * 256 + h[6];
    if (!length || length > MaximumPayload || length > bytes - offset - 7 ||
        h[1] > 1 || h[2] != 0 || (h[3] & ~0x40u)) return Result::Invalid;
    if (h[0] != 0x05 && h[0] != 0x15 && h[0] != 0x39) return Result::Unsupported;
    if ((h[0] == 0x05 && (length > 2 || (length == 2 && h[8]))) ||
        (h[0] == 0x15 && length != 2)) return Result::Invalid;
    p = {h[0], h[2], h[3], h[4], h + 7, length};
    offset += 7 + length;
    return Result::Ok;
}

inline Result inspect(const uint8_t *program, size_t bytes, Summary &answer) {
    answer = {};
    if (!bytes || bytes > MaximumProgramBytes) return Result::Invalid;
    size_t offset = 0;
    Summary s;
    while (offset < bytes) {
        Packet p{};
        Result result = next(program, bytes, offset, p);
        if (result != Result::Ok) return result;
        if (++s.packets > MaximumPackets) return Result::Invalid;
        s.waits_ms += p.wait_ms;
        s.dma_packets += !p.fits_fifo();
        if (p.length > s.maximum_payload) s.maximum_payload = p.length;
    }
    answer = s;
    return Result::Ok;
}

template <typename Supports>
Result preflight(const uint8_t *program, size_t bytes, Supports supports) {
    Summary s;
    Result result = inspect(program, bytes, s);
    if (result != Result::Ok) return result;
    size_t offset = 0;
    while (offset < bytes) {
        Packet p{};
        result = next(program, bytes, offset, p);
        if (result != Result::Ok) return result;
        if (!supports(p)) return Result::Unsupported;
    }
    return Result::Ok;
}

template <typename Supports>
Result preflight_pair(const uint8_t *off, size_t off_bytes,
                      const uint8_t *on, size_t on_bytes, Supports supports) {
    Result result = preflight(off, off_bytes, supports);
    return result == Result::Ok ? preflight(on, on_bytes, supports) : result;
}

// Encode the embedded DSI header and payload for either FIFO or a previously
// mapped DMA buffer. The final padding DWORD required by FIFO is separate
// from DMA_CMD_LENGTH and cannot make an oversized packet fit the FIFO.
inline Result encode(const Packet &p, uint32_t *words, size_t capacity,
                     size_t &wire_words, size_t &fifo_words) {
    wire_words = fifo_words = 0;
    if (!p.payload || !p.length || p.length > MaximumPayload || p.channel ||
        (p.type != 0x05 && p.type != 0x15 && p.type != 0x39) ||
        (p.type == 0x05 && (p.length > 2 || (p.length == 2 && p.payload[1]))) ||
        (p.type == 0x15 && p.length != 2)) return Result::Invalid;
    const size_t n = p.wire_bytes() / 4, padded = n + (n & 1);
    if (!words || capacity < padded) return Result::Invalid;
    for (size_t i = 0; i < padded; ++i) words[i] = 0;
    words[0] = 0x80000000u | uint32_t(p.type) << 16;
    if (p.long_packet()) {
        words[0] |= 0x40000000u | uint32_t(p.length);
        for (size_t i = 0; i < p.length; ++i)
            words[1 + i / 4] |= uint32_t(p.payload[i]) << (i % 4 * 8);
        for (size_t i = p.length; i < (n - 1) * 4; ++i)
            words[1 + i / 4] |= 0xffu << (i % 4 * 8);
    } else {
        words[0] |= p.payload[0];
        if (p.type == 0x15) words[0] |= uint32_t(p.payload[1]) << 8;
    }
    wire_words = n; fifo_words = padded;
    return Result::Ok;
}

// Full preflight precedes the first send. The transaction owner must also
// call preflight_pair with its resume before admitting off. A FIFO-only
// backend must reject an oversized resume before shutting down the panel.
// Time and deadline are monotonic milliseconds.
// send may report Rejected only if it changed no
// hardware; timeout/error after trigger is Uncertain and aborts the program.
template <typename Supports, typename Send, typename Delay, typename Now>
Result run(const uint8_t *program, size_t bytes, Supports supports,
           Send send, Delay delay, Now now, uint64_t deadline, unsigned &completed) {
    completed = 0;
    Result result = preflight(program, bytes, supports);
    if (result != Result::Ok) return result;
    size_t offset = 0;
    while (offset < bytes) {
        Packet p{};
        result = next(program, bytes, offset, p);
        if (result != Result::Ok) return result;
        const uint64_t time = now();
        if (time >= deadline || uint64_t(p.wait_ms) >= deadline - time)
            return Result::Deadline;
        result = send(p);
        if (result != Result::Ok) return result;
        ++completed;
        // Post-command delay is mandatory, even if the outer deadline has
        // just expired. Never shorten a vendor delay to claim timely exit.
        if (p.wait_ms) delay(p.wait_ms);
        if (now() >= deadline) return Result::Deadline;
    }
    return Result::Ok;
}
} // namespace zizhan_panel
