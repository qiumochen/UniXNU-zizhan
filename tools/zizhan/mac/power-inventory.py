#!/usr/bin/env python3
"""Offline zizhan PSCI/domain/wake inventory; no MMIO, ADB or policy writes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def describe(raw):
    spec = importlib.util.spec_from_file_location("zizhan_fdt", ROOT / "scripts/prepare-pong-wifi.py")
    fdt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fdt)
    nodes = fdt.fdt_nodes(raw)

    def cells(p, key):
        value = p.get(key, b"")
        if len(value) % 4:
            raise ValueError("Unaligned DT cells: " + key)
        return list(struct.unpack(">" + "I" * (len(value) // 4), value))

    def strings(p, key):
        value = p.get(key, b"")
        if value and not value.endswith(b"\0"):
            raise ValueError("Unterminated DT string: " + key)
        return value.rstrip(b"\0").decode("ascii").split("\0") if value else []

    identity = strings(nodes["/"], "model") + strings(nodes["/"], "compatible")
    if not any("zizhan" in x.lower() for x in identity):
        raise ValueError("Not a zizhan live DT")
    refs = {}
    for path, p in nodes.items():
        if "phandle" in p:
            value = cells(p, "phandle")
            if len(value) != 1 or value[0] in refs or not value[0]:
                raise ValueError("Invalid or duplicate phandle")
            refs[value[0]] = path

    def parent(path):
        return path.rsplit("/", 1)[0] or "/"

    def one(p, key):
        value = cells(p, key)
        if len(value) != 1:
            raise ValueError("Expected one DT cell: " + key)
        return value[0]

    def handles(p, key):
        values = cells(p, key)
        if any(value not in refs for value in values):
            raise ValueError("Unresolved reference: " + key)
        return [refs[value] for value in values]

    def domains(p):
        values = cells(p, "power-domains")
        result = []
        while values:
            handle = values.pop(0)
            if handle not in refs:
                raise ValueError("Unresolved power-domain")
            path = refs[handle]
            count = one(nodes[path], "#power-domain-cells")
            if count > 4 or len(values) < count:
                raise ValueError("Invalid power-domain specifier")
            result.append(dict(provider=path, arguments=values[:count]))
            del values[:count]
        return result

    def irq(path):
        original = nodes[path]
        if "interrupts" not in original:
            return None
        current = path
        while "interrupt-parent" not in nodes[current] and current != "/":
            current = parent(current)
        if "interrupt-parent" not in nodes[current]:
            # Some SPMI child interrupts inherit an implicit bus IRQ domain;
            # retain the cells without pretending they are GIC SPI numbers.
            return dict(provider="implicit-parent-unresolved", cells=None,
                        specifiers=cells(original, "interrupts"),
                        scope="Provider-specific; parent resolution required")
        value = handles(nodes[current], "interrupt-parent")
        if len(value) != 1:
            raise ValueError("Missing IRQ domain")
        provider = value[0]
        count = one(nodes[provider], "#interrupt-cells")
        values = cells(original, "interrupts")
        if not count or count > 4 or len(values) % count:
            return dict(provider="implicit-parent-or-bus-unresolved", cells=None,
                        specifiers=values,
                        scope="Provider-specific cells do not match inherited domain; parent/bus resolution required")
        return dict(provider=provider, cells=count, specifiers=values,
                    scope="Provider-specific; not a GIC SPI number")

    def regions(path):
        p, bus = nodes[path], nodes[parent(path)]
        ac, sc = one(bus, "#address-cells"), one(bus, "#size-cells")
        if ac not in (1, 2) or sc not in (0, 1, 2):
            raise ValueError("Unsupported register address shape")
        values, width = cells(p, "reg"), ac + sc
        if len(values) % width:
            raise ValueError("Malformed register regions")
        rows = []
        for i in range(0, len(values), width):
            address = int.from_bytes(struct.pack(">" + "I"*ac, *values[i:i+ac]), "big")
            size = int.from_bytes(struct.pack(">" + "I"*sc, *values[i+ac:i+width]), "big") if sc else None
            rows.append(dict(bus_address=hex(address), bytes=size))
        return rows

    states = {}
    for path, p in nodes.items():
        compatible = strings(p, "compatible")
        if not set(compatible) & {"arm,idle-state", "domain-idle-state"}:
            continue
        states[path] = dict(path=path, compatible=compatible, names=strings(p, "idle-state-name"),
                            psci_parameter=hex(one(p, "arm,psci-suspend-param")),
                            entry_us=one(p, "entry-latency-us"),
                            exit_us=one(p, "exit-latency-us"),
                            minimum_residency_us=one(p, "min-residency-us"),
                            local_timer_stop="local-timer-stop" in p,
                            execution_state="UNQUALIFIED")

    domain_rows = {}

    def visit(path, stack=()):
        if path in stack or len(stack) >= 32:
            raise ValueError("Cyclic power-domain topology")
        if path in domain_rows:
            return
        p = nodes[path]
        idle = handles(p, "domain-idle-states")
        if any(name not in states for name in idle):
            raise ValueError("Domain references non-idle-state")
        parents = domains(p)
        for row in parents:
            visit(row["provider"], stack + (path,))
        domain_rows[path] = dict(path=path, parents=parents, idle_states=idle)

    cpus = []
    for path, p in nodes.items():
        if parent(path) != "/cpus" or not path.startswith("/cpus/cpu@"):
            continue
        reg = p.get("reg", b"")
        if len(reg) not in (4, 8):
            raise ValueError("Invalid CPU affinity width")
        idle, providers = handles(p, "cpu-idle-states"), domains(p)
        if not idle or not providers or any(x not in states for x in idle):
            raise ValueError("CPU lacks explicit idle/domain inputs")
        for row in providers:
            visit(row["provider"])
        cpus.append(dict(path=path, affinity=hex(int.from_bytes(reg, "big")),
                         idle_states=idle, power_domains=providers))
    if len(cpus) != 8 or len({p["affinity"] for p in cpus}) != 8:
        raise ValueError("Expected eight distinct zizhan CPU affinities")

    psci, rscs, rtcs, pdcs, wake = [], [], [], [], []
    for path, p in nodes.items():
        comp = strings(p, "compatible")
        if any(value in comp for value in ("arm,psci-1.0", "arm,psci-0.2", "arm,psci")):
            method = strings(p, "method")
            if method not in (["smc"], ["hvc"]):
                raise ValueError("Missing PSCI conduit")
            psci.append(dict(path=path, compatible=comp, method=method[0],
                             capability_state="UNQUALIFIED"))
        if "qcom,rpmh-rsc" in comp:
            config = cells(p, "qcom,tcs-config")
            if not config or len(config) % 2:
                raise ValueError("Invalid TCS pairs")
            pairs = [config[i:i+2] for i in range(0, len(config), 2)]
            if len({pair[0] for pair in pairs}) != len(pairs) or any(pair[1] > 64 for pair in pairs):
                raise ValueError("Duplicate or unbounded TCS declaration")
            rscs.append(dict(path=path, regions=regions(path), names=strings(p, "reg-names"),
                             drv_id=one(p, "qcom,drv-id"), tcs_pairs=pairs,
                             layout_state="UNQUALIFIED", slot_assignment=None))
        if "qcom,pmk8350-rtc" in comp:
            rtcs.append(dict(path=path, regions=regions(path), names=strings(p, "reg-names"),
                             address_space="SPMI peripheral offsets, not MMIO",
                             interrupt=irq(path), wake_state="UNQUALIFIED"))
        if "qcom,pdc" in comp:
            ranges = cells(p, "qcom,pdc-ranges")
            if not ranges or len(ranges) % 3:
                raise ValueError("Invalid PDC mapping declaration")
            pdcs.append(dict(path=path, regions=regions(path), pin_parent_count=ranges,
                             wake_state="UNQUALIFIED"))
        if "wakeup-source" in p:
            wake.append(dict(path=path, compatible=comp, interrupt=irq(path),
                             scope="DT wake declaration, not proven target-state wake"))
    if len(psci) != 1 or not rscs or not rtcs or not pdcs:
        raise ValueError("Missing board PSCI/RSC/RTC/PDC input")
    return dict(format="zizhan-sm8475-power-inputs-v1", board="zizhan", soc="SM8475",
                source="zizhan live FDT; power implementation record",
                scope="Declared resources only; no register writes or sleep/wake qualification",
                psci=psci[0], cpus=cpus, domains=list(domain_rows.values()),
                idle_states=list(states.values()), rsc=rscs, rtc=rtcs, pdc=pdcs,
                wake_declarations=wake, thermal_policy=None,
                topology_scope="Physical power domains; not XNU scheduling or EPSS domains")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dtb", required=True, type=Path)
    parser.add_argument("--profile-output", required=True, type=Path)
    args = parser.parse_args()
    if args.profile_output.exists():
        parser.error("existing profile retained; select a new path and review differences")
    raw = args.dtb.read_bytes()
    profile = describe(raw)
    args.profile_output.write_text(json.dumps(profile, indent=2) + "\n")
    print(json.dumps(dict(status="PASS", scope="offline declaration inventory",
                         dtb_sha256=hashlib.sha256(raw).hexdigest(),
                         cpus=len(profile["cpus"]), domains=len(profile["domains"]),
                         states=len(profile["idle_states"]), rsc=len(profile["rsc"]))))


if __name__ == "__main__":
    main()
