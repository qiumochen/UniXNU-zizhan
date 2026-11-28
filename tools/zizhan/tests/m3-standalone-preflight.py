#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Unit tests for tools/zizhan/mac/check-m3-standalone.py (offline, zero hardware)."""
import gzip
import hashlib
import importlib.util
import json
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOD_PATH = ROOT / "tools/zizhan/mac/check-m3-standalone.py"
spec = importlib.util.spec_from_file_location("check_m3", MOD_PATH)
check_m3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_m3)

PART = check_m3.PARTITION_BYTES
PAGE = check_m3.PAGE_BYTES


def make_fixture(tmp: Path) -> tuple[Path, Path, Path]:
    # Build a valid synthetic 192 MiB M3 A' image + manifest + backup boot_b.img.gz using sparse/seek writes where possible
    img = tmp / "pong-xnu-aprime.img"
    stock_sha = "754e6bd04ab9f7a24cd26b4b9aa5c974ff6333156c98aa30317d61a2000000"

    fat_payload = (b'version = 1\n[boot]\ndefault = "android"\npath = "DarwinBoot24.efi"\n').ljust(8192, b"\x00")
    fat_crc = zlib.crc32(fat_payload) & 0xFFFFFFFF
    pf_hdr = bytearray(4096)
    pf_hdr[:8] = b"PONGFAT1"
    struct.pack_into("<QI", pf_hdr, 16, len(fat_payload), fat_crc)
    pongfat = bytes(pf_hdr) + fat_payload

    symbols = {
        "_Head": 0, "_StackSize": 8, "_AndroidOffset": 16,
        "_PongFatOffset": 24, "_PongFatSize": 32, "_FdOffset": 40, "_Payload": 64,
    }
    pongfat_off = 4032  # 64 + 4032 = 4096
    fd_off = pongfat_off + len(pongfat)
    android_off = 0x400000
    k_field_bytes = android_off + 4096
    ramdisk_bytes = 8192
    ramdisk_off = PAGE + k_field_bytes
    sig_off = ramdisk_off + ramdisk_bytes
    vbmeta_off = sig_off + PAGE
    orig_sz = sig_off + PAGE

    shim = bytearray(64)
    struct.pack_into("<Q", shim, symbols["_StackSize"], k_field_bytes)
    struct.pack_into("<Q", shim, symbols["_AndroidOffset"], android_off)
    struct.pack_into("<Q", shim, symbols["_PongFatOffset"], pongfat_off)
    struct.pack_into("<Q", shim, symbols["_PongFatSize"], len(pongfat))
    struct.pack_into("<Q", shim, symbols["_FdOffset"], fd_off)

    hdr = bytearray(PAGE)
    hdr[:8] = b"ANDROID!"
    struct.pack_into("<2I", hdr, 8, k_field_bytes, ramdisk_bytes)
    struct.pack_into("<I", hdr, 20, 1584)
    struct.pack_into("<I", hdr, 40, 4)

    and_hdr = bytearray(64)
    and_hdr[:2] = b"MZ"
    and_hdr[56:60] = b"ARMd"

    footer = struct.pack(">4sIIQQQ28s", b"AVBf", 1, 0, orig_sz, vbmeta_off, 0x380, b"")

    with open(img, "wb") as f:
        f.truncate(PART)
        f.seek(0)
        f.write(hdr)
        f.seek(PAGE)
        f.write(shim)
        f.seek(PAGE + symbols["_Payload"] + pongfat_off)
        f.write(pongfat)
        f.seek(PAGE + android_off)
        f.write(and_hdr)
        f.seek(PART - 64)
        f.write(footer)

    img_sha = check_m3.sha256_file(img)
    manifest = {
        "format": "zizhan-m3-aprime-boot-v1",
        "inputs": {
            "uefi_fd": {"path": "/synthetic/SILICIUM_UEFI.fd", "bytes": 0x300000, "sha256": "0" * 64},
            "dtb": {"path": "/synthetic/zizhan.dtb", "bytes": 4096, "sha256": "1" * 64},
            "ios_image": {"path": "/synthetic/ios.img", "bytes": 65536, "sha256": "2" * 64},
            "stock_boot": {"path": str(tmp / "stock_boot_b.img"), "bytes": PART, "sha256": stock_sha},
        },
        "bootshim": {"bytes": len(shim), "sha256": "3" * 64, "symbols": symbols},
        "android_handoff": {"bytes": 1536, "sha256": "4" * 64},
        "pongfat": {"bytes": len(pongfat), "sha256": hashlib.sha256(pongfat).hexdigest()},
        "layout": {
            "kind": "pongfat-first", "direct": 0, "fdt_patch": 1, "android": 1,
            "ramdisk_kind": "stock", "pongfat_in_field": 1,
            "fd_offset": fd_off, "default_entry": "android",
            "kernel_field_bytes": k_field_bytes, "android_offset": android_off,
            "pongfat_offset": pongfat_off, "ramdisk_offset": ramdisk_off,
            "ramdisk_bytes": ramdisk_bytes, "signature_offset": sig_off,
            "vbmeta_offset": vbmeta_off, "original_image_size": orig_sz,
            "partition_bytes": PART,
        },
        "output": {"path": str(img), "bytes": PART, "sha256": img_sha},
    }
    manifest_path = tmp / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2))
    return img, manifest_path, Path(manifest["inputs"]["stock_boot"]["path"])


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="zizhan-m3-test-") as td:
        tmp = Path(td)
        img, manifest_path, _ = make_fixture(tmp)

        # 1. Valid package passes full verification and emits gated scripts
        scripts_dir = tmp / "scripts"
        summary = check_m3.verify_m3_package(manifest_path, check_sha=True)
        assert summary["default_entry"] == "android" and summary["layout"] == "pongfat-first"
        check_m3.emit_scripts(scripts_dir, manifest_path, summary)

        # 2. Both emitted scripts MUST refuse execution without ZIZHAN_USER_PRESENT=yes
        for sname in ("run-m3-ram-test.sh", "rollback-boot-b.sh"):
            sp = scripts_dir / sname
            r = subprocess.run(["bash", str(sp)], capture_output=True, text=True)
            assert r.returncode == 2 and "ZIZHAN_USER_PRESENT=yes" in r.stdout, (sname, r.returncode, r.stdout)

        # 3. Corrupting PONGFAT payload fails CRC32 check
        with open(img, "r+b") as f:
            f.seek(PAGE + 64 + 4032 + 4096 + 10)
            f.write(b"X")
        try:
            check_m3.verify_m3_package(manifest_path, check_sha=False)
            raise AssertionError("expected PONGFAT CRC32 failure")
        except ValueError as e:
            assert "CRC32" in str(e), e

        # Restore byte and corrupt ARMd magic -> fails Android ARM64 header check
        img, manifest_path, _ = make_fixture(tmp)
        with open(img, "r+b") as f:
            f.seek(PAGE + 0x400000 + 56)
            f.write(b"BAD!")
        try:
            check_m3.verify_m3_package(manifest_path, check_sha=False)
            raise AssertionError("expected Android ARM64 Image failure")
        except ValueError as e:
            assert "Android ARM64 Image" in str(e), e

    print("PASS m3-standalone-preflight: header/PONGFAT-CRC/ARMd/AVBf checks and ZIZHAN_USER_PRESENT gate")


if __name__ == "__main__":
    main()
