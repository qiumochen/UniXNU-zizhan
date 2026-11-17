#!/usr/bin/env python3
"""Offline original-phone CS35L41 input inventory. Never loads DSP or emits writes.

WMFW/WMDR framing follows Linux v6.6 wmfw.h/cs_dsp.c. Algorithm metadata and
coefficient runtime bases remain opaque; success is not a safe write plan.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct

REPO = Path(__file__).resolve().parents[3]
REQUIRED = {
    "cs35l41-dsp1-spk-prot.wmfw": "speaker-protection",
    "T-cs35l41-dsp1-spk-prot.bin": "speaker-protection",
    "B-cs35l41-dsp1-spk-prot.bin": "speaker-protection",
    "cs35l41-dsp1-spk-cali.wmfw": "calibration-input-not-to-run",
    "T-cs35l41-dsp1-spk-cali.bin": "calibration-input-not-to-run",
    "B-cs35l41-dsp1-spk-cali.bin": "calibration-input-not-to-run",
    **{name: "fast-switch-tuning" for name in
       ("T-music.txt", "T-handset.txt", "T-handfree.txt", "B-music.txt", "B-handfree.txt")},
}
TYPES = {0x10: "PM_PACKED", 0x11: "XM_PACKED", 0x12: "YM_PACKED",
         5: "XM", 6: "YM", 0xf2: "ALGORITHM_METADATA", 0xfe: "NAME", 0xff: "INFO"}


def bounded(data):
    if not data or len(data) > 16 * 1024 * 1024:
        raise ValueError("empty or oversized input")


def wmfw(data):
    bounded(data)
    if len(data) < 48 or data[:4] != b"WMFW":
        raise ValueError("invalid WMFW header")
    length, revision, core, version = struct.unpack_from("<IHBB", data, 4)
    if length != 40 or core != 4 or version != 3:
        raise ValueError("only reviewed HALO WMFW v3/40-byte header supported")
    xm, ym, pm, zm = struct.unpack_from("<IIII", data, 12)
    if not xm or not ym or not pm or zm or max(xm, ym, pm) > 16 * 1024 * 1024:
        raise ValueError("invalid or unreviewed HALO memory sizes")
    limits = {0x10: pm * 5, 0x11: xm * 3, 0x12: ym * 3, 5: xm * 4, 6: ym * 4}
    stride = {0x10: 5, 0x11: 3, 0x12: 3, 5: 4, 6: 4}
    position, rows, intervals = length, [], {}
    while position < len(data):
        if len(rows) >= 4096 or len(data) - position < 8:
            raise ValueError("WMFW region table truncated or too large")
        word, size = struct.unpack_from("<II", data, position)
        kind, offset = word >> 24, word & 0xffffff
        if kind not in TYPES or not size or size > len(data) - position - 8:
            raise ValueError("WMFW region type/length rejected")
        row = dict(type=TYPES[kind], word_offset=offset, bytes=size, file_offset=position + 8)
        if kind in limits:
            start = offset * stride[kind]
            if kind in (0x11, 0x12):
                start &= ~3  # HALO packed XM/YM register-alignment semantics
            if size % stride[kind] or start + size > limits[kind]:
                raise ValueError("WMFW memory region outside declared size/alignment")
            for lower, upper in intervals.setdefault(kind, []):
                if start < upper and lower < start + size:
                    raise ValueError("overlapping WMFW regions")
            intervals[kind].append((start, start + size))
            row["byte_offset_within_declared_memory"] = start
        rows.append(row)
        position += 8 + size
    if not all(kind in intervals for kind in (0x10, 0x11, 0x12)):
        raise ValueError("missing HALO PM/XM/YM payload")
    return dict(format="HALO_WMFW", version=version, revision=revision,
                sizes_words=dict(xm=xm, ym=ym, pm=pm, zm=zm), regions=rows,
                metadata_decoded=False, hardware_address_plan=False)


def wmdr(data):
    bounded(data)
    if len(data) < 36 or data[:4] != b"WMDR":
        raise ValueError("invalid WMDR header")
    length = struct.unpack_from("<I", data, 4)[0]
    revision = struct.unpack_from(">I", data, 8)[0] & 255
    core = struct.unpack_from(">I", data, 12)[0] & 255
    if length != 16 or revision not in (1, 2) or core != 4:
        raise ValueError("unreviewed WMDR header/core")
    position, rows, payloads = length, [], 0
    while position < len(data):
        if len(rows) >= 4096 or len(data) - position < 20:
            raise ValueError("WMDR block header truncated or too large")
        offset, kind, alg, version, sample_rate, size = struct.unpack_from("<HHIIII", data, position)
        end = position + 20 + size
        padded_end = (end + 3) & ~3
        if not size or end > len(data) or padded_end > len(data):
            raise ValueError("WMDR payload/padding truncated")
        if any(data[end:padded_end]):
            raise ValueError("nonzero WMDR alignment padding")
        if kind in (0xfe00, 0xff00, 0xfc00):
            name = {0xfe00: "NAME", 0xff00: "INFO", 0xfc00: "METADATA"}[kind]
        elif kind in (5, 6, 0x10, 0x11, 0x12):
            if not alg or size % 4:
                raise ValueError("WMDR algorithm payload rejected")
            name = TYPES[kind]
            payloads += 1
        else:
            # Absolute register writes are not a reviewed tuning input here.
            raise ValueError("unreviewed WMDR block type")
        rows.append(dict(type=name, algorithm=alg, version=version >> 8,
                         sample_rate=sample_rate, algorithm_relative_offset=offset,
                         bytes=size, file_offset=position + 20))
        position = padded_end
    if not payloads:
        raise ValueError("no algorithm coefficient payload")
    return dict(format="WMDR", revision=revision, core=core, blocks=rows,
                algorithm_bases_resolved=False, hardware_address_plan=False)


def tuning(data):
    bounded(data)
    text = data.decode("ascii").strip()
    if not re.fullmatch(r"[+-]?\d+(?:\s*,\s*[+-]?\d+)*", text):
        raise ValueError("invalid tuning integer stream")
    fields = text.split(",")
    if len(fields) > 65536 or any(len(value.strip()) > 12 for value in fields):
        raise ValueError("tuning token limit exceeded")
    values = [int(value) for value in fields]
    if values[0] != len(values) or len(values) < 2:
        raise ValueError("tuning declared count does not include all tokens")
    if any(value < -(1 << 31) or value >= 1 << 31 for value in values):
        raise ValueError("tuning integer outside signed32")
    return dict(format="COUNTED_SIGNED32_TEXT", words_including_count=len(values),
                signed_values=any(value < 0 for value in values),
                schema_decoded=False, hardware_address_plan=False)


def inspect(bundle):
    bundle = bundle.resolve()
    if bundle.is_relative_to(REPO):
        raise ValueError("firmware bundle must stay outside public repository")
    manifest_path = bundle / "manifest.json"
    if manifest_path.is_symlink() or manifest_path.stat().st_size > 1024 * 1024:
        raise ValueError("manifest symlink/size rejected")
    manifest = json.loads(manifest_path.read_text())
    if not isinstance(manifest, dict):
        raise ValueError("invalid manifest structure")
    if manifest.get("status") != "COPIED_AND_HASH_MATCHED_ONLY":
        raise ValueError("expected original-phone hash comparison manifest")
    if any(manifest.get(key) is not False for key in ("phone_writes", "firmware_loaded", "codec_writes", "audio_output")):
        raise ValueError("unexpected capture operation scope")
    rows = manifest["files"]
    if not isinstance(rows, list) or any(not isinstance(row, dict) or not isinstance(row.get("file"), str) for row in rows):
        raise ValueError("invalid manifest file rows")
    if len(rows) != len(REQUIRED) or {row["file"] for row in rows} != set(REQUIRED):
        raise ValueError("missing/duplicate/unreviewed bundle files")
    result = []
    for row in rows:
        name = row["file"]
        if row.get("role") != REQUIRED[name] or row.get("source") != "/vendor/firmware/" + name:
            raise ValueError("unexpected role or original source")
        path = bundle / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError("invalid bundle file")
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if row.get("sha256") != digest or row.get("remote_sha256") != digest or row.get("bytes") != len(data):
            raise ValueError("actual/remote input hash or size mismatch: " + name)
        parser = wmfw if name.endswith(".wmfw") else wmdr if name.endswith(".bin") else tuning
        result.append(dict(file=name, role=row["role"], sha256=digest, bytes=len(data), structure=parser(data)))
    by_name = {row["file"]: row for row in result}
    return dict(files=result, capture_manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                protection_and_calibration_wmfw_identical=by_name["cs35l41-dsp1-spk-prot.wmfw"]["sha256"] ==
                by_name["cs35l41-dsp1-spk-cali.wmfw"]["sha256"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(REPO) or output.exists():
        raise SystemExit("report must be a new private path")
    report = dict(status="FAIL", created=datetime.now(timezone.utc).isoformat(),
                  scope="offline-input-framing-only", phone_operations=False, hardware_write_plan=False,
                  firmware_loaded=False, dsp_started=False, tuning_applied=False, calibration_run=False,
                  audio_output=False, playback_qualified=False)
    report["firmware_authentication_checked"] = False
    report["checker_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    try:
        report.update(inspect(args.bundle))
        report["status"] = "HOST_VERIFIED_INPUT_FRAMING"
    except (ValueError, KeyError, OSError, UnicodeError, TypeError, struct.error) as error:
        report["error"] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    print(report["status"])
    return 0 if report["status"] == "HOST_VERIFIED_INPUT_FRAMING" else 1


if __name__ == "__main__":
    raise SystemExit(main())
