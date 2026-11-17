#!/usr/bin/env python3
"""Offline CS35L41 HALO protection-memory address plan. No transport/executor.

Only the protection WMFW and T/B protection coefficients are considered.
Scene parameters, errata/OTP register writes and all output controls excluded.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

REPO = Path(__file__).resolve().parents[3]
PREFLIGHT = Path(__file__).with_name("cs35l41-input-preflight.py")
spec = importlib.util.spec_from_file_location("cs35l41_inputs", PREFLIGHT)
inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inputs)

# Actual-phone cs35l41_dsp1_regions table, independently agrees with fixed
# Linux v6.6 cs35l41-lib.c. Register windows are aliases of 24-bit DSP words.
MEMORY = {
    "PM_PACKED": (0x03800000, "pm", 5),
    "XM_PACKED": (0x02000000, "xm", 3),
    "YM_PACKED": (0x02c00000, "ym", 3),
    "XM": (0x02800000, "xm", 4),
    "YM": (0x03400000, "ym", 4),
}
CHIP_WORDS = {"xm": 4096, "ym": 2048, "pm": 4096}
# Last documented32-bit register plus4, relative to each physical alias.
# Keep this additional guard even when a file declares a larger legal size.
REGISTER_BYTES = {"PM_PACKED": 0x4fec, "XM_PACKED": 0x2ff4, "YM_PACKED": 0x17f4,
                  "XM": 0x3ff8, "YM": 0x1ff8}


def unpack_xm_header(data, structure):
    # Packed HALO RAM: each transport32 value contains four little-endian
    # byte lanes. Regmap raw payload puts that value on the bus big-endian.
    # Restore lanes per32 bits, then unpack contiguous little-endian24 words.
    # This is a static image identity, never a substitute for runtime readback.
    rows = [r for r in structure["regions"] if r["type"] == "XM_PACKED" and r["word_offset"] == 0]
    if len(rows) != 1:
        raise ValueError("need one packed XM identity header at offset0")
    row = rows[0]
    start, size = row["file_offset"], row["bytes"]
    if size < 36 or size % 12:
        raise ValueError("unaligned packed HALO identity")
    raw = data[start:start + size]
    lanes = b"".join(raw[i:i + 4][::-1] for i in range(0, len(raw), 4))
    words = [int.from_bytes(lanes[i:i + 3], "little") for i in range(0, len(lanes), 3)]
    core, block_rev, vendor, fw_id, version, xm_base, xm_size, ym_base, ym_size, count = words[:10]
    if core != 0x40000 or block_rev != 0x30000 or not vendor or not fw_id or not version or not 0 < count <= 32:
        raise ValueError("unreviewed HALO identity/core/version")
    if 10 + 6 * count > len(words):
        raise ValueError("truncated HALO algorithm identity list")
    algorithms = [dict(id=fw_id, version=version, xm_base=xm_base, xm_size=xm_size,
                       ym_base=ym_base, ym_size=ym_size, firmware_global=True)]
    seen = {fw_id}
    for i in range(count):
        alg, ver, xb, xs, yb, ys = words[10 + 6 * i:16 + 6 * i]
        if not alg or not ver or alg in seen:
            raise ValueError("missing/duplicate HALO algorithm identity")
        seen.add(alg)
        algorithms.append(dict(id=alg, version=ver, xm_base=xb, xm_size=xs,
                               ym_base=yb, ym_size=ys, firmware_global=False))
    sizes = structure["sizes_words"]
    for alg in algorithms:
        for memory in ("xm", "ym"):
            base, length = alg[memory + "_base"], alg[memory + "_size"]
            if base > sizes[memory] or length > sizes[memory] - base:
                raise ValueError("HALO algorithm outside declared firmware memory")
    # Distinct algorithms may not share ownership; the global region can
    # include runtime identity/management data and is checked separately.
    for memory in ("xm", "ym"):
        regions = sorted((a[memory + "_base"], a[memory + "_base"] + a[memory + "_size"])
                         for a in algorithms[1:] if a[memory + "_size"])
        if any(b[0] < a[1] for a, b in zip(regions, regions[1:])):
            raise ValueError("overlapping HALO algorithm ownership")
    return dict(core=core, block_revision=block_rev, vendor=vendor, firmware_id=fw_id,
                firmware_version=version, algorithms=algorithms, source="static packed XM image")


def firmware_plan(structure, source_hash):
    result, occupied = [], {}
    sizes = structure["sizes_words"]
    for kind in ("xm", "ym", "pm"):
        if not 0 < sizes[kind] <= CHIP_WORDS[kind]:
            raise ValueError("firmware exceeds CS35L41 physical DSP memory")
    for region in structure["regions"]:
        kind = region["type"]
        if kind not in MEMORY:
            continue  # parser already limited metadata types; never write them
        base, memory, stride = MEMORY[kind]
        offset = region["word_offset"] * stride
        if kind in ("XM_PACKED", "YM_PACKED"):
            offset &= ~3
        size = region["bytes"]
        if size % 4 or offset % 4 or offset + size > sizes[memory] * stride or offset + size > REGISTER_BYTES[kind]:
            raise ValueError("unaligned/outside firmware memory write")
        interval = (offset // stride, (offset + size + stride - 1) // stride)
        for lo, hi in occupied.setdefault(memory, []):
            if interval[0] < hi and lo < interval[1]:
                raise ValueError("overlapping firmware aliases")
        occupied[memory].append(interval)
        result.append(dict(source_sha256=source_hash, source_offset=region["file_offset"],
                           bytes=size, memory=kind, register=base + offset))
    return result


def coefficient_plan(structure, identity, sizes, source_hash):
    algorithms = {alg["id"]: alg for alg in identity["algorithms"]}
    result, occupied = [], {}
    for block in structure["blocks"]:
        kind = block["type"]
        if kind not in MEMORY:
            continue
        base, memory, stride = MEMORY[kind]
        if memory == "pm":
            raise ValueError("coefficient patch to program memory forbidden")
        alg = algorithms.get(block["algorithm"])
        if alg is None or block["version"] != alg["version"]:
            raise ValueError("coefficient algorithm/version does not match static firmware")
        if block["sample_rate"]:
            raise ValueError("sample-rate-specific coefficients need explicit policy")
        alg_base, alg_size = alg[memory + "_base"], alg[memory + "_size"]
        if not alg_size:
            raise ValueError("coefficient targets empty algorithm memory")
        # cs_dsp_load_coeff translates algorithm base first, then adds the
        # byte offset. Do not multiply coefficient.offset by word stride.
        lower = alg_base * stride
        if kind.endswith("_PACKED"):
            lower &= ~3
        offset = lower + block["algorithm_relative_offset"]
        end = offset + block["bytes"]
        if offset % 4 or block["bytes"] % 4 or end > (alg_base + alg_size) * stride or end > sizes[memory] * stride or end > REGISTER_BYTES[kind]:
            raise ValueError("coefficient offset outside algorithm/firmware bounds")
        # Packed and unpacked addresses alias the same words: conservatively
        # reject overlapping affected words across both address formats.
        word_interval = (offset // stride, (end + stride - 1) // stride)
        for lo, hi in occupied.setdefault(memory, []):
            if word_interval[0] < hi and lo < word_interval[1]:
                raise ValueError("overlapping coefficient aliases")
        occupied[memory].append(word_interval)
        result.append(dict(source_sha256=source_hash, source_offset=block["file_offset"],
                           bytes=block["bytes"], memory=kind, algorithm=alg["id"],
                           algorithm_version=alg["version"], register=base + offset))
    if not result:
        raise ValueError("no coefficient writes resolved")
    return result


def build(bundle):
    framing = inputs.inspect(bundle)
    bundle = bundle.resolve()
    by_name = {row["file"]: row for row in framing["files"]}
    name = "cs35l41-dsp1-spk-prot.wmfw"
    fw = by_name[name]
    data = (bundle / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != fw["sha256"]:
        raise ValueError("firmware changed after bundle inspection")
    identity = unpack_xm_header(data, fw["structure"])
    common = firmware_plan(fw["structure"], fw["sha256"])
    channels = []
    for prefix, address in (("T", 0x40), ("B", 0x42)):
        coeff = by_name[prefix + "-cs35l41-dsp1-spk-prot.bin"]
        writes = coefficient_plan(coeff["structure"], identity, fw["structure"]["sizes_words"], coeff["sha256"])
        channels.append(dict(prefix=prefix, address=address, coefficient_file=coeff["file"],
                             firmware_segments=common, coefficient_segments=writes))
    return dict(input_manifest_sha256=framing["capture_manifest_sha256"], identity=identity,
                channels=channels, nominal_chip_id=0x35a40, nominal_revision=0xb2,
                total_planned_bytes=sum(sum(s["bytes"] for s in c["firmware_segments"] + c["coefficient_segments"]) for c in channels))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bundle", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(REPO) or output.exists():
        raise SystemExit("new private output path required")
    report = dict(status="FAIL", created=datetime.now(timezone.utc).isoformat(),
                  scope="offline-protection-memory-plan-only", phone_operations=False,
                  plan_executable=False, hardware_writes=False, dsp_started=False, audio_output=False,
                  runtime_algorithm_readback_verified=False, b2_errata_applied=False, otp_unpacked=False,
                  scene_parameters_applied=False, calibration_run=False,
                  remaining_gates=["B2 initialization and volatile OTP unpack", "DSP stopped/exclusive ownership",
                                   "runtime algorithm identity/base readback", "firmware memory readback",
                                   "protection runtime and board ADSP route", "attended first output"],
                  sources_sha256={path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (Path(__file__), PREFLIGHT)})
    try:
        report.update(build(args.bundle))
        report["status"] = "HOST_VERIFIED_PROTECTION_MEMORY_PLAN"
    except (ValueError, KeyError, OSError, UnicodeError, TypeError, struct.error) as error:
        report["error"] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as f:
        json.dump(report, f, indent=2)
        f.write("\n")
    print(report["status"])
    return 0 if report["status"] == "HOST_VERIFIED_PROTECTION_MEMORY_PLAN" else 1


if __name__ == "__main__":
    raise SystemExit(main())
