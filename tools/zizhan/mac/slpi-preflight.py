#!/usr/bin/env python3
"""Offline zizhan SLPI DT/firmware placement preflight; never accesses a phone.

The output is a private evidence report, not a DSP boot or sensor qualification.
Firmware and raw DT files remain local. The actual kernel MDT parser performs
placement checks; no second implementation of ELF segment placement is used.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[3]


def cells(props, key):
    raw = props[key]
    if len(raw) % 4:
        raise ValueError("unaligned DT cells: " + key)
    return [int.from_bytes(raw[i:i + 4], "big") for i in range(0, len(raw), 4)]


def describe_nodes(nodes):
    """Resolve references only for properties that actually carry phandles."""
    root = nodes["/"]
    if b"zizhan" not in root.get("model", b"").lower():
        raise ValueError("DT model is not zizhan")
    if cells(root, "#address-cells") != [2] or cells(root, "#size-cells") != [2]:
        raise ValueError("unsupported root address/size cells")
    soc = nodes["/soc"]
    if (cells(soc, "#address-cells") != [1] or cells(soc, "#size-cells") != [1] or
            cells(soc, "ranges") != [0, 0, 0, 0xffffffff]):
        raise ValueError("missing reviewed identity bus translation")
    refs = {}
    for path, props in nodes.items():
        if "phandle" not in props:
            continue
        handle = cells(props, "phandle")
        if len(handle) != 1 or not handle[0] or handle[0] in refs:
            raise ValueError("invalid or duplicate DT phandle")
        refs[handle[0]] = path

    def active(path):
        while path:
            if nodes[path].get("status", b"okay\0") not in (b"ok\0", b"okay\0"):
                raise ValueError("disabled DT node: " + path)
            path = path.rsplit("/", 1)[0]

    def reference(props, key):
        value = cells(props, key)
        if len(value) != 1 or value[0] not in refs:
            raise ValueError("unresolved DT reference: " + key)
        path = refs[value[0]]
        active(path)
        return path, nodes[path]

    selected = [p for p, v in nodes.items()
                if v.get("compatible") == b"qcom,msm-ssc-sensors\0"]
    if len(selected) != 1:
        raise ValueError("need exactly one SSC sensor owner")
    ssc_path = selected[0]
    active(ssc_path)
    ssc = nodes[ssc_path]
    if ssc.get("qcom,firmware-name") != b"slpi\0":
        raise ValueError("SSC does not select SLPI firmware")
    dsp_path, dsp = reference(ssc, "qcom,rproc-handle")
    if (dsp.get("compatible") != b"qcom,waipio-slpi-pas\0" or
            cells(dsp, "reg") != [0x02400000, 0x10000]):
        raise ValueError("SSC remote processor is not reviewed zizhan SLPI")
    memory_path, memory = reference(dsp, "memory-region")
    if cells(memory, "reg") != [0, 0x88000000, 0, 0x1900000] or "no-map" not in memory:
        raise ValueError("SLPI reserved memory differs from reviewed carveout")
    edge_path = dsp_path + "/glink-edge"
    active(edge_path)
    edge = nodes[edge_path]
    if (edge.get("transport") != b"smem\0" or edge.get("label") != b"slpi\0" or
            edge.get("qcom,glink-label") != b"dsps\0" or
            cells(edge, "qcom,remote-pid") != [3]):
        raise ValueError("SLPI edge differs from reviewed SMEM/GLINK route")
    channels = [(p, v) for p, v in nodes.items() if p.rsplit("/", 1)[0] == edge_path and
                v.get("qcom,glink-channels") == b"IPCRTR\0"]
    if len(channels) != 1:
        raise ValueError("need exactly one SLPI IPCRTR channel")
    channel_path, channel = channels[0]
    active(channel_path)
    if cells(channel, "qcom,net-id") != [2]:
        raise ValueError("SLPI QRTR net-id differs from reviewed route")
    # remote-pid and net-id are numeric domains, not phandles or QRTR nodes.
    return dict(board="zizhan", soc="SM8475", ssc=ssc_path, remoteproc=dsp_path,
                compatible="qcom,waipio-slpi-pas", firmware="slpi", memory=memory_path,
                carveout=dict(base=0x88000000, bytes=0x1900000),
                route=dict(edge=edge_path, transport="smem", glink_label="dsps",
                           remote_pid=3, channel="IPCRTR", net_id=2),
                identity_translation=True, scope="saved-DT configuration only")


def describe_dt(raw):
    spec = importlib.util.spec_from_file_location("device_fdt", REPO / "scripts/prepare-pong-wifi.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return describe_nodes(module.fdt_nodes(raw))


def build_checker(target):
    command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
               "-Wall", "-Wextra", "-Werror",
               str(REPO / "tools/zizhan/mac/slpi-firmware-check.c"),
               str(REPO / "drivers/PongPower/QcomMdt.c"), "-o", str(target)]
    subprocess.run(command, check=True, timeout=30, capture_output=True)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def private_output(path):
    path = path.resolve()
    if path.is_relative_to(REPO):
        raise ValueError("raw evidence report must stay outside the public repository")
    if path.exists():
        raise ValueError("refusing to overwrite an existing preflight report")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True, help="locally merged slpi.mbn")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = private_output(args.output)
    report = dict(status="FAIL", created=datetime.now(timezone.utc).isoformat(),
                  scope="host-placement-only", authenticated=False, started=False,
                  transport_ready=False, sensor_api_ready=False, phone_operations=False)
    try:
        report["board_inputs"] = describe_dt(args.dtb.read_bytes())
        report["dtb_sha256"] = digest(args.dtb)
        report["firmware"] = dict(path=str(args.firmware.resolve()),
                                  bytes=args.firmware.stat().st_size, sha256=digest(args.firmware))
        report["parser_sources"] = {str(path.relative_to(REPO)): digest(path) for path in (
            REPO / "tools/zizhan/mac/slpi-firmware-check.c",
            REPO / "drivers/PongPower/QcomMdt.c",
            REPO / "drivers/PongPower/QcomMdt.h")}
        with tempfile.TemporaryDirectory(prefix="zizhan-slpi-check-") as temp:
            checker = Path(temp) / "check"
            build_checker(checker)
            result = subprocess.run([str(checker), str(args.firmware.resolve())],
                                    capture_output=True, text=True, timeout=30)
        report["placement"] = json.loads(result.stdout)
        if result.returncode or report["placement"].get("status") != "PASS":
            raise ValueError("firmware placement rejected")
        report["status"] = "HOST_VERIFIED_PLACEMENT"
    except Exception as error:
        report["error"] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    print(report["status"] + ": " + str(output))
    return 0 if report["status"] == "HOST_VERIFIED_PLACEMENT" else 1


if __name__ == "__main__":
    raise SystemExit(main())
