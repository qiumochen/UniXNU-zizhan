#!/usr/bin/env python3
"""Review finite IPCC evidence. A log-only PASS does not prove device return."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROW = re.compile(
    r"^ZIZHAN_IPCC_STATS: sample=(\d+) at_ns=(\d+) irq=(\d+) received=(\d+) "
    r"delivered=(\d+) unknown=(\d+) budget=(\d+) subscriptions=(\d+) retries=(\d+) "
    r"adsp_callbacks=(\d+) per_id=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) atomic_fields=1 coherent_latch=0$")
FIELDS = ("sample", "at_ns", "irq", "received", "delivered", "unknown", "budget",
          "subscriptions", "retries", "adsp_callbacks", "aop0", "adsp0", "adsp2", "slpi0", "slpi2")


def review(text):
    faults = re.findall(r"panic\(cpu|PongAdsp: failed[^\r\n]*|PongAdsp: fatal[^\r\n]*|"
                        r"ZIZHAN_IPCC: FAIL[^\r\n]*|firmware crashed:|PongAudio:", text)
    rows, malformed = [], []
    for line in text.splitlines():
        if not line.startswith("ZIZHAN_IPCC_STATS:"):
            continue
        match = ROW.fullmatch(line)
        if not match:
            malformed.append(line)
        else:
            rows.append(dict(zip(FIELDS, map(int, match.groups()))))
    result = dict(status="FAIL", scope="log-only-observed-ADSP-doorbells", rows=rows,
                  faults=faults, malformed=malformed, device_return_verified=False,
                  slpi_qualified=False, lossless_delivery_qualified=False,
                  sampling="individually atomic fields; not a coherent hardware latch")
    reasons = []
    if not text.startswith("XNULOG v1 (zizhan klog: reason 1,"):
        reasons.append("missing fresh deadline header")
    if faults or malformed:
        reasons.append("fault or malformed stats")
    if not 2 <= len(rows) <= 8:
        reasons.append("need 2..8 finite samples")
    if "ZIZHAN_IPCC: PASS receiver-attached spi=261" not in text or "PongAdsp: shared zizhan IPCC routing" not in text:
        reasons.append("shared receiver path not witnessed")
    if "PongAdsp: stage=13" not in text or "PongBattery: physical battery power source published" not in text:
        reasons.append("ADSP/battery readiness absent")
    for i, row in enumerate(rows):
        if row["sample"] != i + 1:
            reasons.append("sample sequence mismatch")
        if row["unknown"] or row["budget"] or row["subscriptions"] != 3:
            reasons.append("unknown/budget or subscription mismatch")
        if row["slpi0"] or row["slpi2"]:
            reasons.append("unexpected SLPI callback")
        if i:
            previous = rows[i - 1]
            if row["at_ns"] - previous["at_ns"] < 10_000_000_000:
                reasons.append("sample interval below 10 seconds")
            # Regressions require review, including wrap; never silently turn
            # wrapped counters into successful delivery. No cross-field sums:
            # IRQ may run between the individually atomic snapshot loads.
            for key in FIELDS[2:]:
                if row[key] < previous[key]:
                    reasons.append("counter regressed or wrapped: " + key)
    if rows:
        last = rows[-1]
        if not all(last[k] > 0 for k in ("irq", "received", "delivered", "adsp_callbacks", "adsp0", "adsp2")):
            reasons.append("missing actual ADSP IRQ/callback evidence")
        if len(rows) >= 2 and not (last["received"] > rows[0]["received"] and
                                   last["delivered"] > rows[0]["delivered"]):
            reasons.append("no progress across samples")
    result["reasons"] = sorted(set(reasons))
    if not reasons:
        result["status"] = "PASS_SCOPED_OBSERVED_IRQ_DELIVERY"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    if output.is_relative_to(repo) or output.exists():
        parser.error("use a fresh private output path outside the repository")
    raw = args.log.read_bytes()
    result = review(raw.decode(errors="replace"))
    result["log_sha256"] = hashlib.sha256(raw).hexdigest()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(result["status"])
    raise SystemExit(0 if result["status"].startswith("PASS_") else 1)


if __name__ == "__main__":
    main()
