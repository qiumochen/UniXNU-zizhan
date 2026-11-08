#!/usr/bin/env python3
"""Inspect zizhan L18 off/resume DT programs offline; never sends commands."""
import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PANEL = "qcom,mdss_dsi_l18_38_0c_0a_prim_dsc_cmd"


def address_domain(nodes):
    """Preserve the real shared display aspace; do not infer its live map."""
    def cells(props, key):
        raw = props.get(key, b"")
        if len(raw) % 4:
            raise ValueError("Unaligned display address-domain cells: " + key)
        return list(struct.unpack(">" + "I" * (len(raw) // 4), raw))

    domains = [path for path, p in nodes.items()
               if b"qcom,smmu_sde_unsec" in p.get("compatible", b"").split(b"\0")]
    if len(domains) != 1:
        raise ValueError("Expected one nonsecure display address domain")
    path = domains[0]
    domain = nodes[path]
    iommu = cells(domain, "iommus")
    if len(iommu) != 3:
        raise ValueError("Expected one display IOMMU with two argument cells")
    providers = [name for name, p in nodes.items() if cells(p, "phandle") == iommu[:1]]
    if len(providers) != 1:
        raise ValueError("Missing or duplicate display IOMMU provider")
    provider_path = providers[0]
    provider = nodes[provider_path]
    if cells(provider, "#iommu-cells") != [2] or not (
            b"qcom,qsmmu-v500" in provider.get("compatible", b"").split(b"\0")):
        raise ValueError("Unsupported display IOMMU provider")
    sid, mask = iommu[1:]
    if sid > 0x7fff or mask > 0x7fff:
        raise ValueError("Display stream ID/mask outside SMMUv2 legacy width")
    handoff = cells(provider, "qcom,handoff-smrs")
    if len(handoff) % 2:
        raise ValueError("Unpaired handoff SMR cells")
    pairs = [handoff[i:i + 2] for i in range(0, len(handoff), 2)]
    pool = cells(domain, "qcom,iommu-dma-addr-pool")
    if len(pool) != 2 or not pool[1] or sum(pool) > 1 << 32:
        raise ValueError("Invalid display IOVA allocation pool")
    return dict(domain_node=path, provider_node=provider_path,
                sid=hex(sid), mask=hex(mask), provider_reg_cells=cells(provider, "reg"),
                iova_pool=dict(base=hex(pool[0]), bytes=pool[1]),
                firmware_handoff_match=[sid, mask] in pairs,
                handoff_pairs=pairs, stream_group_index=None, context_bank=None,
                translation_enabled=None, dma_address_mode="UNQUALIFIED",
                shared_owner="Display nonsecure aspace; DSI usage corroborated by vendor dsi_display.c",
                note="DT routing declaration does not prove current SMR/S2CR/CB state or direct physical DMA safety")


def decode(raw):
    if not raw or len(raw) > 4096:
        raise ValueError("Invalid program size")
    offset, packets = 0, []
    while offset < len(raw):
        if len(raw) - offset < 7:
            raise ValueError("Truncated command header")
        kind, last, channel, flags, wait, hi, lo = raw[offset:offset + 7]
        length = hi * 256 + lo
        if not 0 < length <= 252 or length > len(raw) - offset - 7:
            raise ValueError("Invalid command length")
        if last > 1 or channel or flags & ~0x40:
            raise ValueError("Unsupported channel/header flags")
        payload = raw[offset + 7:offset + 7 + length]
        if kind not in (0x05, 0x15, 0x39):
            raise ValueError("Unsupported command type")
        if (kind == 0x05 and (length > 2 or length == 2 and payload[1])) or (
                kind == 0x15 and length != 2):
            raise ValueError("Invalid short command payload")
        wire = 4 + ((length + 3) // 4 * 4 if kind == 0x39 else 0)
        packets.append(dict(type=kind, channel=channel, flags=flags, wait_ms=wait,
                            command=payload[0], payload_bytes=length,
                            wire_bytes=wire, transport="FIFO" if wire <= 64 else "DMA",
                            payload_sha256=hashlib.sha256(payload).hexdigest()))
        if len(packets) > 128:
            raise ValueError("Packet limit exceeded")
        offset += 7 + length
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                packets=packets, count=len(packets),
                wait_ms=sum(p["wait_ms"] for p in packets),
                dma_packets=sum(p["transport"] == "DMA" for p in packets),
                maximum_payload=max(p["payload_bytes"] for p in packets))


def describe(nodes, dtb_sha256):
    identity = (nodes.get("/", {}).get("model", b"") + b"\0" +
                nodes.get("/", {}).get("compatible", b""))
    if b"zizhan" not in identity.lower():
        raise ValueError("Not a zizhan live DT")
    paths = [p for p in nodes if p.rsplit("/", 1)[-1] == PANEL]
    if len(paths) != 1:
        raise ValueError("Expected one L18 primary panel")
    panel = paths[0]
    profiles = []
    for timing in ("timing@0", "timing@1"):
        path = panel + "/qcom,mdss-dsi-display-timings/" + timing
        props = nodes.get(path, {})
        programs = {}
        for name in ("off", "on"):
            key = f"qcom,mdss-dsi-{name}-command"
            if props.get(key + "-state") != b"dsi_lp_mode\0":
                raise ValueError("Unexpected command link mode")
            programs[name] = decode(props.get(key, b""))
        profiles.append(dict(timing=timing, source_node=path, programs=programs,
                             fifo_only_admissible=not any(
                                 p["dma_packets"] for p in programs.values()),
                             hardware_state="UNQUALIFIED"))
    return dict(format="zizhan-l18-power-programs-v2", dtb_sha256=dtb_sha256,
                panel=PANEL, fifo_store_bytes=64, profiles=profiles,
                address_domain=address_domain(nodes),
                qualification="OFFLINE_ONLY",
                unresolved=["long-command DMA mapping and ownership",
                            "full DPU/DSI/PHY/clock off/resume",
                            "power request completion notification and first frame",
                            "manual first panel-sleep qualification"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location(
        "zizhan_fdt", ROOT / "scripts/prepare-pong-wifi.py")
    fdt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fdt)
    raw = args.dtb.read_bytes()
    result = describe(fdt.fdt_nodes(raw), hashlib.sha256(raw).hexdigest())
    with args.output.open("x") as out:
        json.dump(result, out, indent=2)
        out.write("\n")
    print("PASS: offline panel program inventory; hardware transition UNQUALIFIED")


if __name__ == "__main__":
    main()
