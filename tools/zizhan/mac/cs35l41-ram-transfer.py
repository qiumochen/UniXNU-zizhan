#!/usr/bin/env python3
"""Host-only protection firmware RAM chunk plan and supplied-byte verifier.

No device transport. Firmware bytes stay in the private bundle. Readback input
is an ordered JSON list of {address, register, data_hex}; its provenance must be
qualified separately by the device supervisor. Coefficients are excluded.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

REPO = Path(__file__).resolve().parents[3]
LOAD = Path(__file__).with_name("cs35l41-load-plan.py")
spec = importlib.util.spec_from_file_location("cs35l41_memory_plan", LOAD)
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)
FIRMWARE = "cs35l41-dsp1-spk-prot.wmfw"
MAX_FILE_BYTES = 1024 * 1024
MAX_RAM_BYTES_PER_CHIP = 128 * 1024
MAX_SEGMENTS = 32


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(bundle):
    """Inspect the retained bundle, then pin one immutable firmware byte copy."""
    base = memory.build(bundle)
    path = bundle.resolve() / FIRMWARE
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("firmware file exceeds bound")
    raw = path.read_bytes()
    if len(raw) > MAX_FILE_BYTES:
        raise ValueError("firmware file grew beyond bound")
    source_hash = digest(raw)
    segments = memory.firmware_plan(memory.inputs.wmfw(raw), source_hash)
    if not 0 < len(segments) <= MAX_SEGMENTS:
        raise ValueError("firmware segment count outside bound")
    # Pin against the independently inspected bundle, not an external plan.
    if any(c["firmware_segments"] != segments for c in base["channels"]):
        raise ValueError("firmware changed after inspected plan")
    if not 0 < sum(s["bytes"] for s in segments) <= MAX_RAM_BYTES_PER_CHIP:
        raise ValueError("firmware RAM bytes outside bound")
    chunks, report_segments = [], []
    for channel in base["channels"]:
        for index, segment in enumerate(segments):
            start, size = segment["source_offset"], segment["bytes"]
            if start < 0 or size <= 0 or start + size > len(raw):
                raise ValueError("firmware source span outside file")
            payload = raw[start:start + size]
            report_segments.append(dict(address=channel["address"], segment=index,
                                        **segment, payload_sha256=digest(payload),
                                        chunks=size // 4))
            for offset in range(0, size, 4):
                chunks.append((channel["address"], segment["register"] + offset,
                               raw[start + offset:start + offset + 4]))
    report = dict(status="HOST_VERIFIED_RAM_TRANSFER_PLAN", phone_operations=False,
                  hardware_writes=False, plan_executable=False, device_verified=False,
                  readback_provenance_verified=False, coefficients_included=False,
                  dsp_started=False, audio_output=False,
                  input_manifest_sha256=base["input_manifest_sha256"],
                  firmware_sha256=source_hash, segments=report_segments,
                  total_payload_bytes=len(chunks) * 4, write_transactions=len(chunks),
                  read_transactions=len(chunks),
                  transport=dict(payload_bytes=4, write_bytes=8, read_bytes=4,
                                 register_order="big-endian-32",
                                 payload_order="file bytes unchanged; no lane reversal",
                                 sequence="all writes, then complete ordered readback"),
                  remaining_gates=["runtime bounded executor with exclusive bus ownership",
                                   "whole-transfer deadline and measured transport budget",
                                   "fresh safe-off, identity, B2 and per-chip trim",
                                   "actual full RAM readback and runtime algorithm identity",
                                   "protection runtime and board audio route",
                                   "attended first sound"])
    return report, chunks


def verify(chunks, rows):
    """Validate supplied raw byte records only; never assert device provenance."""
    if not isinstance(rows, list) or len(rows) != len(chunks):
        raise ValueError("readback count mismatch")
    actual_hash = hashlib.sha256()
    for index, ((address, register, expected), row) in enumerate(zip(chunks, rows)):
        if not isinstance(row, dict) or set(row) != {"address", "register", "data_hex"}:
            raise ValueError(f"invalid readback record at chunk {index}")
        if type(row["address"]) is not int or type(row["register"]) is not int or \
                (row["address"], row["register"]) != (address, register):
            raise ValueError(f"readback address/order mismatch at chunk {index}")
        value = row["data_hex"]
        if not isinstance(value, str) or len(value) != 8:
            raise ValueError(f"invalid readback length at chunk {index}")
        try:
            data = bytes.fromhex(value)
        except ValueError as error:
            raise ValueError(f"invalid readback encoding at chunk {index}") from error
        if data != expected:
            raise ValueError(f"readback data mismatch at chunk {index}")
        actual_hash.update(data)
    return dict(status="SUPPLIED_READBACK_BYTES_MATCH", chunks=len(chunks),
                bytes=len(chunks) * 4, payload_sha256=actual_hash.hexdigest(),
                device_verified=False, provenance_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--readback", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(REPO) or output.exists():
        raise SystemExit("new private output path required")
    report = dict(status="FAIL", phone_operations=False, hardware_writes=False,
                  device_verified=False, plan_executable=False)
    try:
        report, chunks = prepare(args.bundle)
        if args.readback:
            if args.readback.stat().st_size > 16 * 1024 * 1024:
                raise ValueError("readback input exceeds bound")
            readback = args.readback.read_bytes()
            if len(readback) > 16 * 1024 * 1024:
                raise ValueError("readback input grew beyond bound")
            report["readback"] = verify(chunks, json.loads(readback))
            report["readback_input_sha256"] = digest(readback)
    except (ValueError, KeyError, TypeError, OSError, OverflowError, struct.error) as error:
        report.update(status="FAIL", error=str(error))
    report["sources_sha256"] = {p.name: digest(p.read_bytes()) for p in
                                (Path(__file__), LOAD, memory.PREFLIGHT)}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print(report["status"])
    return int(report["status"] == "FAIL")


if __name__ == "__main__":
    raise SystemExit(main())
