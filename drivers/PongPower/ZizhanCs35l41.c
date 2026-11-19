/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ZizhanCs35l41.h"
#include "ZizhanCs35l41TrimMap.h"
#include <string.h>

/* Wire/identity facts: Linux v6.6 cs35l41-lib.c regmap_i2c and
 * include/sound/cs35l41.h; no codec initialization sequence imported. */
static uint32_t wire_be32(uint32_t fifo) {
    return (fifo & 255) << 24 | (fifo & 0xff00) << 8 |
           (fifo >> 8 & 0xff00) | (fifo >> 24);
}
static int poll_until(GeniI2c *bus, const ZizhanAmpClock *clock, unsigned budget) {
    const uint64_t start = clock->now_us(clock->context);
    for (unsigned i = 0; i < budget / 20; ++i) {
        const int rc = geni_i2c_poll(bus);
        if (rc != GeniPending) return rc;
        if (clock->now_us(clock->context) - start >= budget) break;
        clock->pause_us(clock->context, 20);
    }
    return GeniPending;
}
static int finish_transfer(GeniI2c *bus, const ZizhanAmpClock *clock, ZizhanAmpIdentity *out) {
    int rc = poll_until(bus, clock, 20000);
    if (rc == GeniDone) return 0;
    if (rc == GeniFailed) {
        out->bus_error = bus->error; out->irq_error = bus->error_status;
        // A bus error while STOP_STRETCH is set may leave the physical bus
        // held despite the generic core's Idle flag. Keep exclusive ownership.
        out->controller_uncertain = 1;
        return ZizhanAmpBus;
    }
    for (unsigned stage = 0; stage < 2; ++stage) {
        rc = geni_i2c_timeout(bus); // cancel, then abort if not acknowledged
        if (rc != GeniPending) break;
        rc = poll_until(bus, clock, 2000);
        if (rc != GeniPending) break;
    }
    // Never pretend the generic final software Idle step proves hardware
    // quiescent after an unacknowledged abort. Retain regardless of outcome.
    out->bus_error = bus->error; out->irq_error = bus->error_status;
    out->controller_uncertain = 1;
    return ZizhanAmpTimeout;
}
static int read_identity(GeniI2c *bus, const ZizhanAmpClock *clock, unsigned reg,
                         uint32_t *value, ZizhanAmpIdentity *out) {
    // Function is private and reg is restricted here, not a generic regmap.
    if (reg != 0 && reg != 4 && reg != 0x1001c && reg != 0x10018 &&
        reg != 0x10 && reg != 0x2014 && reg != 0x2018 && reg != 0x2bc1000 &&
        reg != 0x4100 && reg != 0x4310 && reg != 0x4400 && reg != 0x381c &&
        reg != 0x2bc20e0 && reg != 0x2bc2020 &&
        !(reg >= 0x400 && reg <= 0x47c && !(reg & 3)) && !zizhan_amp_trim_register(reg)) return ZizhanAmpArgument;
    const uint8_t request[4] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16), (uint8_t)(reg >> 8), (uint8_t)reg};
    if (geni_i2c_start_read_bytes(bus, request, 4, 4)) return ZizhanAmpBusy;
    const int reason = finish_transfer(bus, clock, out);
    if (!reason) *value = wire_be32(bus->rx);
    return reason;
}

/* Actual mode9 config proof; strict APIs never use this exception. */
static int owned_config(const ZizhanAmpBusConfig *c, int allow_last) {
    const uint32_t *v = c->values;
    return c->valid == ZizhanAmpConfigWords && c->phase == GeniIdle &&
        !v[0] && v[1] == 0x303 && v[2] == 0x303 && !(v[3] & ~0x80u) &&
        v[4] == GENI_M_IRQ_WANTED && !(v[4] & 0x80) && !(v[3] & v[4]) &&
        (allow_last ? !(v[5] & ~0x08000000u) : !v[5]) && v[6] == 0x03001e36 &&
        !v[7] && !v[8] && !v[9] && v[10] == 0x7f8fe && v[11] == 0xffefe &&
        !v[12] && !v[13] && v[14] == 15 && v[15] == 16 &&
        !v[16] && v[17] == 0xd && !v[18] && v[19] == 0x1d &&
        !v[20] && v[21] == 15 && v[22] == 7 && !v[23];
}
static int identify(unsigned enabled, unsigned verified, GeniI2c *bus,
                    const ZizhanAmpClock *clock, ZizhanAmpIdentity *out, int boot) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out); out->reason = ZizhanAmpOff;
    if (enabled != 1) return 0; // default-off, including unsupported modes
    const unsigned required = boot ? ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified | ZizhanAmpResetReleased |
                                     ZizhanAmpUnmanagedSupplyTopology : ZizhanAmpPrerequisites;
    if (verified != required) { out->reason = ZizhanAmpPrerequisite; return 0; }
    if (!bus || !bus->io.read || !bus->io.write || !clock || !clock->now_us || !clock->pause_us) {
        out->status = ZizhanAmpFail; out->reason = ZizhanAmpArgument; return out->reason;
    }
    out->entry_phase = bus->phase; out->prerequisite_valid = 1;
    if (geni_i2c_busy(bus)) { out->busy_sources = 1; out->reason = ZizhanAmpBusy; return 0; }
    const uint32_t proto = (bus->io.read(bus->io.ctx, GENI_FW_REVISION) >> 8) & 255;
    if (proto != GENI_PROTO_I2C || (bus->io.read(bus->io.ctx, GENI_IF_DISABLE) & 1) ||
        !(bus->io.read(bus->io.ctx, GENI_CLK_CFG) & 1) || !bus->io.read(bus->io.ctx, GENI_SCL_COUNTERS) ||
        (bus->io.read(bus->io.ctx, GENI_DMA_MODE) & 1) ||
        bus->io.read(bus->io.ctx, GENI_TX_PACK0) != 0x7f8fe ||
        bus->io.read(bus->io.ctx, GENI_TX_PACK1) != 0xffefe ||
        bus->io.read(bus->io.ctx, GENI_RX_PACK0) != 0x7f8fe ||
        bus->io.read(bus->io.ctx, GENI_RX_PACK1) != 0xffefe) {
        out->reason = ZizhanAmpProtocol; return 0;
    }
    // Reject an actual active GENI command or stale IRQ/FIFO data before any
    // command write. The caller must prepare/clear safely under its lease.
    // Sample each raw input once, including when a prior sample rejects.
    // This is read-only evidence; never clear/ignore unknown completion,
    // error or level bits to force the next stage through its guard.
    out->entry_se_status = bus->io.read(bus->io.ctx, 0x40);
    out->entry_irq = bus->io.read(bus->io.ctx, GENI_M_IRQ_STATUS);
    out->entry_rx_fifo = bus->io.read(bus->io.ctx, GENI_RX_FIFO_STATUS);
    out->prerequisite_valid |= 2;
    out->busy_sources = (out->entry_se_status & 0x1001 ? 2u : 0u) |
                        (out->entry_irq ? 4u : 0u) | (out->entry_rx_fifo & 0x1ffffff ? 8u : 0u);
    if (boot == 2) {
        ZizhanAmpBusConfig config;
        const int result = zizhan_cs35l41_bus_config(1, verified, bus, &config);
        if (result || !owned_config(&config, 0)) { out->reason = ZizhanAmpBusy; return 0; }
    } else if (out->busy_sources) { out->reason = ZizhanAmpBusy; return 0; }
    const uint8_t original_address = bus->address;
    const uint8_t addresses[] = {0x40, 0x42};
    for (unsigned i = 0; i < 2; ++i) {
        bus->address = addresses[i];
        int reason = 0;
        if (boot) {
            const uint64_t begin = clock->now_us(clock->context);
            for (unsigned attempt = 0; attempt < 64; ++attempt) {
                reason = read_identity(bus, clock, 0x1001c, &out->otp_status[i], out);
                if (reason || (out->otp_status[i] & 2)) break;
                if (clock->now_us(clock->context) - begin >= 100000) break;
                clock->pause_us(clock->context, 1000);
            }
            if (!reason && !(out->otp_status[i] & 2)) reason = ZizhanAmpOtpTimeout;
            if (!reason) reason = read_identity(bus, clock, 0x10018, &out->otp_error[i], out);
            if (!reason && (out->otp_error[i] & 0x80000000u)) reason = ZizhanAmpOtpError;
            if (!reason) ++out->digital_ready;
        }
        if (!reason) reason = read_identity(bus, clock, 0, &out->devid[i], out);
        if (!reason) reason = read_identity(bus, clock, 4, &out->revid[i], out);
        if (!reason) {
            const uint32_t expected = (out->revid[i] & 1) ? 0x35b40 : 0x35a40;
            if (out->devid[i] != expected) reason = ZizhanAmpWrongIdentity;
        }
        if (reason) {
            if (!out->controller_uncertain) bus->address = original_address;
            out->status = ZizhanAmpFail; out->reason = reason; return reason;
        }
        ++out->completed;
    }
    bus->address = original_address;
    out->status = ZizhanAmpPass; out->reason = 0; return 0;
}
int zizhan_cs35l41_identify(unsigned enabled, unsigned verified, GeniI2c *bus,
                          const ZizhanAmpClock *clock, ZizhanAmpIdentity *out)
{ return identify(enabled, verified, bus, clock, out, 0); }
int zizhan_cs35l41_boot_identify(unsigned enabled, unsigned verified, GeniI2c *bus,
                               const ZizhanAmpClock *clock, ZizhanAmpIdentity *out)
{ return identify(enabled, verified, bus, clock, out, 1); }

/* Actual-phone module reads OTP_ID0x10 and32 BE32 words from0x400 without
 * a key write; its separate volatile-trim phase unlocks afterward. Fixed
 * Linux v6.6 register definitions: GLOBAL_EN=bit0,PWR2 AMP=bit0/BST=bits4:5,
 * HALO core enable=bit0. Never infer the last bit from reset defaults. */
static int snapshot(unsigned enabled, unsigned verified, GeniI2c *bus,
                    const ZizhanAmpClock *clock, ZizhanAmpSnapshot *out, int owned) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out); out->reason = ZizhanAmpOff;
    int reason = identify(enabled, verified, bus, clock, &out->identity, owned ? 2 : 1);
    if (out->identity.status != ZizhanAmpPass) {
        out->status = out->identity.status; out->reason = out->identity.reason; return reason;
    }
    const uint8_t original = bus->address;
    const uint32_t registers[] = {0x2014, 0x2018, 0x2bc1000};
    const uint64_t begin = clock->now_us(clock->context);
    for (unsigned chip = 0; chip < 2; ++chip) {
        bus->address = chip ? 0x42 : 0x40;
        if (out->identity.revid[chip] != 0xb2) { reason = ZizhanAmpWrongIdentity; goto fail; }
        for (unsigned phase = 0; phase < 2; ++phase) {
            uint32_t *state = phase ? out->state_after[chip] : out->state_before[chip];
            for (unsigned i = 0; i < 3; ++i) {
                if (clock->now_us(clock->context) - begin >= 2000000) { reason = ZizhanAmpSnapshotTimeout; goto fail; }
                reason = read_identity(bus, clock, registers[i], &state[i], &out->identity);
                if (reason) goto fail;
                if (phase) ++out->after_count[chip]; else ++out->before_count[chip];
            }
            if ((state[0] & 1) || (state[1] & 0x31)) { reason = ZizhanAmpOutputActive; goto fail; }
            if (phase) {
                if (memcmp(state, out->state_before[chip], 3 * sizeof(uint32_t))) {
                    reason = ZizhanAmpStateChanged; goto fail;
                }
                continue;
            }
            out->output_disabled[chip] = 1; out->boost_disabled[chip] = 1;
            out->dsp_stopped[chip] = !(state[2] & 1);
            reason = read_identity(bus, clock, 0x10, &out->otp_id[chip], &out->identity);
            if (reason) goto fail;
            const uint32_t id = out->otp_id[chip];
            if (id != 1 && id != 2 && id != 3 && id != 6 && id != 8) {
                reason = ZizhanAmpUnsupportedOtp; goto fail;
            }
            for (unsigned word = 0; word < 32; ++word) {
                if (clock->now_us(clock->context) - begin >= 2000000) { reason = ZizhanAmpSnapshotTimeout; goto fail; }
                reason = read_identity(bus, clock, 0x400 + word * 4, &out->words[chip][word], &out->identity);
                if (reason) goto fail;
                ++out->word_count[chip];
            }
        }
        ++out->completed;
    }
    bus->address = original; out->status = ZizhanAmpPass; out->reason = 0; return 0;
fail:
    if (!out->identity.controller_uncertain) bus->address = original;
    out->status = ZizhanAmpFail; out->reason = reason; return reason;
}

int zizhan_cs35l41_snapshot(unsigned enabled, unsigned verified, GeniI2c *bus,
                           const ZizhanAmpClock *clock, ZizhanAmpSnapshot *out)
{ return snapshot(enabled, verified, bus, clock, out, 0); }

/* Separate opt-in clear-only operation, not a codec initialization. The
 * caller retains the lease and finally asserts shared reset on every result.
 * Fixed known masks only; no route,key,OTP,trim or output-enabling write. */
int zizhan_cs35l41_safe_off(unsigned enabled, unsigned verified, GeniI2c *bus,
                          const ZizhanAmpClock *clock, ZizhanAmpSafeOff *out) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out); out->reason = ZizhanAmpOff;
    int reason = zizhan_cs35l41_boot_identify(enabled, verified, bus, clock, &out->identity);
    if (out->identity.status != ZizhanAmpPass) {
        out->status = out->identity.status; out->reason = out->identity.reason; return reason;
    }
    const uint8_t original = bus->address;
    const uint32_t registers[] = {0x2014, 0x2018, 0x2bc1000};
    const uint32_t masks[] = {0, 0x31, 1};
    // Inspect BOTH chips before any codec-value write. B2 only, and an
    // already enabled global output rejects the entire operation unchanged.
    for (unsigned chip = 0; chip < 2; ++chip) {
        bus->address = chip ? 0x42 : 0x40;
        if (out->identity.revid[chip] != 0xb2) { reason = ZizhanAmpWrongIdentity; goto fail; }
        for (unsigned i = 0; i < 3; ++i) {
            reason = read_identity(bus, clock, registers[i], &out->before[chip][i], &out->identity);
            if (reason) goto fail;
            ++out->before_count[chip];
        }
        if (out->before[chip][0] & 1) { reason = ZizhanAmpOutputActive; goto fail; }
    }
    for (unsigned chip = 0; chip < 2; ++chip) {
        bus->address = chip ? 0x42 : 0x40;
        for (unsigned i = 1; i < 3; ++i) {
            uint32_t global = 0, current = 0, observed = 0;
            reason = read_identity(bus, clock, 0x2014, &global, &out->identity);
            if (reason) goto fail;
            if (global & 1) { reason = ZizhanAmpOutputActive; goto fail; }
            reason = read_identity(bus, clock, registers[i], &current, &out->identity);
            if (reason) goto fail;
            if (global != out->before[chip][0] || current != out->before[chip][i]) {
                reason = ZizhanAmpStateChanged; goto fail;
            }
            const uint32_t value = current & ~masks[i];
            if (value != current) {
                const uint32_t reg = registers[i];
                const uint8_t wire[8] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16),
                    (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(value >> 24),
                    (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
                // Exactly4 possible value writes across two chips, all BE32
                // and bit-clearing. Count attempts before hardware dispatch.
                ++out->attempted_writes;
                if (geni_i2c_start_write_bytes(bus, wire, sizeof wire)) { reason = ZizhanAmpBusy; goto fail; }
                reason = finish_transfer(bus, clock, &out->identity);
                if (reason) goto fail;
                ++out->acknowledged_writes;
            }
            reason = read_identity(bus, clock, registers[i], &observed, &out->identity);
            if (reason) goto fail;
            if (observed != value) { reason = ZizhanAmpReadback; goto fail; }
        }
        for (unsigned i = 0; i < 3; ++i) {
            reason = read_identity(bus, clock, registers[i], &out->after[chip][i], &out->identity);
            if (reason) goto fail;
            ++out->after_count[chip];
            if (out->after[chip][i] != (out->before[chip][i] & ~masks[i])) {
                reason = ZizhanAmpReadback; goto fail;
            }
        }
        ++out->completed;
    }
    bus->address = original; out->status = ZizhanAmpPass; out->reason = 0; return 0;
fail:
    if (!out->identity.controller_uncertain) bus->address = original;
    out->status = ZizhanAmpFail; out->reason = reason; return reason;
}

/* Live mode5 witness: after the last successfully drained read, only master
 * RX_IRQ(bit7)+RXwatermark(bit26) remained while phase/SE/RXcount were idle.
 * Fixed vendor msm-geni-se.h defines these bits and W1C0x618. This narrow
 * handoff never substitutes for draining data/finishing a command. */
int zizhan_cs35l41_handoff(unsigned enabled, unsigned verified, GeniI2c *bus,
                         const ZizhanAmpClock *clock, const ZizhanAmpSafeOff *off,
                         ZizhanAmpHandoff *out) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out); out->reason = ZizhanAmpOff;
    if (enabled != 1) return 0;
    const unsigned flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
                           ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
    if (verified != flags || !off || off->status != ZizhanAmpPass || off->completed != 2 ||
        off->identity.controller_uncertain || off->attempted_writes != off->acknowledged_writes ||
        off->attempted_writes > 4) { out->reason = ZizhanAmpPrerequisite; return 0; }
    for (unsigned chip = 0; chip < 2; ++chip) {
        if (off->after_count[chip] != 3 || (off->after[chip][0] & 1) ||
            (off->after[chip][1] & 0x31) || (off->after[chip][2] & 1)) {
            out->reason = ZizhanAmpPrerequisite; return 0;
        }
    }
    if (!bus || !bus->io.read || !bus->io.write || !clock || !clock->now_us || !clock->pause_us) {
        out->status = ZizhanAmpFail; out->reason = ZizhanAmpArgument; return out->reason;
    }
    out->phase = bus->phase;
    if (geni_i2c_busy(bus)) { out->reason = ZizhanAmpBusy; return 0; }
    const uint32_t allowed = 0x04000080u;
    const uint64_t start = clock->now_us(clock->context);
    unsigned zero_samples = 0;
    for (unsigned i = 0; i < 26; ++i) {
        out->phase = bus->phase;
        out->after_se = bus->io.read(bus->io.ctx, 0x40);
        out->after_irq = bus->io.read(bus->io.ctx, GENI_M_IRQ_STATUS);
        out->after_rx = bus->io.read(bus->io.ctx, GENI_RX_FIFO_STATUS);
        ++out->samples;
        if (!i) {
            out->before_se = out->after_se; out->before_irq = out->after_irq; out->before_rx = out->after_rx;
            // Read-only evidence if a level reasserts. Never guess or change
            // the inherited threshold in this bounded clear-only candidate.
            out->rx_watermark = bus->io.read(bus->io.ctx, 0x810);
            out->rx_rfr_watermark = bus->io.read(bus->io.ctx, 0x814);
            out->slave_irq = bus->io.read(bus->io.ctx, 0x640);
            out->dma_rx_irq = bus->io.read(bus->io.ctx, 0xd40);
        }
        if (out->phase != GeniIdle || (out->after_se & 0x1001) ||
            (out->after_rx & 0x1ffffff) || (out->after_irq & ~allowed)) {
            out->status = ZizhanAmpFail; out->reason = ZizhanAmpStateChanged; return out->reason;
        }
        if (!i && out->after_irq) {
            out->acknowledged_mask = out->after_irq;
            ++out->clear_writes;
            bus->io.write(bus->io.ctx, GENI_M_IRQ_CLEAR, out->acknowledged_mask);
        }
        if (!out->after_irq) {
            if (++zero_samples == 2) { out->status = ZizhanAmpPass; out->reason = 0; return 0; }
        } else if (zero_samples) {
            // Reappearance after a zero sample is not a successful handoff.
            out->status = ZizhanAmpFail; out->reason = ZizhanAmpStateChanged; return out->reason;
        }
        if (clock->now_us(clock->context) - start >= 500 || i == 25) break;
        clock->pause_us(clock->context, 20);
    }
    out->status = ZizhanAmpFail; out->reason = ZizhanAmpTimeout; return out->reason;
}

/* Actual mode7 witness: slave RX-LAST08000000 remains behind master RXIRQ80.
 * Acknowledge that known source first; do not blanket-clear the slave. */
int zizhan_cs35l41_source_handoff(unsigned enabled, unsigned verified, GeniI2c *bus,
                         const ZizhanAmpClock *clock, const ZizhanAmpSafeOff *off,
                         ZizhanAmpHandoff *out) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out); out->reason = ZizhanAmpOff;
    if (enabled != 1) return 0;
    const unsigned flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
                           ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
    if (verified != flags || !off || off->status != ZizhanAmpPass || off->completed != 2 ||
        off->identity.controller_uncertain || off->attempted_writes != off->acknowledged_writes ||
        off->attempted_writes > 4) { out->reason = ZizhanAmpPrerequisite; return 0; }
    for (unsigned chip = 0; chip < 2; ++chip) {
        if (off->after_count[chip] != 3 || (off->after[chip][0] & 1) ||
            (off->after[chip][1] & 0x31) || (off->after[chip][2] & 1)) {
            out->reason = ZizhanAmpPrerequisite; return 0;
        }
    }
    if (!bus || !bus->io.read || !bus->io.write || !clock || !clock->now_us || !clock->pause_us) {
        out->status = ZizhanAmpFail; out->reason = ZizhanAmpArgument; return out->reason;
    }
    out->phase = bus->phase;
    if (geni_i2c_busy(bus)) { out->reason = ZizhanAmpBusy; return 0; }
    const uint32_t allowed = 0x04000080u, slave_allowed = 0x08000000u;
    const uint64_t start = clock->now_us(clock->context);
    unsigned zero_samples = 0;
    for (unsigned i = 0; i < 26; ++i) {
        out->phase = bus->phase;
        out->after_se = bus->io.read(bus->io.ctx, 0x40);
        out->after_rx = bus->io.read(bus->io.ctx, GENI_RX_FIFO_STATUS);
        out->slave_after = bus->io.read(bus->io.ctx, 0x640);
        out->dma_rx_after = bus->io.read(bus->io.ctx, 0xd40);
        out->after_irq = bus->io.read(bus->io.ctx, GENI_M_IRQ_STATUS);
        ++out->samples;
        if (!i) {
            out->before_se = out->after_se; out->before_irq = out->after_irq; out->before_rx = out->after_rx;
            out->slave_irq = out->slave_after; out->dma_rx_irq = out->dma_rx_after;
            out->rx_watermark = bus->io.read(bus->io.ctx, 0x810);
            out->rx_rfr_watermark = bus->io.read(bus->io.ctx, 0x814);
        }
        if (out->phase != GeniIdle || (out->after_se & 0x1001) ||
            (out->after_rx & 0x1ffffff) || (out->after_irq & ~allowed) ||
            (out->slave_after & ~slave_allowed) || out->dma_rx_after) {
            out->status = ZizhanAmpFail; out->reason = ZizhanAmpStateChanged; return out->reason;
        }
        if (!i && out->slave_after) {
            ++out->slave_clear_writes;
            bus->io.write(bus->io.ctx, 0x648, out->slave_after);
        } else if (!out->slave_after && out->after_irq && !out->clear_writes && !zero_samples) {
            // Never clear the summary while its observed source is set.
            out->acknowledged_mask = out->after_irq; ++out->clear_writes;
            bus->io.write(bus->io.ctx, GENI_M_IRQ_CLEAR, out->acknowledged_mask);
        }
        if (!out->slave_after && !out->after_irq) {
            if (++zero_samples == 2) { out->status = ZizhanAmpPass; out->reason = 0; return 0; }
        } else if (zero_samples) {
            out->status = ZizhanAmpFail; out->reason = ZizhanAmpStateChanged; return out->reason;
        }
        if (clock->now_us(clock->context) - start >= 500 || i == 25) break;
        clock->pause_us(clock->context, 20);
    }
    out->status = ZizhanAmpFail; out->reason = ZizhanAmpTimeout; return out->reason;
}

int zizhan_cs35l41_bus_config(unsigned enabled, unsigned verified, const GeniI2c *bus,
                            ZizhanAmpBusConfig *out) {
    if (!out) return ZizhanAmpArgument;
    memset(out, 0, sizeof *out);
    if (enabled != 1 || verified != (ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
        ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology)) return 0;
    if (!bus || !bus->io.read) return ZizhanAmpArgument;
    static const uint32_t registers[ZizhanAmpConfigWords] = {
        0x40, 0x68, 0x6c, 0x610, 0x614, 0x640, 0x644, 0xe1c,
        0x258, 0x254, 0x284, 0x288, 0x800, 0x804, 0x810, 0x814,
        0xc40, 0xc48, 0xd40, 0xd48, 0xe18, 0xe30, 0x908, 0xe40
    };
    out->phase = bus->phase;
    for (unsigned i = 0; i < ZizhanAmpConfigWords; ++i) {
        out->values[i] = bus->io.read(bus->io.ctx, registers[i]);
        ++out->valid;
    }
    return 0;
}

int zizhan_cs35l41_owned_snapshot(unsigned enabled, unsigned verified, GeniI2c *bus,
    const ZizhanAmpClock *clock, const ZizhanAmpSafeOff *off, ZizhanAmpReadiness *r, ZizhanAmpSnapshot *out) {
    if (!r || !out) return ZizhanAmpArgument;
    memset(r, 0, sizeof *r); memset(out, 0, sizeof *out);
    r->reason = ZizhanAmpOff;
    if (enabled != 1) return 0;
    const unsigned flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
        ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
    if (verified != flags || !off || off->status != ZizhanAmpPass || off->completed != 2 ||
        off->identity.controller_uncertain || off->attempted_writes > 4 ||
        off->attempted_writes != off->acknowledged_writes) { r->reason = ZizhanAmpPrerequisite; return 0; }
    for (unsigned i = 0; i < 2; ++i)
        if (off->after_count[i] != 3 || (off->after[i][0] & 1) ||
            (off->after[i][1] & 0x31) || (off->after[i][2] & 1)) { r->reason = ZizhanAmpPrerequisite; return 0; }
    if (!bus || !bus->io.read || !bus->io.write || !clock || !clock->now_us || !clock->pause_us) {
        r->status = ZizhanAmpFail; r->reason = ZizhanAmpArgument; return r->reason;
    }
    const uint64_t begin = clock->now_us(clock->context);
    unsigned stable = 0;
    for (unsigned i = 0; i < 26; ++i) {
        ZizhanAmpBusConfig config;
        zizhan_cs35l41_bus_config(1, verified, bus, &config); ++r->samples;
        if (!i) r->before = config;
        if (!owned_config(&config, !stable)) { r->reason = ZizhanAmpStateChanged; goto fail; }
        if (config.values[5]) {
            if (!i) { bus->io.write(bus->io.ctx, 0x648, config.values[5]); ++r->slave_clear_writes; }
        } else {
            if (stable && memcmp(config.values, r->stable.values, sizeof config.values)) {
                r->reason = ZizhanAmpStateChanged; goto fail;
            }
            r->stable = config;
            if (++stable == 2) {
                r->masked_status = !!(config.values[3] & 0x80);
                r->status = ZizhanAmpPass; r->reason = 0;
                // Fresh exact config classification at the real identity
                // entry too; raw entry_irq/busy_sources remain observable.
                return snapshot(1, verified, bus, clock, out, 1);
            }
        }
        if (clock->now_us(clock->context) - begin >= 500 || i == 25) break;
        clock->pause_us(clock->context, 20);
    }
    r->reason = ZizhanAmpTimeout;
fail:
    r->status = ZizhanAmpFail; return r->reason;
}

/* Exact immutable triples extracted from actual phone B2 module, compared
 * by offline init-plan checker. Generic Linux v6.6 differs; do not import
 * its power/ASP writes. These key values are NOT OTP byte-key55/aa/cc/33. */
static const uint32_t b2_patch[10][2] = {
    {0x40,0x5555},{0x40,0xaaaa},{0x4100,0},{0x4310,0},{0x4400,0},
    {0x381c,0x51},{0x2bc20e0,0},{0x2bc2020,0},{0x40,0xcccc},{0x40,0x3333}
};
static int b2_write(unsigned step, GeniI2c *bus, const ZizhanAmpClock *clock, ZizhanAmpB2Patch *out) {
    if (step >= 10) return ZizhanAmpArgument;
    const uint32_t reg = b2_patch[step][0], value = b2_patch[step][1];
    const uint8_t wire[] = {(uint8_t)(reg >> 24),(uint8_t)(reg >> 16),(uint8_t)(reg >> 8),(uint8_t)reg,
        (uint8_t)(value >> 24),(uint8_t)(value >> 16),(uint8_t)(value >> 8),(uint8_t)value};
    ++out->attempted_writes;
    if (geni_i2c_start_write_bytes(bus, wire, sizeof wire)) return ZizhanAmpBusy;
    const int reason = finish_transfer(bus, clock, &out->transport);
    if (!reason) ++out->acknowledged_writes;
    return reason;
}
int zizhan_cs35l41_b2_patch(unsigned enabled, unsigned verified, GeniI2c *bus,
                          const ZizhanAmpClock *clock, const ZizhanAmpSnapshot *snapshot,
                          ZizhanAmpB2Patch *out) {
    if (!out) return ZizhanAmpArgument;
    memset(out,0,sizeof *out); out->reason = ZizhanAmpOff;
    if (enabled != 1) return 0;
    const unsigned flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified |
        ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
    if (verified != flags || !snapshot || snapshot->status != ZizhanAmpPass || snapshot->completed != 2 ||
        snapshot->identity.controller_uncertain) { out->reason = ZizhanAmpPrerequisite; return 0; }
    for (unsigned chip=0;chip<2;++chip) {
        if (snapshot->identity.devid[chip] != 0x35a40 || snapshot->identity.revid[chip] != 0xb2 ||
            snapshot->after_count[chip] != 3 || snapshot->word_count[chip] != 32 ||
            (snapshot->state_after[chip][0] & 1) || (snapshot->state_after[chip][1] & 0x31) ||
            (snapshot->state_after[chip][2] & 1)) { out->reason = ZizhanAmpPrerequisite; return 0; }
    }
    if (!bus || !bus->io.read || !bus->io.write || !clock || !clock->now_us || !clock->pause_us) {
        out->status=ZizhanAmpFail;out->reason=ZizhanAmpArgument;return out->reason;
    }
    if (geni_i2c_busy(bus)) {out->reason=ZizhanAmpBusy;return 0;}
    const uint8_t original=bus->address;
    const uint32_t state_regs[]={0x2014,0x2018,0x2bc1000};
    const uint64_t begin=clock->now_us(clock->context);
    int reason=0;
    for(unsigned chip=0;chip<2;++chip) {
        unsigned opened=0;
        ZizhanAmpBusConfig config;
        zizhan_cs35l41_bus_config(1,verified,bus,&config);
        if (!owned_config(&config,1)) {reason=ZizhanAmpStateChanged;goto fail;}
        bus->address=chip?0x42:0x40;
        for(unsigned i=0;i<3;++i) {
            uint32_t value=0;reason=read_identity(bus,clock,state_regs[i],&value,&out->transport);
            if(reason)goto fail;
            if(value!=snapshot->state_after[chip][i]) {reason=ZizhanAmpStateChanged;goto fail;}
        }
        for(unsigned step=0;step<8;++step) {
            if(clock->now_us(clock->context)-begin>=2000000) {reason=ZizhanAmpTimeout;goto close_fail;}
            opened=1;reason=b2_write(step,bus,clock,out);if(reason)goto close_fail;
        }
        for(unsigned i=0;i<6;++i) {
            reason=read_identity(bus,clock,b2_patch[i+2][0],&out->readback[chip][i],&out->transport);
            if(reason)goto close_fail;
            ++out->patch_readbacks[chip];
            if(out->readback[chip][i]!=b2_patch[i+2][1]) {reason=ZizhanAmpReadback;goto close_fail;}
        }
        for(unsigned step=8;step<10;++step) {
            reason=b2_write(step,bus,clock,out);if(reason)goto fail;
            ++out->close_acknowledged[chip];
        }
        opened=0;
        for(unsigned i=0;i<3;++i) {
            reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);
            if(reason)goto fail;
            ++out->state_count[chip];
            if(out->after_state[chip][i]!=snapshot->state_after[chip][i]) {reason=ZizhanAmpStateChanged;goto fail;}
        }
        ++out->completed;continue;
close_fail:
        // Acknowledge close only if bus remains certain, never retry a value
        // write after ambiguous cancellation/error. Final shared reset is the
        // hardware containment when lock/transport cannot be proved.
        if(opened && !out->transport.controller_uncertain && !geni_i2c_busy(bus))
            for(unsigned step=8;step<10;++step) {
                const int cleanup=b2_write(step,bus,clock,out);
                if(cleanup)break;
                ++out->close_acknowledged[chip];
            }
        goto fail;
    }
    bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
fail:
    if(!out->transport.controller_uncertain)bus->address=original;
    out->status=ZizhanAmpFail;out->reason=reason;return reason;
}

/* Actual protected-phone input manifest: public hash and address/offset facts,
 * never firmware payload. Exact hash pins metadata/framing as well as RAM.
 * These spans independently match the host physical/alias-bound plan. */
static const uint8_t ram_sha256[32]={
 0xc1,0x1c,0xb3,0xe2,0x58,0x25,0xc6,0x73,0xa9,0x55,0xa3,0x51,0x93,0x70,0xad,0x77,
 0xfb,0xcd,0x8b,0xcf,0x68,0x80,0x8e,0x8e,0x34,0x14,0x0a,0x70,0xca,0xef,0xce,0xc4};
static const uint32_t ram_spans[6][3]={
 {104,72,0x02000000},{184,528,0x0200009c},{720,1596,0x020003cc},
 {2324,1260,0x02000a14},{3592,6000,0x02c00000},{9600,20420,0x03800000}};
static int ram_load(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpSnapshot *snapshot,const ZizhanAmpTrim *trim,const uint8_t *bytes,unsigned length,
 ZizhanAmpHash hash,ZizhanAmpRam *out,unsigned header_only,unsigned burst,unsigned native_init){
 if(!out)return ZizhanAmpArgument;
 memset(out,0,sizeof *out);out->reason=ZizhanAmpOff;out->header_only=header_only;out->native_core_init=native_init;if(enabled!=1)return 0;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 if(verified!=flags||!snapshot||snapshot->status!=ZizhanAmpPass||snapshot->completed!=2||snapshot->identity.controller_uncertain||
    !trim||trim->status!=ZizhanAmpPass||trim->completed!=2||trim->transport.controller_uncertain||
    trim->key_attempts!=8||trim->attempted_writes!=trim->acknowledged_writes){out->reason=ZizhanAmpPrerequisite;return 0;}
 for(unsigned chip=0;chip<2;++chip)
  if(snapshot->identity.devid[chip]!=0x35a40||snapshot->identity.revid[chip]!=0xb2||snapshot->otp_id[chip]!=8||snapshot->word_count[chip]!=32||
     trim->readbacks[chip]!=98||trim->close_acknowledged[chip]!=2||trim->state_count[chip]!=3||
     (trim->after_state[chip][0]&1)||(trim->after_state[chip][1]&0x31)||(trim->after_state[chip][2]&1)){
   out->reason=ZizhanAmpPrerequisite;return 0;}
 if(!bus||!bus->io.read||!bus->io.write||!clock||!clock->now_us||!clock->pause_us||!bytes||length!=ZizhanAmpRamFileBytes||!hash){
  out->status=ZizhanAmpFail;out->reason=ZizhanAmpArgument;return out->reason;}
 uint8_t digest[32];
 if(hash(bytes,length,digest)||memcmp(digest,ram_sha256,32)){
  out->status=ZizhanAmpFail;out->reason=ZizhanAmpReadback;return out->reason;}
 out->source_verified=1;
 if(geni_i2c_busy(bus)){out->reason=ZizhanAmpBusy;return 0;}
 const uint8_t original=bus->address;const uint32_t state_regs[]={0x2014,0x2018,0x2bc1000};
 const uint64_t begin=clock->now_us(clock->context);int reason=0;
 for(unsigned chip=0;chip<(header_only?1u:2u);++chip){
  out->failed_chip=chip;bus->address=chip?0x42:0x40;
  uint32_t expected_state[3];memcpy(expected_state,trim->after_state[chip],sizeof expected_state);
  if(native_init){
   // Actual native probe2138..2148:full CCM0,not just mask1.
   // Qualify separately;never rewrite the successful trim receipt.
   ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
   if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
   for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->core_init_before[chip][i],&out->transport);if(reason)goto fail;++out->core_init_reads[chip];
    if(out->core_init_before[chip][i]!=expected_state[i]){reason=ZizhanAmpStateChanged;goto fail;}}
   const uint8_t wire[]={2,0xbc,0x10,0,0,0,0,0};++out->core_init_attempted;
   if(geni_i2c_start_write_bytes(bus,wire,sizeof wire)){reason=ZizhanAmpBusy;goto fail;}
   reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;++out->core_init_acknowledged;
   expected_state[2]=0;
   for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->core_init_after[chip][i],&out->transport);if(reason)goto fail;++out->core_init_reads[chip];
    if(out->core_init_after[chip][i]!=expected_state[i]){reason=ZizhanAmpReadback;goto fail;}}
  }
  for(unsigned pass=0;pass<2;++pass)for(unsigned segment=0;segment<(header_only?1u:6u);++segment){
   out->failed_segment=segment;out->failed_offset=0;
   ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
   if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
   for(unsigned i=0;i<3;++i){
    reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;
    ++out->state_count[chip];if(out->after_state[chip][i]!=expected_state[i]){reason=ZizhanAmpStateChanged;goto fail;}
   }
   const unsigned chunk=burst?(segment==5?20u:12u):4u;
   for(unsigned at=0;at<ram_spans[segment][1];at+=pass?chunk:4u){
    out->failed_offset=at;
    if(clock->now_us(clock->context)-begin>=45000000){reason=ZizhanAmpTimeout;goto fail;}
    const uint32_t reg=ram_spans[segment][2]+at;
    const uint8_t *expected=bytes+ram_spans[segment][0]+at;
    uint8_t wire[8]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,0,0,0,0};
    if(!pass){
     memcpy(wire+4,expected,4);++out->attempted_writes;
     if(geni_i2c_start_write_bytes(bus,wire,8)){reason=ZizhanAmpBusy;goto fail;}
     reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;
     ++out->acknowledged_writes;
    }else{
     if(burst?geni_i2c_start_read_block(bus,wire,4,chunk):geni_i2c_start_read_bytes(bus,wire,4,4)){reason=ZizhanAmpBusy;goto fail;}
     reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;
     if(burst)++out->burst_reads;
     for(unsigned word=0;word<chunk/4;++word){
      ++out->readbacks[chip];const uint8_t *p=expected+word*4;
      const uint32_t value=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
      const uint32_t observed=wire_be32(burst?bus->rx_block[word]:bus->rx);
      if(header_only){out->header_expected[at/4]=value;out->header_observed[at/4]=observed;++out->packed_count;}
      if(observed!=value){++out->mismatches;if(!header_only){out->failed_offset=at+word*4;reason=ZizhanAmpReadback;goto fail;}}
     }
    }
   }
   if(pass)++out->segments_verified[chip];else ++out->segments_written[chip];
  }
  if(header_only)for(unsigned i=0;i<24;++i){
   const uint32_t reg=0x02800000+i*4;
   const uint8_t request[4]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg};
   if(clock->now_us(clock->context)-begin>=45000000){reason=ZizhanAmpTimeout;goto fail;}
   if(geni_i2c_start_read_bytes(bus,request,4,4)){reason=ZizhanAmpBusy;goto fail;}
   reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;
   out->header_unpacked[i]=wire_be32(bus->rx);++out->unpacked_count;
  }
  for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;
   ++out->state_count[chip];if(out->after_state[chip][i]!=expected_state[i]){reason=ZizhanAmpStateChanged;goto fail;}}
  ++out->completed;
 }
 if(clock->now_us(clock->context)-begin>=45000000){reason=ZizhanAmpTimeout;goto fail;}
 out->elapsed_us=clock->now_us(clock->context)-begin;bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
fail:
 out->elapsed_us=clock->now_us(clock->context)-begin;
 if(!out->transport.controller_uncertain)bus->address=original;
 out->status=ZizhanAmpFail;out->reason=reason;return reason;
}
int zizhan_cs35l41_ram_native_init(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpSnapshot *snapshot,const ZizhanAmpTrim *trim,const uint8_t *bytes,unsigned length,ZizhanAmpHash hash,ZizhanAmpRam *out){
 return ram_load(enabled,verified,bus,clock,snapshot,trim,bytes,length,hash,out,0,1,1);}
int zizhan_cs35l41_ram(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpSnapshot *snapshot,const ZizhanAmpTrim *trim,const uint8_t *bytes,unsigned length,ZizhanAmpHash hash,ZizhanAmpRam *out){
 return ram_load(enabled,verified,bus,clock,snapshot,trim,bytes,length,hash,out,0,0,0);
}
int zizhan_cs35l41_ram_header(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpSnapshot *snapshot,const ZizhanAmpTrim *trim,const uint8_t *bytes,unsigned length,ZizhanAmpHash hash,ZizhanAmpRam *out){
 return ram_load(enabled,verified,bus,clock,snapshot,trim,bytes,length,hash,out,1,0,0);
}
int zizhan_cs35l41_ram_burst(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpSnapshot *snapshot,const ZizhanAmpTrim *trim,const uint8_t *bytes,unsigned length,ZizhanAmpHash hash,ZizhanAmpRam *out){
 return ram_load(enabled,verified,bus,clock,snapshot,trim,bytes,length,hash,out,0,1,0);
}

static int trim_write(GeniI2c *bus,const ZizhanAmpClock *clock,uint32_t reg,uint32_t value,ZizhanAmpTrim *out,int key){
    if(key?(reg!=0x40||(value!=0x55&&value!=0xaa&&value!=0xcc&&value!=0x33)):!zizhan_amp_trim_register(reg))return ZizhanAmpArgument;
    const uint8_t wire[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,
        (uint8_t)(value>>24),(uint8_t)(value>>16),(uint8_t)(value>>8),(uint8_t)value};
    ++out->attempted_writes;if(key)++out->key_attempts;else ++out->trim_attempts;
    if(geni_i2c_start_write_bytes(bus,wire,sizeof wire))return ZizhanAmpBusy;
    int rc=finish_transfer(bus,clock,&out->transport);if(!rc)++out->acknowledged_writes;return rc;
}
int zizhan_cs35l41_trim(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
    const ZizhanAmpSnapshot *snapshot,const ZizhanAmpB2Patch *patch,ZizhanAmpTrim *out){
    if(!out)return ZizhanAmpArgument;
    memset(out,0,sizeof *out);out->reason=ZizhanAmpOff;if(enabled!=1)return 0;
    const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
    if(verified!=flags||!snapshot||snapshot->status!=ZizhanAmpPass||snapshot->completed!=2||snapshot->identity.controller_uncertain||
       !patch||patch->status!=ZizhanAmpPass||patch->completed!=2||patch->transport.controller_uncertain||
       patch->attempted_writes!=20||patch->acknowledged_writes!=20){out->reason=ZizhanAmpPrerequisite;return 0;}
    /* Static and cleared per call: trim runs only inside the boot-exclusive prepare (11-18 stack audit). */
    static ZizhanAmpTrimUpdate updates[2][98];memset(updates,0,sizeof updates);
    for(unsigned chip=0;chip<2;++chip){
        if(snapshot->identity.devid[chip]!=0x35a40||snapshot->identity.revid[chip]!=0xb2||snapshot->word_count[chip]!=32||
           patch->close_acknowledged[chip]!=2||patch->state_count[chip]!=3||
           (patch->after_state[chip][0]&1)||(patch->after_state[chip][1]&0x31)||(patch->after_state[chip][2]&1)||
           !zizhan_amp_decode_trim8(snapshot->otp_id[chip],snapshot->words[chip],updates[chip])){out->reason=ZizhanAmpPrerequisite;return 0;}
    }
    if(!bus||!bus->io.read||!bus->io.write||!clock||!clock->now_us||!clock->pause_us){out->status=ZizhanAmpFail;out->reason=ZizhanAmpArgument;return out->reason;}
    if(geni_i2c_busy(bus)){out->reason=ZizhanAmpBusy;return 0;}
    const uint8_t original=bus->address;const uint32_t state_regs[]={0x2014,0x2018,0x2bc1000};
    const uint64_t begin=clock->now_us(clock->context);int reason=0;
    for(unsigned chip=0;chip<2;++chip){
        unsigned opened=0;ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
        if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
        bus->address=chip?0x42:0x40;
        for(unsigned i=0;i<3;++i){uint32_t v=0;reason=read_identity(bus,clock,state_regs[i],&v,&out->transport);if(reason)goto fail;if(v!=patch->after_state[chip][i]){reason=ZizhanAmpStateChanged;goto fail;}}
        opened=1;reason=trim_write(bus,clock,0x40,0x55,out,1);if(reason)goto close_fail;
        reason=trim_write(bus,clock,0x40,0xaa,out,1);if(reason)goto close_fail;
        for(unsigned i=0;i<98;++i){
            const ZizhanAmpTrimUpdate u=updates[chip][i];uint32_t before=0,observed=0;
            if(clock->now_us(clock->context)-begin>=5000000){reason=ZizhanAmpTimeout;goto close_fail;}
            reason=read_identity(bus,clock,u.reg,&before,&out->transport);if(reason)goto close_fail;
            const uint32_t value=(before&~u.mask)|(u.value&u.mask);
            if(value!=before){reason=trim_write(bus,clock,u.reg,value,out,0);if(reason)goto close_fail;}
            reason=read_identity(bus,clock,u.reg,&observed,&out->transport);if(reason)goto close_fail;
            ++out->readbacks[chip];if(observed!=value){reason=ZizhanAmpReadback;goto close_fail;}
        }
        reason=trim_write(bus,clock,0x40,0xcc,out,1);if(reason)goto fail;++out->close_acknowledged[chip];
        reason=trim_write(bus,clock,0x40,0x33,out,1);if(reason)goto fail;++out->close_acknowledged[chip];opened=0;
        for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;++out->state_count[chip];if(out->after_state[chip][i]!=patch->after_state[chip][i]){reason=ZizhanAmpStateChanged;goto fail;}}
        ++out->completed;continue;
close_fail:
        if(opened&&!out->transport.controller_uncertain&&!geni_i2c_busy(bus)){
            if(!trim_write(bus,clock,0x40,0xcc,out,1)){++out->close_acknowledged[chip];if(!trim_write(bus,clock,0x40,0x33,out,1))++out->close_acknowledged[chip];}
        }
        goto fail;
    }
    bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
fail:
    if(!out->transport.controller_uncertain)bus->address=original;
    out->status=ZizhanAmpFail;out->reason=reason;return reason;
}

/* Retained original protection parameter files only; no scene/control writes. */
static const uint8_t coeff_sha[2][32]={
 {0xd6,0xda,0x7b,0x1d,0x13,0xf9,0x86,0x17,0x0c,0x42,0x58,0xac,0x31,0xf7,0x3b,0x38,0x14,0x73,0xe3,0x54,0x31,0x51,0x82,0xfa,0xe0,0xa6,0xbf,0xdd,0x29,0x7e,0x98,0x35},
 {0xcd,0x4a,0x09,0xde,0x82,0xc2,0xd0,0x87,0x8e,0x99,0xde,0x7e,0x28,0xaa,0x1a,0x11,0x16,0x68,0xd7,0xa1,0x2b,0xff,0xef,0x67,0x79,0xc6,0x4b,0x9f,0x29,0x37,0x91,0x1b},
};
static const uint32_t coeff_identity[22]={0x40000,0x30000,2,0x400a4,0x2b01,0x1f4,0xe,0,0,2,
 0xcd,0x1d3f01,0x94,0x50,0xe,0x7bc,0xf20b,0x100,0x176,0x4c,0,0};
int zizhan_cs35l41_coefficients(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpRam *ram,const uint8_t *top,unsigned top_size,const uint8_t *bottom,unsigned bottom_size,
 ZizhanAmpHash hash,ZizhanAmpCoefficients *out){
 if(!out){return ZizhanAmpArgument;}memset(out,0,sizeof *out);out->reason=ZizhanAmpOff;if(enabled!=1)return 0;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 if(verified!=flags||!ram||ram->status!=ZizhanAmpPass||ram->completed!=2||!ram->source_verified||ram->transport.controller_uncertain||
    ram->attempted_writes!=14938||ram->acknowledged_writes!=14938||ram->burst_reads!=3618||ram->mismatches){out->reason=ZizhanAmpPrerequisite;return 0;}
 for(unsigned c=0;c<2;++c)if(ram->readbacks[c]!=7469||ram->segments_verified[c]!=6||ram->state_count[c]!=39||
    (ram->after_state[c][0]&1)||(ram->after_state[c][1]&0x31)||(ram->after_state[c][2]&1)){out->reason=ZizhanAmpPrerequisite;return 0;}
 if(ram->native_core_init){
  if(ram->core_init_attempted!=2||ram->core_init_acknowledged!=2){out->reason=ZizhanAmpPrerequisite;return 0;}
  for(unsigned c=0;c<2;++c)if(ram->core_init_reads[c]!=6||ram->core_init_after[c][2]||ram->after_state[c][2]){out->reason=ZizhanAmpPrerequisite;return 0;}
 }
 if(!bus||!bus->io.read||!bus->io.write||!clock||!clock->now_us||!clock->pause_us||!top||top_size!=5060||!bottom||bottom_size!=5148||!hash){
  out->status=ZizhanAmpFail;out->reason=ZizhanAmpArgument;return out->reason;}
 const uint8_t *files[]={top,bottom};const unsigned sizes[]={top_size,bottom_size};
 for(unsigned c=0;c<2;++c){uint8_t digest[32];if(hash(files[c],sizes[c],digest)||memcmp(digest,coeff_sha[c],32)){
  out->status=ZizhanAmpFail;out->reason=ZizhanAmpReadback;return out->reason;}++out->source_verified;}
 if(geni_i2c_busy(bus)){out->reason=ZizhanAmpBusy;return 0;}
 const uint8_t original=bus->address;const uint64_t begin=clock->now_us(clock->context);int reason=0;
 const uint32_t state_regs[]={0x2014,0x2018,0x2bc1000};
 /* Both chip identities must be valid before either receives coefficients. */
 for(unsigned chip=0;chip<2;++chip){
  out->failed_chip=chip;bus->address=chip?0x42:0x40;
  ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
  if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
  for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;
   ++out->state_count[chip];if(out->after_state[chip][i]!=ram->after_state[chip][i]){reason=ZizhanAmpStateChanged;goto fail;}}
  for(unsigned i=0;i<22;++i){
   if(clock->now_us(clock->context)-begin>=10000000){reason=ZizhanAmpTimeout;goto fail;}
   const uint32_t reg=0x02800000+i*4;const uint8_t request[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg};
   if(geni_i2c_start_read_bytes(bus,request,4,4)){reason=ZizhanAmpBusy;goto fail;}
   reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;
   out->identity[chip][i]=wire_be32(bus->rx);++out->identity_words[chip];
   if(out->identity[chip][i]!=coeff_identity[i]){reason=ZizhanAmpWrongIdentity;goto fail;}
  }
 }
 for(unsigned chip=0;chip<2;++chip){
  out->failed_chip=chip;bus->address=chip?0x42:0x40;
  for(unsigned segment=0;segment<(chip?2u:1u);++segment){
   out->failed_segment=segment;out->failed_offset=0;
   const unsigned start=segment?3940:128,size=segment?8:(chip?3792:3732);
   const uint32_t base=segment?0x03400090:0x02c0015c;
   for(unsigned pass=0;pass<2;++pass){
    ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
    if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
    for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;
     ++out->state_count[chip];if(out->after_state[chip][i]!=ram->after_state[chip][i]){reason=ZizhanAmpStateChanged;goto fail;}}
    const unsigned step=pass&&!segment?12:4;
    for(unsigned at=0;at<size;at+=step){
     out->failed_offset=at;if(clock->now_us(clock->context)-begin>=10000000){reason=ZizhanAmpTimeout;goto fail;}
     const uint32_t reg=base+at;const uint8_t *expected=files[chip]+start+at;
     uint8_t wire[8]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,0,0,0,0};
     if(!pass){
      memcpy(wire+4,expected,4);++out->attempted_writes;
      if(geni_i2c_start_write_bytes(bus,wire,8)){reason=ZizhanAmpBusy;goto fail;}
      reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;++out->acknowledged_writes;
     }else{
      if(segment?geni_i2c_start_read_bytes(bus,wire,4,4):geni_i2c_start_read_block(bus,wire,4,12)){reason=ZizhanAmpBusy;goto fail;}
      reason=finish_transfer(bus,clock,&out->transport);if(reason)goto fail;
      for(unsigned word=0;word<step/4;++word){const uint8_t *p=expected+word*4;
       const uint32_t wanted=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
       ++out->readbacks[chip];if(wire_be32(segment?bus->rx:bus->rx_block[word])!=wanted){out->failed_offset=at+word*4;reason=ZizhanAmpReadback;goto fail;}}
     }
    }
   }
   ++out->segments_verified[chip];
  }
  for(unsigned i=0;i<3;++i){reason=read_identity(bus,clock,state_regs[i],&out->after_state[chip][i],&out->transport);if(reason)goto fail;
   ++out->state_count[chip];if(out->after_state[chip][i]!=ram->after_state[chip][i]){reason=ZizhanAmpStateChanged;goto fail;}}
  ++out->completed;
 }
 if(clock->now_us(clock->context)-begin>=10000000){reason=ZizhanAmpTimeout;goto fail;}
 out->elapsed_us=clock->now_us(clock->context)-begin;bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
fail:
 out->elapsed_us=clock->now_us(clock->context)-begin;if(!out->transport.controller_uncertain)bus->address=original;
 out->status=ZizhanAmpFail;out->reason=reason;return reason;
}

/* Actual module probe16 rates and wm_halo_configure_mpu23;fixedregisterfacts.
 * Core start in this phone's module sets bit0 only (unlike generic reset201).
 * HALO state/heartbeat offsets0/1 are actual WMFW metadata,globalXMbase1f4;
 * CSPL state offset2,algorithmcd XMbase94. No mailbox/control writes. */
static const uint32_t run_mpu[23][2]={
 {0x02bc3140,0x5555},{0x02bc3140,0xaaaa},
 {0x02bc3000,0xffffffff},{0x02bc3004,0xffffffff},{0x02bc3008,0xffffffff},{0x02bc300c,0xffffffff},{0x02bc3014,0xffffffff},
 {0x02bc3018,0xffffffff},{0x02bc301c,0xffffffff},{0x02bc3020,0xffffffff},{0x02bc3024,0xffffffff},{0x02bc302c,0xffffffff},
 {0x02bc3030,0xffffffff},{0x02bc3034,0xffffffff},{0x02bc3038,0xffffffff},{0x02bc303c,0xffffffff},{0x02bc3044,0xffffffff},
 {0x02bc3048,0xffffffff},{0x02bc304c,0xffffffff},{0x02bc3050,0xffffffff},{0x02bc3054,0xffffffff},{0x02bc305c,0xffffffff},{0x02bc3140,0}};
/* Exact original module frequency table12.288M->33 and set_sysclk order.
 * Do not choose a guessed SELF clock or enable analogue/ASP outputs. */
static const uint32_t run_pll[6][2]={{0x800,0x800},{0x7e0,0x660},{0x10,0},{7,0},{0x800,0},{0x10,0x10}};
/* Exact native/public readable registers. No DEBUG selector writes and no
 * guessed PC interpretation:unknown fields stay raw in the receipt. */
static const uint32_t run_debug_regs[15]={0x2b80010,0x2b80040,0x2b80048,0x2b80050,0x2b80500,
 0x2b805c0,0x2b805c8,0x2b805d0,0x2b805d8,0x2bc1008,0x2bc2000,0x2bc20c0,0x2bc2180,0x25e2040,0x25e2044};
uint32_t zizhan_cs35l41_debug_register(unsigned index){return index<15?run_debug_regs[index]:0;}
#include "ZizhanAmpDumpRegs.h"
static unsigned zizhan_amp_dump_active;
static int run_read(GeniI2c *bus,const ZizhanAmpClock *clock,uint32_t reg,uint32_t *value,ZizhanAmpRun *out){
 int allowed=reg==0x2014||reg==0x2018||reg==0x2bc1000||reg==0x2c04||reg==0x2c08||reg==0x13004||
  reg==0x28007d0||reg==0x28007d4||reg==0x2800258||reg==0x2bc3104||reg==0x2bc310c||reg==0x2bc3114||reg==0x2bc7008;
 for(unsigned i=0;i<16;++i)if(reg==0x2b80080+(i%8)*8+(i>=8?0x200:0))allowed=1;
 for(unsigned i=2;i<22;++i)if(reg==run_mpu[i][0])allowed=1;
 if(out->clocked_profile>=2&&((reg>=0x10010&&reg<=0x1001c)||(reg>=0x10090&&reg<=0x1009c))&&!(reg&3))allowed=1;
 if(out->frequency_monitor&&(reg==0x2d10||reg==0x14000))allowed=1;
 if(out->global_profile&&reg==0x4800)allowed=1;
 if(out->native_frame&&(reg==0x2c0c||reg==0x2800804))allowed=1;
 if(out->asp_format&&reg==0x4808)allowed=1;
 if(out->core_debug)for(unsigned i=0;i<15;++i)if(reg==run_debug_regs[i])allowed=1;
 if(out->native_calibration&&(reg==0x2800800||reg==0x2800268||reg==0x2800274||reg==0x2800270))allowed=1;
 if(out->mailbox_profile&&(reg==0x10914))allowed=1;
 if(zizhan_amp_dump_active&&reg<0x20000u)allowed=1;
 if(out->play_profile&&(reg==0x4c00||reg==0x4820||reg==0x6c04||reg==0x2c0c||reg==0x4800||reg==0x4808||reg==0x201c||reg==0x2020||reg==0x2024||reg==0x4804||reg==0x4c20||reg==0x4810))allowed=1;
 if(out->pulse_profile&&(reg==0x6000||reg==0x2084))allowed=1;
 if(out->iv_profile&&(reg==0x4c48||reg==0x4c4c))allowed=1;
 if(!allowed)return ZizhanAmpArgument;
 const uint8_t request[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg};
 if(geni_i2c_start_read_bytes(bus,request,4,4))return ZizhanAmpBusy;
 const int reason=finish_transfer(bus,clock,&out->transport);if(!reason)*value=wire_be32(bus->rx);return reason;
}
static int run_write(GeniI2c *bus,const ZizhanAmpClock *clock,uint32_t reg,uint32_t value,ZizhanAmpRun *out){
 int allowed=0;const unsigned chip=out->failed_chip;
 for(unsigned i=0;i<16;++i)if(reg==0x2b80080+(i%8)*8+(i>=8?0x200:0)&&value==1)allowed=1;
 for(unsigned i=0;i<23;++i)if(reg==run_mpu[i][0]&&value==run_mpu[i][1])allowed=1;
 if(reg==0x2bc1000&&(value==out->core_before[chip]||value==(out->core_before[chip]|1u)))allowed=1;
 if(out->clocked_profile&&reg==0x2c04&&out->pll_steps[chip]<6){const unsigned i=out->pll_steps[chip];
  if(value==((out->pll_current[chip]&~run_pll[i][0])|run_pll[i][1]))allowed=1;}
 if(out->frequency_monitor&&reg==0x2d10&&value==0x24010&&!out->monitor_writes[chip])allowed=1;
 if(out->global_profile&&reg==0x2014){
  if(!out->global_on[chip]&&!out->global_active[chip]&&value==(out->after_state[chip][0]|1u)&&!out->asp_enables[chip]&&out->asp_checks[chip])allowed=1;
  if(out->global_active[chip]&&value==(out->after_state[chip][0]&~1u))allowed=1;
 }
 if(out->native_frame&&reg==0x2c0c&&value==((out->global_fs_before[chip]&~0x1fu)|4u)&&out->frame_writes[chip]==0)allowed=1;
 if(out->native_frame&&reg==0x2800804&&value==32&&out->frame_writes[chip]==1)allowed=1;
 if(out->native_calibration){const unsigned n=out->prerun_writes[chip];
  if(!chip&&n==0&&reg==0x2800800&&value==1)allowed=1;
  if(n==(chip?0u:1u)&&reg==0x2800268&&value==out->calibration_r[chip])allowed=1;
  if(n==(chip?1u:2u)&&reg==0x2800274&&value==out->calibration_r[chip]+1u)allowed=1;
  if(n==(chip?2u:3u)&&reg==0x2800270&&value==1)allowed=1;
 }
 if(out->native_rates&&out->native_rate_writes[chip]<16){const unsigned i=out->native_rate_writes[chip];
  if(reg==0x2b80080+(i%8)*8+(i>=8?0x200:0)&&value==(out->native_rate_before[chip][i]&~0x1fu))allowed=1;}
 if(out->asp_rx_profile&&reg==0x4800){
  if(!out->asp_rx_active[chip]&&!out->asp_rx_on[chip]&&value==0x10000u&&out->asp_checks[chip]&&!out->asp_enables[chip])allowed=1;
  if(out->asp_rx_active[chip]&&value==0&&(out->stopped[chip]||(out->mpu_capture_only&&!out->started[chip])))allowed=1;
 }
 if(out->asp_format&&reg==0x4808&&value==((out->asp_format_before[chip]&~0xff000755u)|0x20000000u)&&out->asp_format_reads[chip]==1)allowed=1;
 if(!allowed||out->attempted_writes>=(out->clocked_profile==14?145u:out->asp_format?149u:out->asp_rx_profile?147u:out->native_rates?143u:out->native_calibration?111u:out->native_frame?104u:out->clocked_profile==6?96u:out->clocked_profile>=5?100u:out->global_profile?18u:out->frequency_monitor?14u:out->pll_capture?12u:out->clocked_profile?94u:82u))return ZizhanAmpArgument;
 const uint8_t wire[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,
  (uint8_t)(value>>24),(uint8_t)(value>>16),(uint8_t)(value>>8),(uint8_t)value};
 ++out->attempted_writes;if(geni_i2c_start_write_bytes(bus,wire,8))return ZizhanAmpBusy;
 const int reason=finish_transfer(bus,clock,&out->transport);if(!reason)++out->acknowledged_writes;return reason;
}
static int run_state(unsigned chip,GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out,int running){
 const uint32_t regs[]={0x2014,0x2018,0x2bc1000};
 for(unsigned i=0;i<3;++i){
  int rc=run_read(bus,clock,regs[i],&out->after_state[chip][i],out);if(rc)return rc;++out->state_count[chip];
  if(out->after_state[chip][i]!=(coeff->after_state[chip][i]|(i==2&&running?1u:0u)|(i==0&&out->global_active[chip]?1u:0u)))return ZizhanAmpStateChanged;
 }
 if(out->global_profile){int rc=run_read(bus,clock,0x4800,&out->asp_enables[chip],out);if(rc)return rc;++out->asp_checks[chip];if(out->asp_enables[chip]!=(out->asp_rx_active[chip]?0x10000u:0u))return ZizhanAmpStateChanged;}
 return 0;
}
/* Native set_csplmboxcmd sequence from the zizhan vendor driver. This
 * scoped writer cannot write power/boost/data/OTP registers. Keep its budget
 * separate from the existing149 core-configuration writes. */
static int mailbox_write(GeniI2c *bus,const ZizhanAmpClock *clock,ZizhanAmpRun *out,
 unsigned chip,uint32_t reg,uint32_t value,unsigned step,uint32_t mask,unsigned command){
 const uint32_t regs[6]={0x10814,0x10014,0x10914,0x13020,0x10014,0x10914};
 const uint32_t values[6]={0x100000,0x200000,mask&~0x100000u,command,0x200000,mask};
 if(!out->mailbox_profile||chip>1||step>=6||reg!=regs[step]||value!=values[step]||
    (command!=1&&command!=2)||out->mailbox_attempted[chip]>=12)return ZizhanAmpArgument;
 const uint8_t wire[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,
  (uint8_t)(value>>24),(uint8_t)(value>>16),(uint8_t)(value>>8),(uint8_t)value};
 ++out->mailbox_attempted[chip];if(geni_i2c_start_write_bytes(bus,wire,8))return ZizhanAmpBusy;
 const int rc=finish_transfer(bus,clock,&out->transport);if(!rc)++out->mailbox_acknowledged[chip];return rc;
}
static int native_mailbox(unsigned chip,unsigned index,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){
 if(chip>1||index>1||out->mailbox_completed[chip]!=index)return ZizhanAmpArgument;
 int rc=run_state(chip,bus,clock,coeff,out,1);if(rc)return rc;
 const uint64_t begin=clock->now_us(clock->context);const unsigned command=index+1;
 uint32_t mask=0,value=0;unsigned mask_changed=0;
 rc=run_read(bus,clock,0x10914,&mask,out);if(rc)return rc;out->mailbox_mask_before[chip][index]=mask;
 for(unsigned step=0;step<4;++step){
  const uint32_t regs[]={0x10814,0x10014,0x10914,0x13020};
  const uint32_t values[]={0x100000,0x200000,mask&~0x100000u,command};
  rc=mailbox_write(bus,clock,out,chip,regs[step],values[step],step,mask,command);if(rc)goto restore;
  if(step==1){rc=run_read(bus,clock,0x10014,&value,out);if(rc)goto restore;
   if(value&0x200000u){rc=ZizhanAmpStateChanged;goto restore;}}
  if(step==2){mask_changed=1;rc=run_read(bus,clock,0x10914,&value,out);if(rc)goto restore;
   if(value!=(mask&~0x100000u)){rc=ZizhanAmpReadback;goto restore;}}
 }
 /* Match native five1ms polls; no synthetic completion from a write ACK. */
 for(unsigned n=0;n<5;++n){
  if(clock->now_us(clock->context)-begin>=100000u){rc=ZizhanAmpTimeout;goto restore;}
  clock->pause_us(clock->context,1000);rc=run_read(bus,clock,0x10014,&value,out);if(rc)goto restore;
  ++out->mailbox_polls[chip][index];out->mailbox_irq[chip][index]=value;
  if(value&0x200000u)break;
 }
 if(!(value&0x200000u)){rc=ZizhanAmpTimeout;goto restore;}
 rc=mailbox_write(bus,clock,out,chip,0x10014,0x200000,4,mask,command);if(rc)goto restore;
restore:
 /* An uncertain bus forbids further traffic. Shared reset contains it. */
 if(mask_changed&&!out->transport.controller_uncertain&&!geni_i2c_busy(bus)){
  int cleanup=mailbox_write(bus,clock,out,chip,0x10914,mask,5,mask,command);
  if(!cleanup)cleanup=run_read(bus,clock,0x10914,&out->mailbox_mask_after[chip][index],out);
  if(!cleanup&&out->mailbox_mask_after[chip][index]!=mask)cleanup=ZizhanAmpReadback;
  if(!cleanup)out->mailbox_mask_restored[chip][index]=1;
  if(!rc)rc=cleanup;
 }
 if(rc)return rc;
 rc=run_read(bus,clock,0x13004,&out->mailbox_status[chip][index],out);if(rc)return rc;
 if(out->mailbox_status[chip][index]!=(index?0u:1u))return ZizhanAmpProtocol;
 rc=run_state(chip,bus,clock,coeff,out,1);if(rc)return rc;
 if(clock->now_us(clock->context)-begin>=100000u)return ZizhanAmpTimeout;
 ++out->mailbox_completed[chip];return 0;
}
/* Native module DSPRX3/4 defaults and VMON/IMON ADC widget bits.
 * Read-only routing/enable prerequisite, not an analogue measurement or
 * proof of protection at load while the outputs are deliberately disabled. */
static int native_iv(unsigned chip,unsigned slot,GeniI2c *bus,const ZizhanAmpClock *clock,ZizhanAmpRun *out){
 if(!out->iv_profile||chip>1||slot>1||out->mailbox_completed[chip]!=2)return ZizhanAmpPrerequisite;
 const uint32_t regs[]={0x2018,0x4c48,0x4c4c};
 for(unsigned i=0;i<3;++i){int rc=run_read(bus,clock,regs[i],&out->iv_values[chip][slot][i],out);if(rc)return rc;++out->iv_reads[chip][slot];}
 if((out->iv_values[chip][slot][0]&0x3031u)!=0x3000u||out->iv_values[chip][slot][1]!=0x18||out->iv_values[chip][slot][2]!=0x19)return ZizhanAmpStateChanged;
 return 0;
}
typedef struct {GeniI2c *bus;const ZizhanAmpClock *clock;ZizhanAmpRun *out;const ZizhanAudioTransfer *transfer;unsigned chip;} NativePulse;
static int pulse_read(void *p,uint32_t reg,uint32_t *value){NativePulse *c=p;return run_read(c->bus,c->clock,reg,value,c->out)==0;}
static int pulse_write(void *p,uint32_t reg,uint32_t value){
 NativePulse *c=p;ZizhanAmpRun *out=c->out;const uint32_t original=out->pulse[c->chip].original_volume;
 int allowed=(reg==0x40&&(value==0x55||value==0xaa||value==0xcc||value==0x33))||
  (reg==0x2084&&(value==0x2f1aa0||value==0x2f1aa3))||
  (reg==0x2018&&(value==0x3300||value==0x3320||value==0x3321))||
  (reg==0x4c00&&(value==0||value==0x08))||
  (zizhan_amp_echo&&(zizhan_amp_pulse_flags&1u)&&c->chip==0&&((reg==0x4c20&&(value==0x08||value==out->echo_src[0]))||(reg==0x4800&&(value==0x10000||value==0x10001))))||
  (reg==0x6000&&(value==original||value==((original&~0x3ff8u)|0x3f40u)||((zizhan_amp_pulse_flags&4u)&&value==(original&~0x3ff8u))))||
  ((zizhan_amp_pulse_flags&4u)&&reg==0x6c04&&(value==0x253||value==out->pulse[c->chip].original_gain));
 if(!out->pulse_profile||!allowed||out->transport.controller_uncertain)return 0;
 const uint8_t wire[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg,
  (uint8_t)(value>>24),(uint8_t)(value>>16),(uint8_t)(value>>8),(uint8_t)value};
 if(geni_i2c_start_write_bytes(c->bus,wire,8))return 0;
 return finish_transfer(c->bus,c->clock,&out->transport)==0;
}
static uint64_t pulse_now(void *p){NativePulse *c=p;return c->clock->now_us(c->clock->context);}
static int pulse_uncertain(void *p){NativePulse *c=p;return c->out->transport.controller_uncertain||geni_i2c_busy(c->bus);}
static int pulse_data(void *p){NativePulse *c=p;return c->transfer->once(c->transfer->context,c->chip);}
/* Diagnostics only (11-17 amp-pulse cold-watchdog hang): the kernel installs this when
 * zizhan_audio_checkpoint=1 so the log is persisted before each pulse-profile stage.
 * NULL by default and in host tests; no effect on any other profile. */
void (*zizhan_amp_checkpoint)(unsigned chip,unsigned stage);
#define AMP_CHECKPOINT(chip,stage) do{if(out->pulse_profile&&zizhan_amp_checkpoint){zizhan_amp_checkpoint((chip),(stage));}}while(0)
unsigned zizhan_amp_pulse_hold_us; /* 0 unless the kernel sets zizhan_pulse_hold=<ms> (attended only). */
unsigned zizhan_amp_echo;
void (*zizhan_play_pin_sample)(void);
unsigned zizhan_amp_dump_on,zizhan_amp_dump_reads[2];
uint32_t zizhan_amp_dump_values[2][228];
unsigned zizhan_amp_pulse_flags; /* bit0 dry (no AMP enable/volume), bit1 skip the native pup/pdn patch. */
unsigned zizhan_amp_play_ms; /* profile 19 window length; 0 refuses the profile. */
static void pulse_pause(void *p,unsigned us){NativePulse *c=p;c->clock->pause_us(c->clock->context,us);}
static ZizhanPulseOps pulse_ops(NativePulse *p){ZizhanPulseOps o={p,pulse_read,pulse_write,pulse_now,pulse_uncertain,pulse_data,pulse_pause,zizhan_amp_pulse_hold_us,zizhan_amp_pulse_flags&1u,(zizhan_amp_pulse_flags>>2)&1u};return o;}
static int run_debug_snapshot(GeniI2c *bus,const ZizhanAmpClock *clock,ZizhanAmpRun *out,unsigned slot,uint64_t begin){
 if(!out->core_debug){return 0;}if(slot>=6)return ZizhanAmpArgument;const unsigned chip=out->failed_chip;
 for(unsigned i=0;i<15;++i){if(clock->now_us(clock->context)-begin>=8000000)return ZizhanAmpTimeout;
  int rc=run_read(bus,clock,run_debug_regs[i],&out->debug_values[chip][slot][i],out);if(rc)return rc;++out->debug_count[chip][slot];}
 return 0;
}
static const uint32_t observed_mpu[20]={3,1,0xff0000,0xfffff,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0};
/* Profile 19 (docs/plan/2026-11-18-m1-playback-amp.md): the per-chip tail of silent_run for a chip
 * left prepared, run once both amps are off again. pdn patch only after a complete window. */
static int play_teardown(unsigned chip,GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out,uint64_t begin){
 if(!out->play_ready[chip]||out->play_torn[chip]){return 0;}
 NativePulse pulse_context={bus,clock,out,0,chip};ZizhanPulseOps pulse_io=pulse_ops(&pulse_context);
 bus->address=chip?0x42:0x40;out->failed_chip=chip;out->failed_stage=6;out->play_torn[chip]=1;
 int reason=run_write(bus,clock,0x2bc1000,out->core_before[chip],out);if(reason){return reason;}
 reason=run_read(bus,clock,0x2bc1000,&out->core_after[chip],out);if(reason){return reason;}
 if(out->core_after[chip]!=out->core_before[chip]){return ZizhanAmpReadback;}++out->stopped[chip];
 AMP_CHECKPOINT(chip,13);
 reason=run_debug_snapshot(bus,clock,out,5,begin);if(reason){return reason;}
 reason=run_state(chip,bus,clock,coeff,out,0);if(reason){return reason;}
 if(out->global_profile){
  reason=run_write(bus,clock,0x2014,coeff->after_state[chip][0],out);if(reason){return reason;}
  out->global_active[chip]=0;reason=run_state(chip,bus,clock,coeff,out,0);if(reason){return reason;}++out->global_off[chip];
  if(zizhan_amp_pulse_flags&2u){out->pulse[chip].patch_off=1;}
  else if(out->pulse[chip].complete){if(zizhan_pulse_patch(&pulse_io,&out->pulse[chip],0)){return ZizhanAmpProtocol;}AMP_CHECKPOINT(chip,14);}
 }
 if(out->asp_rx_profile){
  reason=run_write(bus,clock,0x4800,0,out);if(reason){return reason;}out->asp_rx_active[chip]=0;
  reason=run_state(chip,bus,clock,coeff,out,0);if(reason){return reason;}++out->asp_rx_off[chip];
 }
 return 0;
}
/* After a failure: tear down every chip still prepared, unless the bus is uncertain (then only the
 * caller's shared reset may touch the amps). */
static void play_teardown_all(GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out,uint64_t begin){
 for(unsigned chip=2;chip-->0;){
  if(out->transport.controller_uncertain||geni_i2c_busy(bus)){return;}
  if(out->pulse[chip].armed&&!out->pulse[chip].disabled){NativePulse c={bus,clock,out,0,chip};ZizhanPulseOps io=pulse_ops(&c);
   bus->address=chip?0x42:0x40;(void)zizhan_pulse_disarm(&io,&out->pulse[chip],0);}
  (void)play_teardown(chip,bus,clock,coeff,out,begin);
 }
}
typedef struct {GeniI2c *bus;const ZizhanAmpClock *clock;ZizhanAmpRun *out;ZizhanPulseOps io[2];uint64_t begin,limit;int reason;} PlayPoll;
/* Between periods: both amps fault-free (sticky and raw), HALO still running, inside the run limit. */
const uint32_t zizhan_play_snap_regs[16]={0x2014,0x2018,0x2c0c,0x4800,0x4808,0x4820,0x4c00,0x6000,0x6c04,0x10018,0x10098,0x1009c,0x201c,0x2020,0x2024,0x4804};
static int play_poll(void *p){
 PlayPoll *pp=p;ZizhanAmpRun *out=pp->out;++out->play_polls;
 if(out->play_polls==5&&zizhan_play_pin_sample){zizhan_play_pin_sample();}
 if(out->play_polls==5){
  for(unsigned chip=0;chip<2;++chip){pp->bus->address=chip?0x42:0x40;
   for(unsigned i=0;i<16;++i){if(run_read(pp->bus,pp->clock,zizhan_play_snap_regs[i],&out->play_snap[chip][i],out)){break;}++out->play_snap_reads[chip];}}
 }
 if(pp->clock->now_us(pp->clock->context)-pp->begin>=pp->limit){pp->reason=ZizhanAmpTimeout;return 1;}
 for(unsigned chip=0;chip<2;++chip){uint32_t halo=0;pp->bus->address=chip?0x42:0x40;out->failed_chip=chip;
  if(zizhan_pulse_hold_check(&pp->io[chip],&out->pulse[chip])){pp->reason=ZizhanAmpProtocol;return 1;}
  if(run_read(pp->bus,pp->clock,0x28007d0,&halo,out)||halo!=2){pp->reason=ZizhanAmpStateChanged;return 1;}}
 return 0;
}
static int play_window(GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out,
                       const ZizhanAudioTransfer *transfer,uint64_t begin,uint64_t limit){
 if(!out->play_ready[0]||!out->play_ready[1]||!transfer||!transfer->stream||!out->play_ms){return ZizhanAmpPrerequisite;}
 static NativePulse context[2];static PlayPoll poll;
 for(unsigned chip=0;chip<2;++chip){context[chip].bus=bus;context[chip].clock=clock;context[chip].out=out;context[chip].transfer=transfer;context[chip].chip=chip;}
 poll.bus=bus;poll.clock=clock;poll.out=out;poll.io[0]=pulse_ops(&context[0]);poll.io[1]=pulse_ops(&context[1]);poll.begin=begin;poll.limit=limit;poll.reason=0;
 int reason=0;
 for(unsigned chip=0;chip<2&&!reason;++chip){bus->address=chip?0x42:0x40;out->failed_chip=chip;out->failed_stage=10;
  if(zizhan_pulse_arm(&poll.io[chip],&out->pulse[chip])){reason=ZizhanAmpProtocol;}}
 // Echo (dry only): the top chip sends what it receives on ASPRX1 back on ASPTX1 (SDOUT, GPIO122).
 if(!reason&&zizhan_amp_echo&&(zizhan_amp_pulse_flags&1u)){bus->address=0x40;out->failed_chip=0;out->failed_stage=13;
  if(run_read(bus,clock,0x4c20,&out->echo_src[0],out)||run_read(bus,clock,0x4800,&out->echo_en[0],out)||run_read(bus,clock,0x4810,&out->echo_slots,out)||out->echo_en[0]!=0x10000u){reason=ZizhanAmpProtocol;}
  else if(!pulse_write(&context[0],0x4c20,0x08)||!pulse_write(&context[0],0x4800,0x10001)||
          run_read(bus,clock,0x4c20,&out->echo_src[1],out)||run_read(bus,clock,0x4800,&out->echo_en[1],out)){reason=ZizhanAmpProtocol;}
  else{out->echo_on=out->echo_src[1]==0x08&&out->echo_en[1]==0x10001u?1u:0u;if(!out->echo_on){reason=ZizhanAmpReadback;}}}
 if(!reason){out->failed_stage=11;
  out->play_streamed=transfer->stream(transfer->context,out->play_ms,play_poll,&poll)==1?1u:0u;
  // A fault seen by the poll fails the window even if the stream reports success.
  if(!out->play_streamed||poll.reason){reason=poll.reason?poll.reason:ZizhanAmpProtocol;}}
 if(zizhan_amp_dump_on&&(zizhan_amp_pulse_flags&1u)&&!out->transport.controller_uncertain){
  zizhan_amp_dump_active=1;
  for(unsigned chip=0;chip<2;++chip){bus->address=chip?0x42:0x40;zizhan_amp_dump_reads[chip]=0;
   for(unsigned i=0;i<ZizhanAmpDumpCount;++i){if(run_read(bus,clock,zizhan_amp_dump_regs[i],&zizhan_amp_dump_values[chip][i],out)){break;}++zizhan_amp_dump_reads[chip];}}
  zizhan_amp_dump_active=0;}
 if(out->echo_on&&!out->transport.controller_uncertain){bus->address=0x40;
  if(pulse_write(&context[0],0x4800,0x10000)&&pulse_write(&context[0],0x4c20,out->echo_src[0])&&
     !run_read(bus,clock,0x4c20,&out->echo_src[2],out)&&!run_read(bus,clock,0x4800,&out->echo_en[2],out)){
   out->echo_restored=out->echo_src[2]==out->echo_src[0]&&out->echo_en[2]==0x10000u?1u:0u;}
  if(!out->echo_restored&&!reason){reason=ZizhanAmpReadback;}}
 // Both amps off (reverse order) whatever happened; then the chips' own teardown.
 for(unsigned chip=2;chip-->0;){if(!out->pulse[chip].armed){continue;}bus->address=chip?0x42:0x40;out->failed_chip=chip;out->failed_stage=12;
  if(zizhan_pulse_disarm(&poll.io[chip],&out->pulse[chip],!reason)&&!reason){reason=ZizhanAmpProtocol;}}
 if(reason){return reason;}
 for(unsigned chip=2;chip-->0;){reason=play_teardown(chip,bus,clock,coeff,out,begin);if(reason){return reason;}}
 return 0;
}
static int silent_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out,unsigned capture_only,unsigned observed_profile,unsigned clocked_profile,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer){
 if(!out){return ZizhanAmpArgument;}memset(out,0,sizeof *out);out->clocked_profile=clocked_profile;out->mailbox_profile=clocked_profile>=16;out->iv_profile=clocked_profile>=17;out->pulse_profile=clocked_profile==18||clocked_profile==19;out->play_profile=clocked_profile==19;out->play_ms=clocked_profile==19?zizhan_amp_play_ms:0u;out->asp_format=clocked_profile>=13;out->asp_rx_profile=clocked_profile>=12;out->native_rates=clocked_profile>=11;out->native_calibration=clocked_profile>=10;out->core_debug=clocked_profile==9;out->native_frame=clocked_profile>=8;out->pll_capture=clocked_profile>=2&&clocked_profile<=4;out->frequency_monitor=clocked_profile>=3;out->global_profile=clocked_profile>=4;out->mpu_capture_only=capture_only;out->observed_mpu_profile=observed_profile;out->reason=ZizhanAmpOff;if(enabled!=1)return 0;
 if(out->native_calibration){
  if(!cal||cal->version!=1||cal->bytes!=sizeof *cal||!cal->resistance[0]||!cal->resistance[1]||
     cal->resistance[0]>=0xffffffu||cal->resistance[1]>=0xffffffu){out->reason=ZizhanAmpPrerequisite;return 0;}
  out->calibration_r[0]=cal->resistance[0];out->calibration_r[1]=cal->resistance[1];
 }
 const uint64_t limit=clocked_profile>=5?8000000u:clocked_profile?5000000u:2000000u;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 if(verified!=flags||!coeff||coeff->status!=ZizhanAmpPass||coeff->completed!=2||coeff->source_verified!=2||coeff->transport.controller_uncertain||
    coeff->attempted_writes!=1883||coeff->acknowledged_writes!=1883){out->reason=ZizhanAmpPrerequisite;return 0;}
 for(unsigned c=0;c<2;++c)if(coeff->identity_words[c]!=22||coeff->readbacks[c]!=(c?950u:933u)||coeff->segments_verified[c]!=(c?2u:1u)||
    coeff->state_count[c]!=(c?18u:12u)||(coeff->after_state[c][0]&1)||(coeff->after_state[c][1]&0x31)||(coeff->after_state[c][2]&1)||
    memcmp(coeff->identity[c],coeff_identity,sizeof coeff_identity)){out->reason=ZizhanAmpPrerequisite;return 0;}
 if(clocked_profile>=15&&(coeff->after_state[0][2]||coeff->after_state[1][2])){out->reason=ZizhanAmpPrerequisite;return 0;}
 if(!bus||!bus->io.read||!bus->io.write||!clock||!clock->now_us||!clock->pause_us){out->status=ZizhanAmpFail;out->reason=ZizhanAmpArgument;return out->reason;}
 if(geni_i2c_busy(bus)){out->reason=ZizhanAmpBusy;return 0;}
 const uint8_t original=bus->address;const uint64_t begin=clock->now_us(clock->context);int reason=0;
 for(unsigned chip=0;chip<2;++chip){
  unsigned core_enabled=0;NativePulse pulse_context={bus,clock,out,transfer,chip};ZizhanPulseOps pulse_io=pulse_ops(&pulse_context);out->failed_chip=chip;out->failed_stage=1;bus->address=chip?0x42:0x40;
  ZizhanAmpBusConfig config;zizhan_cs35l41_bus_config(1,verified,bus,&config);
  if(!owned_config(&config,1)){reason=ZizhanAmpStateChanged;goto fail;}
  AMP_CHECKPOINT(chip,1);
  reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
  out->core_before[chip]=out->after_state[chip][2];
  if(out->native_frame){
   // Actual vendor pcm_hw_params96k maps to3 (generic upstream maps4).
   // WMFW metadata MAX_LRCLK_DELAY globalXMoffset13/nativepre_run32.
   reason=run_read(bus,clock,0x2c0c,&out->global_fs_before[chip],out);if(reason)goto fail;
   /* GLOBAL_FS 4 = 96 kHz: the ADSP TDM backend runs 96 kHz/24-bit/3 slots (as Android agm). */
   reason=run_write(bus,clock,0x2c0c,(out->global_fs_before[chip]&~0x1fu)|4u,out);if(reason)goto fail;++out->frame_writes[chip];
   reason=run_read(bus,clock,0x2c0c,&out->global_fs_after[chip],out);if(reason)goto fail;
   if(out->global_fs_after[chip]!=((out->global_fs_before[chip]&~0x1fu)|4u)){reason=ZizhanAmpReadback;goto fail;}
   reason=run_write(bus,clock,0x2800804,32,out);if(reason)goto fail;++out->frame_writes[chip];
   reason=run_read(bus,clock,0x2800804,&out->lrclk_delay[chip],out);if(reason)goto fail;
   if(out->lrclk_delay[chip]!=32){reason=ZizhanAmpReadback;goto fail;}
  }
  if(out->native_calibration){
   // Original fwindex9 wm_halo_apply_calibration before MPU/core. Copy
   // actual cached factory values to volatile RAM;no measurement,OTP,
   // CAL_AMBIENT(fw10 only),or permanent storage write.
   const uint32_t regs[]={0x2800800,0x2800268,0x2800274,0x2800270};
   const uint32_t values[]={1,out->calibration_r[chip],out->calibration_r[chip]+1u,1};
   for(unsigned i=chip?1u:0u;i<4;++i){uint32_t value=0;
    reason=run_write(bus,clock,regs[i],values[i],out);if(reason)goto fail;
    reason=run_read(bus,clock,regs[i],&value,out);if(reason)goto fail;
    if(value!=values[i]){out->failed_register=regs[i];out->expected_readback=values[i];out->observed_readback=value;reason=ZizhanAmpReadback;goto fail;}
    ++out->prerun_writes[chip];++out->prerun_readbacks[chip];
    if(i==0){out->us_bypass[chip]=value;}if(i==2)out->calibration_checksum[chip]=value;if(i==3)out->calibration_status[chip]=value;
   }
  }
  if(out->asp_format){
   // Actual native TDM short-sync,DSP_A/slave/normal,32-bit RX slots.
   // Preserve TX width,force and reserved bits. No data/output enable.
   reason=run_read(bus,clock,0x4808,&out->asp_format_before[chip],out);if(reason)goto fail;++out->asp_format_reads[chip];
   const uint32_t expected=(out->asp_format_before[chip]&~0xff000755u)|0x20000000u;
   reason=run_write(bus,clock,0x4808,expected,out);if(reason)goto fail;
   reason=run_read(bus,clock,0x4808,&out->asp_format_after[chip],out);if(reason)goto fail;
   if(out->asp_format_after[chip]!=expected){out->failed_register=0x4808;out->expected_readback=expected;out->observed_readback=out->asp_format_after[chip];reason=ZizhanAmpReadback;goto fail;}
   ++out->asp_format_reads[chip];
  }
  if(out->frequency_monitor){
   reason=run_read(bus,clock,0x2d10,&out->monitor_before[chip],out);if(reason)goto fail;
   reason=run_write(bus,clock,0x2d10,0x24010,out);if(reason)goto fail;++out->monitor_writes[chip];
   reason=run_read(bus,clock,0x2d10,&out->monitor_after[chip],out);if(reason)goto fail;
   if(out->monitor_after[chip]!=0x24010){reason=ZizhanAmpReadback;goto fail;}
  }
  reason=run_read(bus,clock,0x2c04,&out->pll[chip],out);if(reason)goto fail;
  if(clocked_profile){out->failed_stage=7;out->pll_before[chip]=out->pll_current[chip]=out->pll[chip];
   for(unsigned i=0;i<6;++i){uint32_t readback=0;const uint32_t value=(out->pll_current[chip]&~run_pll[i][0])|run_pll[i][1];
    if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
    reason=run_write(bus,clock,0x2c04,value,out);if(reason)goto fail;
    reason=run_read(bus,clock,0x2c04,&readback,out);if(reason)goto fail;
    if(readback!=value){out->failed_register=0x2c04;out->expected_readback=value;out->observed_readback=readback;reason=ZizhanAmpReadback;goto fail;}
    out->pll_current[chip]=readback;++out->pll_steps[chip];}
   out->pll[chip]=out->pll_current[chip];}
  reason=run_read(bus,clock,0x2c08,&out->dsp_clock[chip],out);if(reason)goto fail;
  if(out->global_profile){
   // Only internal GLOBAL clock/power gate. AMP/BST and the whole ASP
   // enables register are read-back disabled;no output interface writes.
   reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
   if(out->pulse_profile&&(zizhan_amp_pulse_flags&2u)){out->pulse[chip].patch_on=1;}
   else if(out->pulse_profile){AMP_CHECKPOINT(chip,2);reason=zizhan_pulse_patch(&pulse_io,&out->pulse[chip],1);if(reason){reason=ZizhanAmpProtocol;goto fail;}AMP_CHECKPOINT(chip,3);}
   reason=run_write(bus,clock,0x2014,coeff->after_state[chip][0]|1u,out);if(reason)goto fail;
   out->global_active[chip]=1;reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->global_on[chip];
   AMP_CHECKPOINT(chip,4);
  }
  if(out->pll_capture||clocked_profile>=5){
   out->failed_stage=8;
   for(unsigned i=0;i<4;++i){reason=run_read(bus,clock,0x10010+i*4,&out->pll_sticky[chip][i],out);if(reason)goto fail;}
   for(unsigned sample=0;sample<64;++sample){
    if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
    reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
    for(unsigned i=0;i<4;++i){uint32_t value=0;reason=run_read(bus,clock,0x10090+i*4,&value,out);if(reason)goto fail;
     if(!sample)out->pll_raw_first[chip][i]=out->pll_raw_and[chip][i]=value;
     out->pll_raw_last[chip][i]=value;out->pll_raw_or[chip][i]|=value;out->pll_raw_and[chip][i]&=value;}
    if(out->pll_raw_last[chip][2]&2)++out->pll_lock_samples[chip];
    if(out->frequency_monitor){uint32_t detect=0;reason=run_read(bus,clock,0x14000,&detect,out);if(reason)goto fail;
     if(!sample){out->clock_detect_first[chip]=detect;}out->clock_detect_last[chip]=detect;out->clock_detect_or[chip]|=detect;}
    ++out->pll_samples[chip];if(sample<63)clock->pause_us(clock->context,1000);
   }
   if(out->pll_capture&&out->global_profile){
    reason=run_write(bus,clock,0x2014,coeff->after_state[chip][0],out);if(reason)goto fail;
    out->global_active[chip]=0;reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->global_off[chip];
   }
   if(out->pll_capture){++out->completed;continue;}
   // Real locked reference is mandatory before any HALO/core write.
   if(out->pll_lock_samples[chip]<32||!(out->pll_raw_last[chip][2]&2)||(out->pll_raw_last[chip][2]&0x10)){reason=ZizhanAmpPrerequisite;goto fail;}
   AMP_CHECKPOINT(chip,5);
  }
  if(out->asp_rx_profile){
   // Native DAPM ASPRX1 receives only. AMP/BST remain disabled,all ASP
   // transmit bits remain0,data pads are not connected by this core.
   reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
   reason=run_write(bus,clock,0x4800,0x10000,out);if(reason)goto fail;out->asp_rx_active[chip]=1;
   reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->asp_rx_on[chip];
  }
  for(unsigned i=0;i<16;++i){
   if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
   const uint32_t reg=0x2b80080+(i%8)*8+(i>=8?0x200:0);uint32_t value=0;
   out->failed_stage=2;reason=run_write(bus,clock,reg,1,out);if(reason)goto fail;
   reason=run_read(bus,clock,reg,&value,out);if(reason)goto fail;if(value!=1){reason=ZizhanAmpReadback;goto fail;}++out->rate_readbacks[chip];
  }
  out->failed_stage=3;
  for(unsigned i=0;i<23;++i){
   if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
   reason=run_write(bus,clock,run_mpu[i][0],run_mpu[i][1],out);if(reason)goto fail;
   if(i>=2&&i<22){uint32_t value=0;reason=run_read(bus,clock,run_mpu[i][0],&value,out);if(reason)goto fail;
    out->mpu_values[chip][i-2]=value;++out->mpu_readbacks[chip];
    const uint32_t wanted=observed_profile?observed_mpu[i-2]:run_mpu[i][1];
    if(value!=wanted){++out->mpu_mismatches[chip];out->failed_register=run_mpu[i][0];out->expected_readback=wanted;out->observed_readback=value;
     if(!capture_only){reason=ZizhanAmpReadback;goto fail;}}}
  }
  if(observed_profile||clocked_profile==6||clocked_profile==14)for(unsigned i=0;i<20;++i){uint32_t value=0;
   reason=run_read(bus,clock,run_mpu[i+2][0],&value,out);if(reason)goto fail;
   if(value!=out->mpu_values[chip][i]||(observed_profile&&value!=observed_mpu[i])){out->failed_register=run_mpu[i+2][0];out->expected_readback=out->mpu_values[chip][i];out->observed_readback=value;reason=ZizhanAmpReadback;goto fail;}
   ++out->mpu_stable_readbacks[chip];}
  reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
  if(out->native_rates){
   // Native wm_halo_start_core uses zero-initialized RX/TX rate arrays.
   // This comes AFTER initialization FS errata1;preserve non-rate bits.
   out->failed_stage=9;
   for(unsigned i=0;i<16;++i){const uint32_t reg=0x2b80080+(i%8)*8+(i>=8?0x200:0);
    if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
    reason=run_read(bus,clock,reg,&out->native_rate_before[chip][i],out);if(reason)goto fail;
    const uint32_t expected=out->native_rate_before[chip][i]&~0x1fu;
    reason=run_write(bus,clock,reg,expected,out);if(reason)goto fail;++out->native_rate_writes[chip];
    reason=run_read(bus,clock,reg,&out->native_rate_after[chip][i],out);if(reason)goto fail;
    if(out->native_rate_after[chip][i]!=expected){out->failed_register=reg;out->expected_readback=expected;out->observed_readback=out->native_rate_after[chip][i];reason=ZizhanAmpReadback;goto fail;}
    ++out->native_rate_readbacks[chip];
   }
  }
  if(capture_only){
   if(out->global_profile){reason=run_write(bus,clock,0x2014,coeff->after_state[chip][0],out);if(reason)goto fail;
    out->global_active[chip]=0;reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->global_off[chip];}
   if(out->asp_rx_profile){reason=run_write(bus,clock,0x4800,0,out);if(reason)goto fail;out->asp_rx_active[chip]=0;
    reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->asp_rx_off[chip];}
   ++out->completed;continue;
  }
  reason=run_debug_snapshot(bus,clock,out,0,begin);if(reason)goto fail;
  AMP_CHECKPOINT(chip,6);
  out->failed_stage=4;core_enabled=1;
  reason=run_write(bus,clock,0x2bc1000,out->core_before[chip]|1u,out);if(reason)goto stop_fail;
  reason=run_read(bus,clock,0x2bc1000,&out->core_running[chip],out);if(reason)goto stop_fail;
  if(out->core_running[chip]!=(out->core_before[chip]|1u)){reason=ZizhanAmpReadback;goto stop_fail;}++out->started[chip];
  AMP_CHECKPOINT(chip,7);
  reason=run_debug_snapshot(bus,clock,out,1,begin);if(reason)goto stop_fail;
  out->failed_stage=5;
  for(unsigned sample=0;sample<(clocked_profile?100u:32u);++sample){
   if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto stop_fail;}
   uint32_t heartbeat=0;
   reason=run_state(chip,bus,clock,coeff,out,1);if(reason)goto stop_fail;
   reason=run_read(bus,clock,0x28007d0,&out->halo_state[chip],out);if(reason)goto stop_fail;
   reason=run_read(bus,clock,0x28007d4,&heartbeat,out);if(reason)goto stop_fail;
   reason=run_read(bus,clock,0x2800258,&out->cspl_state[chip],out);if(reason)goto stop_fail;
   reason=run_read(bus,clock,0x13004,&out->mailbox[chip],out);if(reason)goto stop_fail;
   for(unsigned i=0;i<3;++i){reason=run_read(bus,clock,0x2bc3104+i*8,&out->violations[chip][i],out);if(reason)goto stop_fail;
    if(out->violations[chip][i]){reason=ZizhanAmpProtocol;goto stop_fail;}}
   reason=run_read(bus,clock,0x2bc7008,&out->watchdog[chip],out);if(reason)goto stop_fail;
   if(out->watchdog[chip]){reason=ZizhanAmpProtocol;goto stop_fail;}
   if(sample==49&&transfer){
    AMP_CHECKPOINT(chip,8);
    if((clocked_profile<15||clocked_profile>19)||(out->play_profile?!transfer->stream:!transfer->once)||out->halo_state[chip]!=2||out->heartbeat_changes[chip]<10){reason=ZizhanAmpPrerequisite;goto stop_fail;}
    if(out->mailbox_profile){
     reason=native_mailbox(chip,0,bus,clock,coeff,out);if(reason)goto stop_fail;
     reason=native_mailbox(chip,1,bus,clock,coeff,out);if(reason)goto stop_fail;
    }
    if(out->iv_profile){reason=native_iv(chip,0,bus,clock,out);if(reason)goto stop_fail;}
    // Profile 19: no per-chip transfer or pulse; both amps open together in play_window after bring-up.
    if(!out->play_profile){
    out->transfer_halo[chip][0]=out->halo_state[chip];out->transfer_heartbeat[chip][0]=heartbeat;++out->transfer_attempts[chip];
    if(out->pulse_profile){
     // Actual zero pre-roll uses a distinct reserved tail,token and V2DONE;
     // no enable before that completion and another live-core/off check.
     AMP_CHECKPOINT(chip,9);
     if(transfer->once(transfer->context,chip+2)!=1){reason=ZizhanAmpProtocol;goto stop_fail;}
     reason=run_state(chip,bus,clock,coeff,out,1);if(reason)goto stop_fail;
     AMP_CHECKPOINT(chip,10);
     {const int pulse_rc=zizhan_pulse_run(&pulse_io,&out->pulse[chip]);AMP_CHECKPOINT(chip,pulse_rc?12u:11u);if(pulse_rc){reason=ZizhanAmpProtocol;goto stop_fail;}}
    }else if(transfer->once(transfer->context,chip)!=1){reason=ZizhanAmpProtocol;goto stop_fail;}
    ++out->transfer_acks[chip];
    reason=run_state(chip,bus,clock,coeff,out,1);if(reason)goto stop_fail;
    reason=run_read(bus,clock,0x28007d0,&out->transfer_halo[chip][1],out);if(reason)goto stop_fail;
    reason=run_read(bus,clock,0x28007d4,&out->transfer_heartbeat[chip][1],out);if(reason)goto stop_fail;
    if(out->iv_profile){reason=native_iv(chip,1,bus,clock,out);if(reason)goto stop_fail;}
    if(out->transfer_halo[chip][1]!=2||out->transfer_heartbeat[chip][1]==heartbeat){reason=ZizhanAmpTimeout;goto stop_fail;}
    }
   }
   if(!sample)out->heartbeat_first[chip]=heartbeat;
   else if(heartbeat!=out->heartbeat_last[chip])++out->heartbeat_changes[chip];
   out->heartbeat_last[chip]=heartbeat;++out->samples[chip];
   if(sample==0||sample==49||sample==99){reason=run_debug_snapshot(bus,clock,out,sample==0?2u:sample==49?3u:4u,begin);if(reason)goto stop_fail;}
   if(!clocked_profile&&out->heartbeat_changes[chip]>=2&&out->halo_state[chip])break;
   if(sample<(clocked_profile?99u:31u))clock->pause_us(clock->context,10000);
  }
  if(out->heartbeat_changes[chip]<2||!out->halo_state[chip]){reason=ZizhanAmpTimeout;goto stop_fail;}
  if(clocked_profile&&(out->samples[chip]!=100||out->heartbeat_changes[chip]<10||out->halo_state[chip]!=2)){reason=ZizhanAmpTimeout;goto stop_fail;}
  // Profile 19: leave this chip prepared (HALO running, GLOBAL/ASPRX on, AMP off) for the window.
  if(out->play_profile){out->play_ready[chip]=1;++out->completed;continue;}
  out->failed_stage=6;reason=run_write(bus,clock,0x2bc1000,out->core_before[chip],out);if(reason)goto fail;
  core_enabled=0;reason=run_read(bus,clock,0x2bc1000,&out->core_after[chip],out);if(reason)goto fail;
  if(out->core_after[chip]!=out->core_before[chip]){reason=ZizhanAmpReadback;goto fail;}++out->stopped[chip];
  AMP_CHECKPOINT(chip,13);
  reason=run_debug_snapshot(bus,clock,out,5,begin);if(reason)goto fail;
  reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;
  if(out->global_profile){
   reason=run_write(bus,clock,0x2014,coeff->after_state[chip][0],out);if(reason)goto fail;
   out->global_active[chip]=0;reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->global_off[chip];
   if(out->pulse_profile&&(zizhan_amp_pulse_flags&2u)){out->pulse[chip].patch_off=1;}
   else if(out->pulse_profile){if(zizhan_pulse_patch(&pulse_io,&out->pulse[chip],0)){reason=ZizhanAmpProtocol;goto fail;}AMP_CHECKPOINT(chip,14);}
  }
  if(out->asp_rx_profile){
   reason=run_write(bus,clock,0x4800,0,out);if(reason)goto fail;out->asp_rx_active[chip]=0;
   reason=run_state(chip,bus,clock,coeff,out,0);if(reason)goto fail;++out->asp_rx_off[chip];
  }
  ++out->completed;continue;
stop_fail:
  if(core_enabled&&!out->transport.controller_uncertain&&!geni_i2c_busy(bus)){
   if(!run_write(bus,clock,0x2bc1000,out->core_before[chip],out)&&
      !run_read(bus,clock,0x2bc1000,&out->core_after[chip],out)&&out->core_after[chip]==out->core_before[chip]){
       ++out->stopped[chip];if(out->core_debug)(void)run_debug_snapshot(bus,clock,out,5,begin);}
  }
  goto fail;
 }
 if(out->play_profile){reason=play_window(bus,clock,coeff,out,transfer,begin,limit);if(reason)goto fail;}
 if(clock->now_us(clock->context)-begin>=limit){reason=ZizhanAmpTimeout;goto fail;}
 out->elapsed_us=clock->now_us(clock->context)-begin;bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
fail:
 if(out->play_profile){out->play_reason=reason;out->play_stage=out->failed_stage;out->play_chip=out->failed_chip;play_teardown_all(bus,clock,coeff,out,begin);}
 out->elapsed_us=clock->now_us(clock->context)-begin;if(!out->transport.controller_uncertain)bus->address=original;
 out->status=ZizhanAmpFail;out->reason=reason;return reason;
}
int zizhan_cs35l41_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,0,0,0,0);}
int zizhan_cs35l41_mpu_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,1,0,0,0,0);}
int zizhan_cs35l41_observed_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,0,0,0);}
int zizhan_cs35l41_clocked_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,1,0,0);}
int zizhan_cs35l41_pll_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,0,2,0,0);}
int zizhan_cs35l41_clock_monitor_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,0,3,0,0);}
int zizhan_cs35l41_global_pll_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,0,4,0,0);}
int zizhan_cs35l41_global_silent_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,0,5,0,0);}
int zizhan_cs35l41_global_mpu_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,1,0,6,0,0);}
/* Full vector and close-stability independently captured on BOTH real
 * amps under actualPLLlock/GLOBALrun in mode13. Exact comparison only;
 * this is not a proof of MPU rights or a relaxation of strict mode12. */
int zizhan_cs35l41_global_observed_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,7,0,0);}
int zizhan_cs35l41_native_frame_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,8,0,0);}
int zizhan_cs35l41_native_debug_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,9,0,0);}

int zizhan_cs35l41_native_protection_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,10,cal,0);}

int zizhan_cs35l41_native_rates_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,11,cal,0);}

int zizhan_cs35l41_asp_rx_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,12,cal,0);}

int zizhan_cs35l41_asp_format_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,13,cal,0);}

int zizhan_cs35l41_native_core_capture(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,1,0,14,cal,0);}

/* Core0 fullvector andpostclosestability independently observed on BOTH
 * real amps in mode21;do not infer permissions orrelax oldcore100 trials. */
int zizhan_cs35l41_native_core_run(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,ZizhanAmpRun *out){return silent_run(enabled,verified,bus,clock,coeff,out,0,1,15,cal,0);}

int zizhan_cs35l41_native_core_transfer(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *out){
 if(!transfer||!transfer->once){if(out){memset(out,0,sizeof *out);out->reason=ZizhanAmpPrerequisite;}return 0;}
 return silent_run(enabled,verified,bus,clock,coeff,out,0,1,15,cal,transfer);
}

static int output_read(GeniI2c *bus,const ZizhanAmpClock *clock,uint32_t reg,uint32_t *value,ZizhanAmpIdentity *transport){
 unsigned allowed=0;for(unsigned i=0;i<ZizhanOutputFields;++i)if(reg==zizhan_output_fields[i].reg)allowed=1;
 if(!allowed)return ZizhanAmpArgument;
 const uint8_t request[]={(uint8_t)(reg>>24),(uint8_t)(reg>>16),(uint8_t)(reg>>8),(uint8_t)reg};
 if(geni_i2c_start_read_bytes(bus,request,4,4))return ZizhanAmpBusy;
 const int reason=finish_transfer(bus,clock,transport);if(!reason)*value=wire_be32(bus->rx);return reason;
}
static int output_state(unsigned chip,GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,uint32_t values[3],ZizhanAmpOutputSetup *out){
 const uint32_t regs[]={0x2014,0x2018,0x2bc1000};
 for(unsigned i=0;i<3;++i){int rc=read_identity(bus,clock,regs[i],&values[i],&out->transport);if(rc)return rc;
  if(values[i]!=coeff->after_state[chip][i]||(i==0&&(values[i]&1))||(i==1&&(values[i]&0x31))||(i==2&&values[i]))return ZizhanAmpStateChanged;}
 return 0;
}
int zizhan_cs35l41_output_setup(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,const ZizhanAmpCoefficients *coeff,ZizhanAmpOutputSetup *out){
 if(!out){return ZizhanAmpArgument;}memset(out,0,sizeof *out);out->reason=ZizhanAmpOff;if(enabled!=1)return 0;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 if(verified!=flags||!coeff||coeff->status!=ZizhanAmpPass||coeff->completed!=2||coeff->source_verified!=2||coeff->transport.controller_uncertain||
    coeff->attempted_writes!=1883||coeff->acknowledged_writes!=1883||coeff->readbacks[0]!=933||coeff->readbacks[1]!=950){out->reason=ZizhanAmpPrerequisite;return 0;}
 for(unsigned c=0;c<2;++c)if(coeff->identity_words[c]!=22||coeff->segments_verified[c]!=(c?2u:1u)||coeff->state_count[c]!=(c?18u:12u)||
    memcmp(coeff->identity[c],coeff_identity,sizeof coeff_identity)||coeff->after_state[c][0]&1||coeff->after_state[c][1]&0x31||coeff->after_state[c][2]){out->reason=ZizhanAmpPrerequisite;return 0;}
 if(!bus||!bus->io.read||!bus->io.write||!clock||!clock->now_us||!clock->pause_us){out->reason=ZizhanAmpArgument;return 0;}
 if(geni_i2c_busy(bus)){out->reason=ZizhanAmpBusy;return 0;}
 const uint8_t original=bus->address;const uint64_t begin=clock->now_us(clock->context);int reason=0;
 // Read BOTH real chips before the first configuration write.
 for(unsigned c=0;c<2;++c){bus->address=c?0x42:0x40;out->failed_chip=c;reason=output_state(c,bus,clock,coeff,out->state_before[c],out);if(reason)goto failed;}
 for(unsigned c=0;c<2;++c){bus->address=c?0x42:0x40;out->failed_chip=c;
  for(unsigned i=0;i<ZizhanOutputFields;++i){out->failed_index=i;const ZizhanOutputField *f=&zizhan_output_fields[i];const uint32_t desired=c?f->bottom:f->top;
   uint64_t now=clock->now_us(clock->context);if(now<begin||now-begin>=2000000u){reason=ZizhanAmpTimeout;goto failed;}
   reason=output_state(c,bus,clock,coeff,out->state_after[c],out);if(reason)goto failed;
   reason=output_read(bus,clock,f->reg,&out->before[c][i],&out->transport);if(reason)goto failed;
   if(desired&~f->mask||out->attempted>=2*ZizhanOutputFields){reason=ZizhanAmpArgument;goto failed;}
   const uint32_t expected=(out->before[c][i]&~f->mask)|desired;
   const uint8_t wire[]={(uint8_t)(f->reg>>24),(uint8_t)(f->reg>>16),(uint8_t)(f->reg>>8),(uint8_t)f->reg,(uint8_t)(expected>>24),(uint8_t)(expected>>16),(uint8_t)(expected>>8),(uint8_t)expected};
   ++out->attempted;if(geni_i2c_start_write_bytes(bus,wire,8)){reason=ZizhanAmpBusy;goto failed;}
   reason=finish_transfer(bus,clock,&out->transport);if(reason)goto failed;++out->acknowledged;
   reason=output_read(bus,clock,f->reg,&out->after[c][i],&out->transport);if(reason)goto failed;++out->readbacks[c];
   if(out->after[c][i]!=expected){reason=ZizhanAmpReadback;goto failed;}
   reason=output_state(c,bus,clock,coeff,out->state_after[c],out);if(reason)goto failed;
  }++out->completed;
 }
 {const uint64_t end=clock->now_us(clock->context);if(end<begin||end-begin>=2000000u){reason=ZizhanAmpTimeout;goto failed;}out->elapsed_us=end-begin;}
 bus->address=original;out->status=ZizhanAmpPass;out->reason=0;return 0;
failed:
 out->elapsed_us=clock->now_us(clock->context)-begin;out->status=ZizhanAmpFail;out->reason=reason;if(!out->transport.controller_uncertain)bus->address=original;return reason;
}

int zizhan_cs35l41_native_mailbox_transfer(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *out){
 if(!transfer||!transfer->once){if(out){memset(out,0,sizeof *out);out->reason=ZizhanAmpPrerequisite;}return 0;}
 return silent_run(enabled,verified,bus,clock,coeff,out,0,1,16,cal,transfer);
}

int zizhan_cs35l41_native_iv_transfer(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *out){
 if(!transfer||!transfer->once){if(out){memset(out,0,sizeof *out);out->reason=ZizhanAmpPrerequisite;}return 0;}
 return silent_run(enabled,verified,bus,clock,coeff,out,0,1,17,cal,transfer);
}

int zizhan_cs35l41_native_play_transfer(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *out){
 if(!transfer||!transfer->stream||zizhan_amp_play_ms<100||zizhan_amp_play_ms>2000){if(out){memset(out,0,sizeof *out);out->reason=ZizhanAmpPrerequisite;out->play_stage=90u+(!transfer?1u:!transfer->stream?2u:3u);}return 0;}
 const int rc=silent_run(enabled,verified,bus,clock,coeff,out,0,1,19,cal,transfer);
 return rc;
}
int zizhan_cs35l41_native_pulse_transfer(unsigned enabled,unsigned verified,GeniI2c *bus,const ZizhanAmpClock *clock,
 const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *cal,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *out){
 if(!transfer||!transfer->once){if(out){memset(out,0,sizeof *out);out->reason=ZizhanAmpPrerequisite;}return 0;}
 return silent_run(enabled,verified,bus,clock,coeff,out,0,1,18,cal,transfer);
}

/* Diagnostics: the C view of the run receipt size, compared by the C++ caller. */
unsigned zizhan_cs35l41_run_size(void){return (unsigned)sizeof(ZizhanAmpRun);}
