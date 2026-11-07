#!/usr/bin/env python3
"""zizhan THERM-01: bounded Android read-only capture and offline DT inventory.

No MMIO, thermal policy writes, stress, image installation or device reboot.
Raw observations must stay outside the public repository.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[3]
THERMAL = "/sys/class/thermal/"
CPUFREQ = "/sys/devices/system/cpu/cpufreq/"
BATTERY = "/sys/class/power_supply/battery/"


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def parse_stream(raw):
    """Preserve failed reads and require a complete bounded capture."""
    tokens = raw.decode("utf-8").split("\0")
    if tokens[-1] != "" or tokens[0] != "BEGIN":
        raise ValueError("Incomplete thermal stream")
    tokens.pop()
    # BEGIN, records of three fields, END, final uptime record.
    if len(tokens) < 8 or tokens[-4] != "END" or (len(tokens) - 5) % 3:
        raise ValueError("Incomplete thermal stream")
    records = [tokens[i:i + 3] for i in range(1, len(tokens) - 4, 3)]
    records.append(tokens[-3:])
    if records[0][1] != "/proc/uptime" or records[-1][1] != "/proc/uptime":
        raise ValueError("Missing final uptime")
    fields, uptime = {}, []
    for index, parts in enumerate(records):
        if parts[0] not in ("OK", "ERROR"):
            raise ValueError("Invalid thermal field record")
        status, path, raw_value = parts
        if path != "/proc/uptime" and not path.startswith((THERMAL, CPUFREQ, BATTERY)):
            raise ValueError("Unexpected observation path")
        if ".." in Path(path).parts:
            raise ValueError("Invalid observation path")
        value = raw_value if status == "OK" else None
        record = dict(status=status, raw=value)
        if path == "/proc/uptime":
            if index not in (0, len(records) - 1) or status != "OK":
                raise ValueError("Missing observation time")
            uptime.append(float(value.split()[0]))
        else:
            if path in fields:
                raise ValueError("Duplicate thermal field")
            fields[path] = record
    if len(uptime) != 2 or not 0 <= uptime[0] <= uptime[1]:
        raise ValueError("Invalid monotonic observation interval")
    return dict(fields=fields, uptime_start=uptime[0], uptime_end=uptime[1],
                duration_seconds=round(uptime[1] - uptime[0], 3))


def integer_field(fields, path, unit, divisor=1):
    result = dict(fields.get(path, dict(status="MISSING", raw=None)), unit=unit)
    if result["status"] == "OK":
        try:
            result["value"] = int(result["raw"]) / divisor
        except (TypeError, ValueError):
            result["status"] = "INVALID"
    return result


def summarize_snapshot(snapshot):
    fields = snapshot["fields"]
    directories = sorted({str(Path(path).parent) for path in fields})
    zones, cooling, policies = [], [], []
    for directory in directories:
        get = lambda name: fields.get(directory + "/" + name, dict(status="MISSING", raw=None))
        if re.fullmatch(THERMAL + r"thermal_zone\d+", directory):
            zones.append(dict(path=directory, type=get("type"),
                              temperature=integer_field(fields, directory + "/temp", "C", 1000),
                              mode=get("mode"), policy=get("policy"),
                              trips={Path(k).name: v for k, v in fields.items()
                                     if str(Path(k).parent) == directory and "/trip_point_" in k}))
        elif re.fullmatch(THERMAL + r"cooling_device\d+", directory):
            cooling.append(dict(path=directory, type=get("type"),
                                current=integer_field(fields, directory + "/cur_state", "state"),
                                maximum=integer_field(fields, directory + "/max_state", "state")))
        elif re.fullmatch(CPUFREQ + r"policy\d+", directory):
            policies.append(dict(path=directory, related_cpus=get("related_cpus"),
                                 governor=get("scaling_governor"),
                                 fields={Path(k).name: v for k, v in fields.items()
                                         if str(Path(k).parent) == directory}))
    battery = {Path(k).name: v for k, v in fields.items() if k.startswith(BATTERY)}
    battery["temperature"] = integer_field(fields, BATTERY + "temp", "C", 10)
    return dict(thermal_zones=zones, cooling_devices=cooling, cpufreq_policies=policies,
                battery=battery,
                qualification="Android observations only; no XNU thermal driver or control qualified",
                read_errors=sum(v["status"] != "OK" for v in fields.values()),
                temperature_scope="Linux thermal sysfs mC; battery temp deciC; keep raw values",
                timing="Sequential reads within the recorded interval, not simultaneous samples")


def u32(props, key):
    raw = props.get(key, b"")
    if len(raw) % 4:
        raise ValueError("Unaligned DT property: " + key)
    return list(struct.unpack(">" + "I" * (len(raw) // 4), raw))


def strings(props, key):
    raw = props.get(key, b"")
    return raw.rstrip(b"\0").decode("ascii").split("\0") if raw else []


def describe_dt(raw):
    # Reuse the maintained device-repository FDT reader; no dtc dependency.
    spec = importlib.util.spec_from_file_location("zizhan_wifi_inputs", REPO / "scripts/prepare-pong-wifi.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    nodes = module.fdt_nodes(raw)
    compatible = strings(nodes["/"], "compatible")
    model = strings(nodes["/"], "model")
    if not any("zizhan" in value.lower() for value in compatible + model):
        raise ValueError("DT root does not identify zizhan")
    refs = {u32(props, "phandle")[0]: path for path, props in nodes.items() if "phandle" in props}

    def resources(path, props):
        parent = nodes[path.rsplit("/", 1)[0] or "/"]
        ac = u32(parent, "#address-cells") or [2]
        sc = u32(parent, "#size-cells") or [1]
        if ac[0] not in (1, 2) or sc[0] not in (1, 2):
            raise ValueError("Unsupported resource cell layout")
        cells = u32(props, "reg")
        width = ac[0] + sc[0]
        if len(cells) % width:
            raise ValueError("Invalid DT register resources")
        result = []
        for i in range(0, len(cells), width):
            address = size = 0
            for cell in cells[i:i + ac[0]]:
                address = (address << 32) | cell
            for cell in cells[i + ac[0]:i + width]:
                size = (size << 32) | cell
            result.append(dict(bus_address=hex(address), bytes=size))
        return result

    providers, zones, domains = [], [], []
    for path, props in nodes.items():
        comp = strings(props, "compatible")
        if "qcom,tsens-v2" in comp:
            providers.append(dict(path=path, compatible=comp, status=strings(props, "status"),
                                  resources=resources(path, props),
                                  reg_names=strings(props, "reg-names"),
                                  sensors=u32(props, "#qcom,sensors"),
                                  sensor_cells=u32(props, "#thermal-sensor-cells"),
                                  interrupts=u32(props, "interrupts"),
                                  interrupt_names=strings(props, "interrupt-names")))
        if "qcom,cpufreq-hw-epss" in comp:
            domains.append(dict(path=path, compatible=comp, resources=resources(path, props),
                                names=strings(props, "reg-names"),
                                lut_row_bytes=u32(props, "qcom,lut-row-size"),
                                perf_lock="qcom,perf-lock-support" in props))
        if "/thermal-zones/" not in path or "thermal-sensors" not in props:
            continue
        cells = u32(props, "thermal-sensors")
        sensors = []
        while cells:
            handle = cells.pop(0)
            provider = refs.get(handle)
            if provider is None:
                raise ValueError("Unresolved thermal sensor phandle")
            count = u32(nodes[provider], "#thermal-sensor-cells")
            if len(count) != 1 or len(cells) < count[0]:
                raise ValueError("Invalid thermal sensor specifier")
            sensors.append(dict(provider=provider, sensor_ids=cells[:count[0]]))
            del cells[:count[0]]
        trips = []
        for trip_path, trip in nodes.items():
            if trip_path.startswith(path + "/trips/") and "temperature" in trip:
                values = u32(trip, "temperature")
                # DT temperatures are signed int32 millidegrees.
                temperatures = [v - (1 << 32) if v & (1 << 31) else v for v in values]
                trips.append(dict(path=trip_path, type=strings(trip, "type"),
                                  temperature_mc=temperatures,
                                  hysteresis_cells=u32(trip, "hysteresis")))
        zones.append(dict(path=path, sensors=sensors, trips=trips,
                          polling_ms=u32(props, "polling-delay"),
                          passive_polling_ms=u32(props, "polling-delay-passive")))
    if not providers or not domains:
        raise ValueError("Missing zizhan TSENS/EPSS input")
    # Only claim the translation we actually decoded; missing/non-identity
    # ranges remain unusable for the XNU observer's physical input contract.
    translation = None
    soc = nodes.get("/soc", {})
    if (u32(soc, "#address-cells") == [1] and u32(soc, "#size-cells") == [1] and
            u32(nodes["/"], "#address-cells") == [2] and
            u32(soc, "ranges") == [0, 0, 0, 0xffffffff] and
            all(path.rsplit("/", 1)[0] == "/soc" for path in (row["path"] for row in providers))):
        translation = dict(bus="/soc", child_base="0x0", physical_base="0x0", bytes=0xffffffff,
                           source="live /soc/ranges: 1 child address + 2 parent address + 1 size cells")
    return dict(sha256=hashlib.sha256(raw).hexdigest(), root_compatible=compatible, model=model,
                tsens=providers, epss=domains, thermal_zones=zones,
                tsens_address_translation=translation,
                register_scope="DT bus addresses; no MMIO access or address translation performed",
                trip_scope="DT declarations only; not an adopted XNU policy or measured thermal response")


def board_profile(device_tree):
    """Only hardware resources; omit runtime readings, raw DT and private paths."""
    providers = {}
    for row in device_tree["tsens"]:
        counts = row["sensors"]
        if len(counts) != 1 or not 0 < counts[0] <= 32 or len(row["resources"]) != 2:
            raise ValueError("Unsupported TSENS board resource shape")
        providers[row["path"]] = dict(path=row["path"], compatible=row["compatible"],
                                      regions=row["resources"], declared_sensor_count=counts[0],
                                      interrupt_specifiers=row["interrupts"],
                                      interrupt_names=row["interrupt_names"])
    zones = []
    for row in device_tree["thermal_zones"]:
        sensors = [sensor for sensor in row["sensors"] if sensor["provider"] in providers]
        if not sensors:
            continue
        for sensor in sensors:
            ids = sensor["sensor_ids"]
            count = providers[sensor["provider"]]["declared_sensor_count"]
            if len(ids) != 1 or not 0 <= ids[0] < count:
                raise ValueError("TSENS zone references an invalid sensor")
        zones.append(dict(path=row["path"], sensors=sensors))
    return dict(format="zizhan-sm8475-thermal-inputs-v1", board="zizhan", soc="SM8475",
                source="zizhan Android /sys/firmware/fdt; see thermal development record",
                scope="Board inputs only; no XNU MMIO or thermal control qualification",
                tsens_address_translation=device_tree.get("tsens_address_translation"),
                tsens=list(providers.values()), epss=device_tree["epss"], zones=zones,
                thermal_policy=None)


def run_adb(adb, arguments, timeout, **kwargs):
    process = subprocess.run([adb, *arguments], capture_output=True, timeout=timeout, **kwargs)
    if process.returncode:
        raise RuntimeError("adb command failed: " + " ".join(arguments[:2]))
    return process.stdout


def private_path(path):
    path = path.resolve()
    if path == REPO or REPO in path.parents:
        raise ValueError("Raw observations must stay outside the public repository")
    return path


def make_report(snapshot):
    summary = summarize_snapshot(snapshot)
    result = dict(schema_version=1, status="PASS", snapshot=summary,
                  observation_interval={key: snapshot[key] for key in
                                        ("uptime_start", "uptime_end", "duration_seconds")})
    if not summary["thermal_zones"] or not summary["cpufreq_policies"]:
        result.update(status="SKIP", reason="Missing thermal zones or CPU policies")
    return result


def capture(directory, adb, root, timeout):
    directory = private_path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    started, began = time.monotonic(), utc_now()
    meta = dict(status="RUNNING", started=began, commands="read-only Android sysfs/FDT")
    (directory / "capture.json").write_text(json.dumps(meta, indent=2) + "\n")
    try:
        if run_adb(adb, ["get-state"], 8).strip() != b"device":
            raise RuntimeError("Android device not ready")
        if run_adb(adb, ["shell", "getprop", "ro.product.device"], 8).strip() != b"zizhan":
            raise RuntimeError("Android product is not zizhan")
        if run_adb(adb, ["shell", "getprop", "sys.boot_completed"], 8).strip() != b"1":
            raise RuntimeError("Android boot incomplete")
        # exec-out is for output-only commands: use non-PTY shell so adb
        # actually forwards the script's stdin and preserves NUL framing.
        command = ["shell", "-T", "su -c 'sh -s'"] if root else ["shell", "-T", "sh", "-s"]
        try:
            data = run_adb(adb, command, timeout,
                           input=(REPO / "tools/zizhan/phone/thermal-readonly.sh").read_bytes())
        except subprocess.TimeoutExpired as error:
            (directory / "thermal.partial.bin").write_bytes(error.stdout or b"")
            raise
        (directory / "thermal.bin").write_bytes(data)
        snapshot = parse_stream(data)
        (directory / "snapshot.json").write_text(json.dumps(snapshot, indent=2) + "\n")
        dtb = run_adb(adb, ["exec-out", "su -c 'cat /sys/firmware/fdt'"] if root else
                      ["exec-out", "cat", "/sys/firmware/fdt"], 15)
        (directory / "live.dtb").write_bytes(dtb)
        report = make_report(snapshot)
        report.update(capture_started=began, device_tree=describe_dt(dtb))
        (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        meta["status"] = report["status"]
        return report
    except Exception as error:
        meta.update(status="FAIL", reason=str(error))
        raise
    finally:
        meta.update(finished=utc_now(), elapsed_seconds=round(time.monotonic() - started, 3))
        (directory / "capture.json").write_text(json.dumps(meta, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--collect", type=Path, help="New private capture directory")
    mode.add_argument("--snapshot", type=Path, help="Offline snapshot.json")
    parser.add_argument("--dtb", type=Path, help="Offline live FDT for --snapshot")
    parser.add_argument("--output", type=Path, help="Offline JSON output")
    parser.add_argument("--profile-output", type=Path, help="Sanitized hardware-only board JSON")
    parser.add_argument("--root", action="store_true", help="Use existing Android su for reads")
    parser.add_argument("--adb", default=os.environ.get("ADB_BIN", "adb"))
    parser.add_argument("--timeout", type=int, default=60, help="Bound for the sysfs read command")
    args = parser.parse_args()
    if args.collect and (args.dtb or args.output):
        parser.error("--collect writes its own live DT/report; --dtb and --output are offline options")
    if not 1 <= args.timeout <= 60:
        parser.error("--timeout must be 1..60 seconds")
    try:
        if args.collect:
            report = capture(args.collect, args.adb, args.root, args.timeout)
        else:
            report = make_report(json.loads(args.snapshot.read_text()))
            if args.dtb:
                report["device_tree"] = describe_dt(args.dtb.read_bytes())
            if args.output:
                private_path(args.output).write_text(json.dumps(report, indent=2) + "\n")
        if args.profile_output:
            if "device_tree" not in report:
                raise ValueError("--profile-output requires a collected or offline --dtb input")
            args.profile_output.write_text(json.dumps(board_profile(report["device_tree"]), indent=2) + "\n")
        summary = report["snapshot"]
        print(json.dumps(dict(status=report["status"], zones=len(summary["thermal_zones"]),
                              cooling_devices=len(summary["cooling_devices"]),
                              cpu_policies=len(summary["cpufreq_policies"]),
                              read_errors=summary["read_errors"])))
        return 0 if report["status"] == "PASS" else 2
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
