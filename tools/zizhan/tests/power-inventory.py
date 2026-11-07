#!/usr/bin/env python3
"""Replay power inventory with a synthetic FDT; no private capture required."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("power", ROOT / "tools/zizhan/mac/power-inventory.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


def fixture(model="Zizhan synthetic test", cpu_count=8, domain_parent=2):
    """Encode public DT shapes with artificial addresses, names and timings.

    This is parser input, not a captured device tree or a hardware profile.
    Keep TCS ordering and the unresolved bus IRQ case as explicit controls.
    """
    def cells(*values):
        return struct.pack(">" + "I" * len(values), *values)

    states = [
        (f"state{i}", {
            "phandle": cells(100 + i),
            "compatible": b"arm,idle-state\0" if i < 2 else b"domain-idle-state\0",
            "idle-state-name": f"synthetic-state-{i}".encode() + b"\0",
            "arm,psci-suspend-param": cells(i),
            "entry-latency-us": cells(10 + i),
            "exit-latency-us": cells(20 + i),
            "min-residency-us": cells(100 + i),
            **({"local-timer-stop": b""} if i < 2 else {}),
        }, []) for i in range(4)
    ]
    domains = [
        ("shared", {"phandle": cells(2), "#power-domain-cells": cells(0),
                    "domain-idle-states": cells(102, 103)}, []),
        *[(f"cpu{i}", {"phandle": cells(10 + i), "#power-domain-cells": cells(0),
                       "power-domains": cells(domain_parent)}, []) for i in range(8)],
    ]
    cpu_nodes = [
        (f"cpu@{i << 8:x}", {
            "reg": cells(0, i << 8),
            "cpu-idle-states": cells(100 if i < 4 else 101),
            "power-domains": cells(10 + i),
        }, []) for i in range(cpu_count)
    ]
    rscs = [
        (f"rsc@{0x1000 + i * 0x1000:x}", {
            "compatible": b"qcom,rpmh-rsc\0",
            "reg": cells(0x1000 + i * 0x1000, 0x100),
            "reg-names": b"synthetic-rsc\0",
            "qcom,drv-id": cells(i),
            "qcom,tcs-config": cells(2, 3, 0, 2, 1, 2, 3, 0, 4, 1),
        }, []) for i in range(2)
    ]
    tree = ("", {"model": model.encode() + b"\0",
                 "#address-cells": cells(2), "#size-cells": cells(2),
                 "interrupt-parent": cells(1)}, [
        ("intc", {"phandle": cells(1), "#interrupt-cells": cells(3)}, []),
        ("psci", {"compatible": b"arm,psci-1.0\0", "method": b"smc\0"}, []),
        ("cpus", {}, cpu_nodes + [("idle-states", {}, states)]),
        ("domains", {}, domains),
        ("soc", {"#address-cells": cells(1), "#size-cells": cells(1)}, [
            *rscs,
            ("pdc@3000", {"compatible": b"qcom,pdc\0",
                          "reg": cells(0x3000, 0x100),
                          "qcom,pdc-ranges": cells(0, 10, 2)}, []),
            ("spmi", {"#address-cells": cells(1), "#size-cells": cells(0)}, [
                ("rtc", {"compatible": b"qcom,pmk8350-rtc\0",
                         "reg": cells(0x100, 0x200),
                         "reg-names": b"synthetic-rtc\0synthetic-alarm\0",
                         "interrupts": cells(0, 1, 2, 3),
                         "wakeup-source": b""}, []),
            ]),
        ]),
    ])
    names, offsets, body = bytearray(), {}, bytearray()

    def emit(node):
        name, properties, children = node
        body.extend(cells(1) + name.encode() + b"\0")
        body.extend(b"\0" * (-len(body) % 4))
        for name, value in properties.items():
            if name not in offsets:
                offsets[name] = len(names)
                names.extend(name.encode() + b"\0")
            body.extend(cells(3, len(value), offsets[name]) + value)
            body.extend(b"\0" * (-len(body) % 4))
        for child in children:
            emit(child)
        body.extend(cells(2))

    emit(tree)
    body.extend(cells(9))
    start = 56
    header = cells(0xd00dfeed, start + len(body) + len(names), start,
                   start + len(body), 40, 17, 16, 0, len(names), len(body))
    return header + b"\0" * 16 + body + names


class PowerInventoryTests(unittest.TestCase):
    def setUp(self):
        self.profile = power.describe(fixture())

    def test_identity_and_counts(self):
        self.assertEqual(self.profile["format"], "zizhan-sm8475-power-inputs-v1")
        self.assertEqual(len(self.profile["cpus"]), 8)
        self.assertEqual(len(self.profile["idle_states"]), 4)
        self.assertEqual(self.profile["psci"]["method"], "smc")
        self.assertEqual(len(self.profile["rsc"]), 2)
        self.assertEqual(len(self.profile["rtc"]), 1)
        self.assertEqual(len(self.profile["pdc"]), 1)

    def test_no_fake_execution_qualification(self):
        payload = json.dumps(self.profile)
        self.assertIn('"execution_state": "UNQUALIFIED"', payload)
        self.assertIn('"layout_state": "UNQUALIFIED"', payload)
        self.assertIn('"capability_state": "UNQUALIFIED"', payload)
        self.assertIn('"wake_state": "UNQUALIFIED"', payload)
        self.assertIn('"thermal_policy": null', payload)

    def test_cpu_domain_topology_is_explicit(self):
        self.assertEqual({row["affinity"] for row in self.profile["cpus"]},
                         {hex(i << 8) for i in range(8)})
        self.assertTrue(all(cpu["idle_states"] for cpu in self.profile["cpus"]))
        for domain in self.profile["domains"]:
            self.assertLessEqual(len(domain["parents"]), 4)

    def test_tcs_order_and_implicit_rtc_irq_are_preserved(self):
        pairs = self.profile["rsc"][0]["tcs_pairs"]
        self.assertEqual(pairs, [[2, 3], [0, 2], [1, 2], [3, 0], [4, 1]])
        irq = self.profile["rtc"][0]["interrupt"]
        self.assertIn("unresolved", irq["provider"])
        self.assertEqual(irq["scope"], "Provider-specific cells do not match inherited domain; parent/bus resolution required")

    def test_foreign_board_rejected(self):
        with self.assertRaisesRegex(ValueError, "Not a zizhan"):
            power.describe(fixture(model="Foreign synthetic test"))

    def test_missing_cpu_rejected(self):
        with self.assertRaisesRegex(ValueError, "eight distinct"):
            power.describe(fixture(cpu_count=7))

    def test_cyclic_domains_rejected(self):
        with self.assertRaisesRegex(ValueError, "Cyclic power-domain"):
            power.describe(fixture(domain_parent=10))


if __name__ == "__main__":
    unittest.main()
