#!/usr/bin/env python3
"""Host regressions for zizhan thermal input decoding; no device required."""
import importlib.util
from pathlib import Path
import struct
import json
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "thermal", Path(__file__).resolve().parents[1] / "mac/thermal-inventory.py")
thermal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(thermal)


def field(path, value):
    return b"OK\0" + path.encode() + b"\0" + value.encode() + b"\0"


def stream(*fields):
    return (b"BEGIN\0" + field("/proc/uptime", "123.00 900.0") +
            b"".join(fields) + b"END\0" + field("/proc/uptime", "126.25 902.0"))


def fixture(model="Zizhan SM8475", sensor=1, address_cells=1, soc_ranges=None):
    """Minimal FDT including signed trips, provider refs and 32/64-bit reg."""
    cell = lambda *v: struct.pack(">" + "I" * len(v), *v)
    reg = lambda address, size: cell(address, size) if address_cells == 1 else cell(1, address, 0, size)
    tree = ("", {"model": model.encode() + b"\0", "compatible": b"qcom,cape\0",
                 "#address-cells": cell(2), "#size-cells": cell(2)}, [
        ("soc", {"#address-cells": cell(address_cells), "#size-cells": cell(address_cells),
                 **({"ranges": cell(*soc_ranges)} if soc_ranges is not None else {})}, [
            ("thermal-sensor@1000", {"compatible": b"qcom,tsens-v2\0",
                                   "phandle": cell(1), "#thermal-sensor-cells": cell(1),
                                   "#qcom,sensors": cell(16),
                                   "reg": reg(0x1000, 0x1ff) + reg(0x1800, 0x1ff)}, []),
            ("cpufreq", {"compatible": b"qcom,cpufreq-hw-epss\0",
                         "reg": reg(0x2000, 0x1000), "reg-names": b"freq-domain0\0"}, []),
            ("thermal-zones", {}, [
                ("cpu0", {"thermal-sensors": cell(sensor, 3)}, [
                    ("trips", {}, [
                        ("cold", {"temperature": cell((-5000) & 0xffffffff),
                                  "hysteresis": cell(1000), "type": b"passive\0"}, [])
                    ])
                ])
            ])
        ])
    ])
    names, name_offsets, body = bytearray(), {}, bytearray()

    def emit(node):
        name, props, children = node
        body.extend(cell(1) + name.encode() + b"\0")
        body.extend(b"\0" * (-len(body) % 4))
        for key, value in props.items():
            if key not in name_offsets:
                name_offsets[key] = len(names)
                names.extend(key.encode() + b"\0")
            body.extend(cell(3, len(value), name_offsets[key]) + value)
            body.extend(b"\0" * (-len(body) % 4))
        for child in children:
            emit(child)
        body.extend(cell(2))
    emit(tree)
    body.extend(cell(9))
    offset = 56
    header = cell(0xd00dfeed, offset + len(body) + len(names), offset,
                  offset + len(body), 40, 17, 16, 0, len(names), len(body))
    return header + b"\0" * 16 + body + names


class ThermalInputTests(unittest.TestCase):
    def test_units_and_signed_zero(self):
        raw = stream(field(thermal.THERMAL + "thermal_zone0/type", "cpu0"),
                     field(thermal.THERMAL + "thermal_zone0/temp", "-1250"),
                     field(thermal.THERMAL + "thermal_zone1/temp", "0"),
                     field(thermal.BATTERY + "temp", "286"))
        report = thermal.summarize_snapshot(thermal.parse_stream(raw))
        self.assertEqual(report["thermal_zones"][0]["temperature"]["value"], -1.25)
        self.assertEqual(report["thermal_zones"][1]["temperature"]["value"], 0)
        self.assertEqual(report["battery"]["temperature"]["value"], 28.6)

    def test_error_missing_invalid_are_distinct(self):
        raw = stream(b"ERROR\0" + (thermal.THERMAL + "thermal_zone0/temp").encode() + b"\0\0",
                     field(thermal.THERMAL + "thermal_zone1/temp", "not-ready"))
        zones = thermal.summarize_snapshot(thermal.parse_stream(raw))["thermal_zones"]
        self.assertEqual(zones[0]["temperature"]["status"], "ERROR")
        self.assertNotIn("value", zones[0]["temperature"])
        self.assertEqual(zones[0]["type"]["status"], "MISSING")
        self.assertEqual(zones[1]["temperature"]["status"], "INVALID")

    def test_incomplete_stream_rejected(self):
        for raw in (b"", b"BEGIN\0", stream()[:-10], stream().replace(b"END\0", b"")):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                thermal.parse_stream(raw)

    def test_duplicate_rejected(self):
        record = field(thermal.BATTERY + "temp", "300")
        with self.assertRaises(ValueError):
            thermal.parse_stream(stream(record, record))

    def test_path_and_encoding_rejected(self):
        for record in (field("/etc/passwd", "x"),
                       field(thermal.BATTERY + "../secret", "x"),
                       b"BAD\0" + (thermal.BATTERY + "temp").encode() + b"\0@@\0"):
            with self.subTest(record=record), self.assertRaises(ValueError):
                thermal.parse_stream(stream(record))

    def test_monotonic_time_required(self):
        raw = stream().replace(b"126.25 902.0", b"122.25 902.0")
        with self.assertRaises(ValueError):
            thermal.parse_stream(raw)

    def test_missing_groups_not_pass(self):
        report = thermal.make_report(thermal.parse_stream(stream()))
        self.assertEqual(report["status"], "SKIP")

    def test_dt_ref_signed_trip_and_resource_width(self):
        for width, address in ((1, "0x1000"), (2, "0x100001000")):
            with self.subTest(width=width):
                report = thermal.describe_dt(fixture(address_cells=width))
                self.assertEqual(report["tsens"][0]["resources"][0]["bus_address"], address)
                self.assertEqual(report["thermal_zones"][0]["sensors"][0]["sensor_ids"], [3])
                self.assertEqual(report["thermal_zones"][0]["trips"][0]["temperature_mc"], [-5000])
                self.assertEqual(report["thermal_zones"][0]["trips"][0]["hysteresis_cells"], [1000])
                self.assertEqual(report["tsens"][0]["status"], [])

    def test_foreign_dt_unresolved_ref_truncation_rejected(self):
        for raw in (fixture(model="Pong SM8475"), fixture(sensor=999), fixture()[:32]):
            with self.subTest(length=len(raw)), self.assertRaises(ValueError):
                thermal.describe_dt(raw)

    def test_identity_translation_must_be_present_and_exact(self):
        for ranges in (None, (0, 0, 0x1000, 0xffffffff), (0, 0, 0, 0xffffffff)):
            with self.subTest(ranges=ranges):
                dt = thermal.describe_dt(fixture(soc_ranges=ranges))
                translated = dt["tsens_address_translation"]
                if ranges == (0, 0, 0, 0xffffffff):
                    self.assertEqual(translated["physical_base"], "0x0")
                    self.assertEqual(thermal.board_profile(dt)["tsens_address_translation"], translated)
                else:
                    self.assertIsNone(translated)

    def test_raw_outputs_cannot_enter_repository(self):
        with self.assertRaises(ValueError):
            thermal.private_path(thermal.REPO / "raw-input.json")

    def test_board_profile_excludes_readings_and_trips(self):
        profile = thermal.board_profile(thermal.describe_dt(fixture()))
        self.assertIsNone(profile["thermal_policy"])
        self.assertNotIn("sha256", profile)
        self.assertNotIn("temperature_mc", json.dumps(profile))
        dt = thermal.describe_dt(fixture())
        dt["thermal_zones"][0]["sensors"][0]["sensor_ids"] = [999]
        with self.assertRaises(ValueError):
            thermal.board_profile(dt)

    def test_capture_forwards_stdin_and_preserves_failed_input(self):
        observed = stream(field(thermal.THERMAL + "thermal_zone0/temp", "30000"),
                          field(thermal.CPUFREQ + "policy0/related_cpus", "0 1 2 3"))

        def adb(_binary, arguments, timeout, **kwargs):
            if arguments == ["get-state"]:
                return b"device\n"
            if "ro.product.device" in arguments:
                return b"zizhan\n"
            if "sys.boot_completed" in arguments:
                return b"1\n"
            if "input" in kwargs:
                self.assertEqual(arguments[:2], ["shell", "-T"])
                self.assertIn(b"read_field", kwargs["input"])
                return observed
            return fixture()

        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "good"
            with patch.object(thermal, "run_adb", side_effect=adb):
                result = thermal.capture(destination, "fake-adb", True, 5)
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(json.loads((destination / "capture.json").read_text())["status"], "PASS")
            failing = Path(directory) / "timeout"

            def timeout_adb(binary, arguments, timeout, **kwargs):
                if "input" in kwargs:
                    raise subprocess.TimeoutExpired("adb", 5, output=b"partial-observation")
                return adb(binary, arguments, timeout, **kwargs)

            with patch.object(thermal, "run_adb", side_effect=timeout_adb), self.assertRaises(subprocess.TimeoutExpired):
                thermal.capture(failing, "fake-adb", True, 5)
            self.assertEqual((failing / "thermal.partial.bin").read_bytes(), b"partial-observation")
            self.assertEqual(json.loads((failing / "capture.json").read_text())["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
