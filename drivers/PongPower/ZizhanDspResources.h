/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_DSP_RESOURCES_H
#define ZIZHAN_DSP_RESOURCES_H
#include "QcomQmp.h"
#include "QcomSmem.h"

// zizhan ADSP/SLPI share one AOSS mailbox and SMEM hardware mutex. These
// helpers belong to the existing ADSP worker, not to a second DSP thread.
// Callers must keep all QMP step/send and SMEM allocation on that same actor.
namespace zizhan_dsp {
enum class Processor { Adsp, Slpi };
enum { BadProcessor = -100, MailboxUnavailable = -101, AckTimeout = -102,
       SmemLockTimeout = -103 };

struct LoadStateOwner {
    bool timed_out = false;

    // wait(ms, predicate) pumps that worker's existing events with a finite
    // deadline. A timeout poisons this owner until reboot: a late ack cannot
    // be mistaken for completion of a subsequent processor's command.
    template <typename Wait>
    int send(QcomQmp &qmp, Processor processor, bool enabled, Wait wait) {
        const char *command;
        switch (processor) {
        case Processor::Adsp:
            command = enabled ? "{class: image, res: load_state, name: adsp, val: on}"
                              : "{class: image, res: load_state, name: adsp, val: off}";
            break;
        case Processor::Slpi:
            command = enabled ? "{class: image, res: load_state, name: slpi, val: on}"
                              : "{class: image, res: load_state, name: slpi, val: off}";
            break;
        default: return BadProcessor;
        }
        if (timed_out || qmp.state != QcomQmpOpen) return MailboxUnavailable;
        const int rc = qcom_qmp_send(&qmp, command);
        if (rc != QcomQmpSending) return rc;
        if (wait(1000, [&qmp] { return qcom_qmp_step(&qmp) == QcomQmpOpen; })) return 0;
        timed_out = true;
        qcom_qmp_abandon(&qmp);
        return AckTimeout;
    }
};

// One hardware mutex protects both remote partitions. Keep verification
// under that mutex too; an existing undersized item must never be handed to
// SMP2P/GLINK as though a full-size allocation had succeeded.
template <typename Lock, typename Unlock>
int allocate(const QcomSmem &smem, const QcomSmemPartition &part,
             uint16_t id, uint32_t bytes, Lock lock, Unlock unlock) {
    if (!bytes) return QcomSmemCorrupt;
    if (!lock()) return SmemLockTimeout;
    int rc = qcom_smem_alloc(&smem, &part, id, bytes);
    if (rc == QcomSmemOk || rc == QcomSmemExists) {
        uint32_t offset = 0, available = 0;
        rc = qcom_smem_find(&smem, &part, id, &offset, &available);
        if (!rc && available < bytes) rc = QcomSmemCorrupt;
    }
    unlock();
    return rc;
}
} // namespace zizhan_dsp
#endif
