#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only M3 standalone dual-boot A' image preflight verifier & gated operator script generator.

Strictly offline: never invokes adb, fastboot, or device writes. Validates:
  1. zizhan-m3-aprime-boot-v1 manifest schema and output image SHA256 / 192 MiB partition geometry
  2. Android boot image v4 header (ANDROID!, version 4, header_size 1584, kernel_field <= 190.13 MiB)
  3. Embedded PONGFAT1 header + CRC32 + UEFI FD (3 MiB) + embedded stock Android ARM64 Image (MZ + ARMd)
  4. AVB footer (AVBf) + optional gzipped stock boot_b backup SHA256 match
  5. Optional --emit-scripts DIR generating ZIZHAN_USER_PRESENT=yes-gated RAM-test & rollback scripts
"""
import argparse
import gzip
import hashlib
import json
import os
import struct
import sys
import zlib
from pathlib import Path

PARTITION_BYTES = 201326592
PAGE_BYTES = 4096
MAX_KERNEL_FIELD = 199364608
UEFI_FD_BYTES = 0x300000
PONGFAT_MAGIC = b"PONGFAT1"
PONGFAT_HEADER = 4096


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_gzip(path: Path) -> tuple[int, str]:
    h = hashlib.sha256()
    total = 0
    with gzip.open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4 << 20), b""):
            total += len(chunk)
            h.update(chunk)
    return total, h.hexdigest()


def verify_m3_package(manifest_path: Path, backup_boot_gz: Path | None = None, check_sha: bool = True) -> dict:
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != "zizhan-m3-aprime-boot-v1":
        raise ValueError(f"unexpected manifest format: {manifest.get('format')!r}")

    layout = manifest["layout"]
    out_info = manifest["output"]
    shim_info = manifest["bootshim"]
    symbols = shim_info["symbols"]
    img_path = Path(out_info["path"])
    if not img_path.is_file():
        candidate = manifest_path.parent / img_path.name
        if candidate.is_file():
            img_path = candidate
        else:
            raise FileNotFoundError(f"missing M3 image: {out_info['path']}")

    size = img_path.stat().st_size
    if size != PARTITION_BYTES or layout.get("partition_bytes") != PARTITION_BYTES or out_info.get("bytes") != PARTITION_BYTES:
        raise ValueError(f"partition size mismatch: file={size} layout={layout.get('partition_bytes')} expected={PARTITION_BYTES}")

    if check_sha:
        actual_sha = sha256_file(img_path)
        if actual_sha != out_info["sha256"]:
            raise ValueError(f"M3 image sha256 mismatch: actual={actual_sha} manifest={out_info['sha256']}")

    with open(img_path, "rb") as f:
        hdr = f.read(PAGE_BYTES)
        if hdr[:8] != b"ANDROID!":
            raise ValueError("missing ANDROID! boot header magic")
        k_size, r_size = struct.unpack_from("<2I", hdr, 8)
        hdr_size = struct.unpack_from("<I", hdr, 20)[0]
        hdr_ver = struct.unpack_from("<I", hdr, 40)[0]
        if hdr_ver != 4 or hdr_size != 1584:
            raise ValueError(f"expected Android boot v4 (hdr_size=1584), got v{hdr_ver} (hdr_size={hdr_size})")
        if k_size != layout["kernel_field_bytes"] or k_size > MAX_KERNEL_FIELD:
            raise ValueError(f"invalid kernel_field_bytes: hdr={k_size} manifest={layout['kernel_field_bytes']}")
        if r_size != layout["ramdisk_bytes"]:
            raise ValueError(f"ramdisk_bytes mismatch: hdr={r_size} manifest={layout['ramdisk_bytes']}")

        # Verify BootShim patched symbols inside field
        f.seek(PAGE_BYTES)
        shim_bytes = f.read(shim_info["bytes"])
        for sym in ("_Head", "_StackSize", "_AndroidOffset", "_PongFatOffset", "_PongFatSize", "_FdOffset", "_Payload"):
            if sym not in symbols:
                raise ValueError(f"missing BootShim symbol {sym}")
        patched_android = struct.unpack_from("<Q", shim_bytes, symbols["_AndroidOffset"])[0]
        patched_pf_off = struct.unpack_from("<Q", shim_bytes, symbols["_PongFatOffset"])[0]
        patched_pf_sz = struct.unpack_from("<Q", shim_bytes, symbols["_PongFatSize"])[0]
        patched_fd_off = struct.unpack_from("<Q", shim_bytes, symbols["_FdOffset"])[0]
        if patched_android != layout["android_offset"]:
            raise ValueError(f"_AndroidOffset mismatch: {patched_android} != {layout['android_offset']}")
        if patched_pf_off != layout["pongfat_offset"]:
            raise ValueError(f"_PongFatOffset mismatch: {patched_pf_off} != {layout['pongfat_offset']}")
        if patched_fd_off != layout["fd_offset"]:
            raise ValueError(f"_FdOffset mismatch: {patched_fd_off} != {layout['fd_offset']}")

        # Verify PONGFAT1 header and CRC32
        if layout.get("pongfat_in_field"):
            pf_file_off = PAGE_BYTES + symbols["_Payload"] + layout["pongfat_offset"]
            if patched_pf_sz != manifest["pongfat"]["bytes"]:
                raise ValueError(f"_PongFatSize mismatch: {patched_pf_sz} != {manifest['pongfat']['bytes']}")
        else:
            pf_file_off = layout["ramdisk_offset"]
        f.seek(pf_file_off)
        pf_hdr = f.read(PONGFAT_HEADER)
        if pf_hdr[:8] != PONGFAT_MAGIC:
            raise ValueError(f"missing PONGFAT1 magic at offset {pf_file_off:#x}")
        fat_bytes, fat_crc = struct.unpack_from("<QI", pf_hdr, 16)
        if PONGFAT_HEADER + fat_bytes != manifest["pongfat"]["bytes"]:
            raise ValueError(f"PONGFAT size mismatch: {PONGFAT_HEADER + fat_bytes} != {manifest['pongfat']['bytes']}")
        crc_acc = 0
        rem = fat_bytes
        fat_sample = bytearray()
        while rem > 0:
            chunk = f.read(min(1 << 20, rem))
            if not chunk:
                raise ValueError("truncated PONGFAT payload")
            if len(fat_sample) < (2 << 20):
                fat_sample.extend(chunk[: (2 << 20) - len(fat_sample)])
            crc_acc = zlib.crc32(chunk, crc_acc)
            rem -= len(chunk)
        if (crc_acc & 0xFFFFFFFF) != fat_crc:
            raise ValueError(f"PONGFAT CRC32 mismatch: {(crc_acc & 0xFFFFFFFF):#x} != {fat_crc:#x}")
        if b'default = "' not in fat_sample or b"DarwinBoot24.efi" not in fat_sample:
            raise ValueError("PONGFAT FAT root missing unixnu.toml default entry or DarwinBoot24.efi")

        # Verify embedded stock Android ARM64 Image header
        if layout.get("android"):
            and_off = PAGE_BYTES + layout["android_offset"]
            f.seek(and_off)
            and_hdr = f.read(64)
            if and_hdr[:2] != b"MZ" or and_hdr[56:60] != b"ARMd":
                raise ValueError(f"invalid Android ARM64 Image header at {and_off:#x}")

        # Verify UEFI FD size in manifest
        if manifest["inputs"]["uefi_fd"]["bytes"] != UEFI_FD_BYTES:
            raise ValueError(f"unexpected UEFI FD size: {manifest['inputs']['uefi_fd']['bytes']}")

        # Verify AVB footer at end of partition
        f.seek(PARTITION_BYTES - 64)
        footer = f.read(64)
        if footer[:4] != b"AVBf":
            raise ValueError("missing AVBf footer at end of 192 MiB image")
        _, _, _, orig_sz, vbmeta_off, vbmeta_sz, _ = struct.unpack(">4sIIQQQ28s", footer)
        if orig_sz != layout["original_image_size"] or vbmeta_off != layout["vbmeta_offset"] or vbmeta_sz != 0x380:
            raise ValueError("AVBf footer offsets do not match manifest layout")

    backup_verified = False
    if backup_boot_gz is not None:
        raw_sz, raw_sha = sha256_gzip(backup_boot_gz)
        stock_info = manifest["inputs"]["stock_boot"]
        if raw_sz != PARTITION_BYTES or raw_sha != stock_info["sha256"]:
            raise ValueError(
                f"backup {backup_boot_gz.name} mismatch: bytes={raw_sz} sha={raw_sha} vs stock_boot={stock_info['sha256']}"
            )
        backup_verified = True

    return {
        "image": str(img_path),
        "image_sha256": out_info["sha256"],
        "stock_boot_sha256": manifest["inputs"]["stock_boot"]["sha256"],
        "stock_boot_path": manifest["inputs"]["stock_boot"]["path"],
        "default_entry": layout.get("default_entry"),
        "layout": layout.get("kind"),
        "direct": layout.get("direct"),
        "kernel_field_mib": round(layout["kernel_field_bytes"] / (1024 * 1024), 2),
        "backup_verified": backup_verified,
    }


def emit_scripts(out_dir: Path, manifest_path: Path, summary: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    checker = Path(__file__).resolve()
    m3_ramtest = checker.parent / "m3-ramtest.sh"
    img = summary["image"]
    stock_boot = summary["stock_boot_path"]
    stock_sha = summary["stock_boot_sha256"]

    ram_sh = out_dir / "run-m3-ram-test.sh"
    ram_sh.write_text(
        f"""#!/bin/bash
# Attended-only M3 standalone dual-boot RAM test (zero partition writes: fastboot boot only).
set -euo pipefail
if [ "${{ZIZHAN_USER_PRESENT:-}}" != "yes" ]; then
  echo "REFUSED: set ZIZHAN_USER_PRESENT=yes only when physically next to the Mix Fold 2."
  exit 2
fi
python3 "{checker}" --manifest "{manifest_path.resolve()}"
OUT_RUN="${{1:-{out_dir.resolve()}/ramtest-$(date +%Y%m%d-%H%M%S)}}"
exec bash "{m3_ramtest}" "{img}" "$OUT_RUN"
"""
    )
    os.chmod(ram_sh, 0o755)

    rb_sh = out_dir / "rollback-boot-b.sh"
    rb_sh.write_text(
        f"""#!/bin/bash
# Attended emergency rollback: verify stock boot_b SHA256 ({stock_sha[:12]}), flash boot_b, keep slot b active.
set -euo pipefail
export PATH=/opt/homebrew/bin:$PATH
if [ "${{ZIZHAN_USER_PRESENT:-}}" != "yes" ]; then
  echo "REFUSED: set ZIZHAN_USER_PRESENT=yes only when physically next to the Mix Fold 2."
  exit 2
fi
STOCK="{stock_boot}"
[ -f "$STOCK" ] || {{ echo "missing stock boot_b: $STOCK"; exit 2; }}
ACTUAL=$(shasum -a 256 "$STOCK" | awk '{{print $1}}')
[ "$ACTUAL" = "{stock_sha}" ] || {{ echo "SHA256 mismatch: $ACTUAL != {stock_sha}"; exit 2; }}
SLOT=$(fastboot getvar current-slot 2>&1 | awk '/current-slot:/ {{print $2}}')
echo "Verified stock boot_b ($ACTUAL), current-slot=$SLOT -> flashing boot_b and setting active slot b"
fastboot flash boot_b "$STOCK"
fastboot set_active b
fastboot reboot
"""
    )
    os.chmod(rb_sh, 0o755)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", type=Path, required=True, help="Path to manifest.json from build_m3_aprime.py")
    ap.add_argument("--backup-boot-gz", type=Path, default=None, help="Optional backup boot_b.img.gz to cross-verify")
    ap.add_argument("--no-sha", action="store_true", help="Skip full 192 MiB SHA256 pass (header/CRC checks still run)")
    ap.add_argument("--emit-scripts", type=Path, default=None, help="Directory to write gated attended scripts")
    a = ap.parse_args()
    summary = verify_m3_package(a.manifest, a.backup_boot_gz, check_sha=not a.no_sha)
    if a.emit_scripts:
        emit_scripts(a.emit_scripts, a.manifest, summary)
        summary["emitted_scripts"] = [str(a.emit_scripts / "run-m3-ram-test.sh"), str(a.emit_scripts / "rollback-boot-b.sh")]
    print("M3_STANDALONE_PREFLIGHT: PASS " + json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
