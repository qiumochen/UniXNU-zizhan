#!/usr/bin/env python3
"""Collect application evidence without turning missing logs into a pass.

Run after cycle.sh and screenshot decoding. Outputs belong outside the public
repository. This preserves per-layer observations; visible page correctness,
forecast content and video playback always need a separate review.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import zlib


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def escaped_text(value):
    value = re.sub(r"\\x([0-9a-f]{2})", lambda match: chr(int(match[1], 16)), value)
    return value.encode("latin1", "replace").decode("utf-8", "replace")


def app_observations(text):
    """Join UTF-8 byte chunks only when offsets are contiguous; retain gaps."""
    rows, active = [], {}
    pattern = r'Zizhan App18 (\S+) (\d+) (\S+) byte=(\d+): ([^\r\n]*)'
    for match in re.finditer(pattern, text):
        process, sample, field, offset, payload = match.groups()
        key, offset = (process, int(sample), field), int(offset)
        raw = re.sub(r'\\x([0-9a-fA-F]{2})', lambda value: chr(int(value[1], 16)), payload).encode('latin1', 'replace')
        if offset == 0 or key not in active:
            row = {'process': process, 'sample': int(sample), 'field': field,
                   'bytes': bytearray(), 'complete_sequence': offset == 0,
                   'offsets': []}
            active[key] = row
            rows.append(row)
        row = active[key]
        if offset != len(row['bytes']):
            row['complete_sequence'] = False
        row['offsets'].append(offset)
        row['bytes'].extend(raw)
    for row in rows:
        raw = bytes(row.pop('bytes'))
        row['text'] = raw.decode('utf-8', 'replace')
        row['utf8_valid'] = '\ufffd' not in row['text']
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run")
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.run) or args.out.exists():
        parser.error("Use a safe run name and fresh output; preserve old results")
    source = args.logs / (args.run + ".txt")
    raw = source.read_bytes()
    text = raw.decode("latin1")
    lines = text.splitlines()
    root = Path(__file__).resolve().parent
    summary = load("actual_round_report", root / "round-report.py").summarize(source, 0)
    integrity = load("actual_shot_integrity", root / "shot-integrity.py")
    try:
        capture = integrity.verify(source, args.logs)
    except (ValueError, OSError, struct.error, zlib.error) as error:
        capture = {"status": "FAIL", "reason": str(error)}
    per_capture = []
    for number in sorted(set(int(value) for value in re.findall(r'PONG SHOT (\d+) begin ', text))):
        try:
            checked = integrity.verify(source, args.logs, identifiers=[number])
            per_capture.append({'shot': number, **checked})
        except (ValueError, OSError, struct.error, zlib.error) as error:
            per_capture.append({'shot': number, 'status': 'FAIL', 'reason': str(error)})
    categories = {
        "calendar": r"calendar epoch=",
        "navigation": r"Pong UI18: open-(?:sensitive|plain|app)|open prefs:",
        "network": r"PongWifi|wifi-net:|WiFi wired DMA|actual wired DMA|HTTPS.*(?:200|FAIL)|DHCP",
        "web_media_errors": r"WebKit.*(?:error|fail|crash)|VideoToolbox|VTDecompression|MediaError|NSURLErrorDomain|kCFErrorDomainCFNetwork",
        "network_boundary": r"Zizhan Net18",
        "allocation_failures": r"ZIZHAN KMEM:|DMA allocation SKIP|PONG CS4K:",
        "input_actions": r"Zizhan keyboard18:|zizhan touch:.*synthetic|Pong touch HID: type=",
        "shot_schedule": r"PONG SHOT:|PONG SHOT \d+ begin",
        "app_diagnostic_state": r"Zizhan App18.*(?:fatal|image=|begin windows=|observation enabled)",
    }
    events = {key: [escaped_text(line[:1200]) for line in lines if re.search(pattern, line)][:80]
              for key, pattern in categories.items()}
    controls = [escaped_text(match[1]) for match in
                re.finditer(r"Pong Settings18 ui \d+: ([^\n]*)", text)]
    network = "UNKNOWN_NO_STARTUP_EVIDENCE"
    if summary.get("wifi_arena_failed"):
        network = "FAIL_DMA_ALLOCATION"
    elif summary.get("wifi") or summary.get("wifi_dhcp_in"):
        network = "OBSERVED_REVIEW_STAGE_AND_REQUEST"
    result = {
        "run": args.run, "log_sha256": hashlib.sha256(raw).hexdigest(),
        "collection": {"fresh_deadline_header": raw.startswith(b"XNULOG v1 (zizhan klog: reason 1,"),
                       "has_kernel_startup": "Darwin Kernel Version" in text,
                       "has_calendar_result": bool(events["calendar"]),
                       "note": "Missing early events may reflect ring overwrite; no implicit success"},
        "network_verdict": network, "summary": summary,
        "capture_integrity": capture, "events": events, "settings_controls": controls,
        "per_capture_integrity": per_capture,
        "app_observations": app_observations(text),
        "application_acceptance": "PENDING_CONTENT_AND_OPERATION_REVIEW",
        "video_acceptance": "PENDING_REAL_FRAME_ADVANCE_AND_PLAY_PAUSE",
    }
    returned = args.logs / (args.run + ".android-return.json")
    result["android_return"] = json.loads(returned.read_text()) if returned.exists() else {"status": "UNKNOWN"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"run": args.run, "network": network,
                      "capture": capture["status"], "app": result["application_acceptance"]}))


if __name__ == "__main__":
    main()
