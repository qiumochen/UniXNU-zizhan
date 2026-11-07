#!/usr/bin/env python3
"""Encode reviewed zizhan physical TSENS resources; never infer a thermal policy."""
import struct


def encode(profile):
    if (profile.get("format") != "zizhan-sm8475-thermal-inputs-v1" or
            profile.get("board") != "zizhan" or profile.get("soc") != "SM8475"):
        raise ValueError("Expected zizhan SM8475 thermal board inputs")
    translation = profile.get("tsens_address_translation", {})
    if translation != {
        "bus": "/soc",
        "child_base": "0x0",
        "physical_base": "0x0",
        "bytes": 0xffffffff,
        "source": "live /soc/ranges: 1 child address + 2 parent address + 1 size cells",
    }:
        raise ValueError("TSENS physical address translation is not reviewed")
    banks = profile.get("tsens", [])
    if len(banks) != 2:
        raise ValueError("Expected two TSENS banks")
    cells, ranges = [1, 2], []
    for bank in banks:
        if bank["compatible"] != ["qcom,tsens-v2"] or bank["declared_sensor_count"] != 16:
            raise ValueError("Unsupported TSENS provider")
        if len(bank["regions"]) != 2:
            raise ValueError("Expected TM then SROT regions")
        row = []
        for region, needed in zip(bank["regions"], (0xe8, 8)):
            base, size = int(region["bus_address"], 0), region["bytes"]
            if not (base > 0 and base % 4 == 0 and needed <= size <= 0x1000 and
                    base + size <= translation["bytes"]):
                raise ValueError("Invalid TSENS region")
            if any(base < prior + length and prior < base + size for prior, length in ranges):
                raise ValueError("Overlapping TSENS resources")
            ranges.append((base, size))
            row.extend((base, size))
        cells.extend((*row, 0xffff))
    return struct.pack("<12Q", *cells)
