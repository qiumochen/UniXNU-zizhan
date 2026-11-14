#!/usr/bin/env python3
"""Stage matching native iPhone restore inputs; never install or access a phone.

Uses UniXNU's range ZIP reader, normal ZIP CRC validation and kernelcache
decoder. Outputs include the exact BuildIdentity and local SHA-256 receipts.
The experimental selection does not change the shipping board/profile.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
import hashlib
import importlib
import json
import os
from pathlib import Path
from types import SimpleNamespace
import sys
import tomllib
import struct
import time
import urllib.request
import zipfile
import zlib

REPO = Path(__file__).resolve().parents[3]


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            result.update(block)
    return result.hexdigest()


def checked_component_digest(path, entry):
    result, checksum = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            result.update(block)
            checksum = zlib.crc32(block, checksum)
    if path.stat().st_size != entry.file_size or checksum != entry.CRC:
        raise RuntimeError("Staged component differs from the matched ZIP member: " + entry.filename)
    return result.hexdigest()


def stored_component(image, key, jobs):
    """Resume stored multi-GiB members with bounded parallel range requests."""
    entry = image.zip.getinfo(image.member(key))
    image.stream.seek(entry.header_offset)
    header = image.stream.read(30)
    if header[:4] != b"PK\x03\x04":
        raise RuntimeError("Invalid ZIP local header")
    name_bytes, extra_bytes = struct.unpack_from("<HH", header, 26)
    start = entry.header_offset + 30 + name_bytes + extra_bytes
    target = image.cache / Path(entry.filename).name
    image.cache.mkdir(parents=True, exist_ok=True)
    if target.exists():
        return image.extract(key)
    partial = target.with_suffix(target.suffix + ".partial")
    state_path = target.with_suffix(target.suffix + ".ranges.json")
    block = 8 << 20
    contract = {"url": image.spec, "member": entry.filename, "start": start,
                "bytes": entry.file_size, "block": block}
    if state_path.exists():
        state = json.loads(state_path.read_text())
        if state["contract"] != contract or not partial.exists():
            raise RuntimeError("Retain incompatible partial download for inspection: " + str(partial))
        complete = set(state["complete"])
        if partial.stat().st_size != entry.file_size:
            raise RuntimeError("Parallel partial download has unexpected size")
    else:
        size = partial.stat().st_size if partial.exists() else 0
        if size > entry.file_size:
            raise RuntimeError("Partial download exceeds the matched component")
        # A preceding sequential ZIP reader wrote a contiguous prefix. Its full
        # blocks can be retained; the final incomplete block is fetched again.
        complete = set(range(size // block))
    count = (entry.file_size + block - 1) // block
    if not complete.issubset(range(count)):
        raise RuntimeError("Invalid retained range indexes")
    def save():
        temp = state_path.with_suffix(".tmp")
        temp.write_text(json.dumps({"contract": contract, "complete": sorted(complete)}, indent=2) + "\n")
        temp.replace(state_path)
    fd = os.open(partial, os.O_RDWR | os.O_CREAT, 0o600)
    os.ftruncate(fd, entry.file_size)
    save()
    def fetch(index):
        offset = index * block
        length = min(block, entry.file_size - offset)
        first, last = start + offset, start + offset + length - 1
        for attempt in range(3):
            try:
                request = urllib.request.Request(image.spec, headers={"Range": f"bytes={first}-{last}"})
                with urllib.request.urlopen(request, timeout=45) as response:
                    if response.status != 206 or response.headers.get("Content-Range", "").split("/")[0] != f"bytes {first}-{last}":
                        raise RuntimeError("Server did not honor the exact component range")
                    data = response.read()
                if len(data) != length:
                    raise OSError("Incomplete component range")
                done = 0
                while done < length:
                    written = os.pwrite(fd, data[done:], offset + done)
                    if written <= 0:
                        raise OSError("Incomplete local component write")
                    done += written
                return index
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(attempt + 1)
    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            pending = [pool.submit(fetch, index) for index in range(count) if index not in complete]
            for future in as_completed(pending):
                complete.add(future.result())
                save()
                if len(complete) % 32 == 0 or len(complete) == count:
                    print(f"{key}: {len(complete)}/{count} ranges", flush=True)
        os.fsync(fd)
    finally:
        os.close(fd)
    checksum = 0
    with partial.open("rb") as stream:
        for data in iter(lambda: stream.read(block), b""):
            checksum = zlib.crc32(data, checksum)
    if checksum != entry.CRC:
        raise RuntimeError("Component ZIP CRC mismatch; partial and receipt retained")
    partial.rename(target)
    record = target.with_suffix(target.suffix + ".json")
    record.write_text(json.dumps({"component": key, "member": entry.filename,
                                  "size": entry.file_size, "sha256": digest(target),
                                  "source": "remote", "zip_crc_verified": True}, indent=2) + "\n")
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unixnu", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--profile", type=Path,
                        default=REPO / "configs/zizhan-ios18e-inputs.toml")
    parser.add_argument("--ipsw", help="Optional local IPSW; default is the pinned Apple URL")
    parser.add_argument("--component", action="append", default=[])
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--decode-kernel", action="store_true")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel reads for large stored ZIP members (1–8)")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error("--jobs must be between 1 and 8")
    output = args.out.expanduser().resolve()
    if output == REPO or REPO in output.parents:
        parser.error("Apple restore inputs must be staged outside the public device repository")
    unixnu = args.unixnu.expanduser().resolve()
    if not (unixnu / "scripts/unixnu/ipsw.py").is_file():
        parser.error("--unixnu must contain scripts/unixnu/ipsw.py")
    board = tomllib.loads(args.profile.read_text())
    config = board["ipsw"]
    output.mkdir(parents=True, exist_ok=True)
    # The shared library owns BuildIdentity matching and bounded HTTP range
    # reads. Keep its source decoder and validation intact.
    sys.path.insert(0, str(unixnu / "scripts"))
    ipsw_module = importlib.import_module("unixnu.ipsw")
    context = SimpleNamespace(board={"ipsw": config}, out_root=output,
                              option_text=lambda *_: None)
    image = ipsw_module.Ipsw(context, args.ipsw or config["url"])
    try:
        if image.identity["Info"].get("Variant") != "Customer Erase Install (IPSW)":
            raise RuntimeError("The matched identity is not Customer Erase Install (IPSW)")
        manifest = image.zip.read("BuildManifest.plist")
        manifest_path = output / "BuildManifest.plist"
        manifest_path.write_bytes(manifest)
        available = {}
        for key, item in image.identity["Manifest"].items():
            name = item.get("Info", {}).get("Path")
            if name in image.zip.namelist():
                entry = image.zip.getinfo(name)
                available[key] = {"member": name, "bytes": entry.file_size,
                                  "compressed_bytes": entry.compress_size}
        report = {
            "observed_at": datetime.now().astimezone().isoformat(),
            "scope": "local input preparation only; no boot, install or phone access",
            "profile": config,
            "source": "local IPSW" if args.ipsw else config["url"],
            "identity": image.identity["Info"],
            "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
            "available_components": available,
            "staged": [],
        }
        receipt = output / "native-inputs.json"
        receipt.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Matched {config['product']} {config['build']} {config['device_class']}", flush=True)
        if args.inventory_only:
            return
        components = args.component or ["KernelCache", "DeviceTree", "StaticTrustCache"]
        for key in dict.fromkeys(components):
            if key not in available:
                raise RuntimeError("Requested component absent from matched identity: " + key)
            entry = image.zip.getinfo(image.member(key))
            if image.remote and entry.compress_type == zipfile.ZIP_STORED and entry.file_size >= 64 << 20:
                path = stored_component(image, key, args.jobs)
            else:
                path = image.extract(key)
            record = {"component": key, "member": image.member(key),
                      "path": str(path.relative_to(output)), "bytes": path.stat().st_size,
                      "sha256": checked_component_digest(path, entry), "zip_crc_verified": True}
            if record["bytes"] != available[key]["bytes"]:
                raise RuntimeError("Staged component size differs from the ZIP member: " + key)
            report["staged"].append(record)
            receipt.write_text(json.dumps(report, indent=2) + "\n")
            print(f"Staged {key}: {record['bytes']} bytes", flush=True)
        if args.decode_kernel:
            kernel = ipsw_module.kernelcache(context, image)
            report["kernelcache"] = {"path": str(kernel.relative_to(output)),
                                     "bytes": kernel.stat().st_size,
                                     "sha256": digest(kernel)}
            receipt.write_text(json.dumps(report, indent=2) + "\n")
            print(f"Decoded kernelcache: {kernel.stat().st_size} bytes", flush=True)
    finally:
        image.zip.close()
        image.stream.close()


if __name__ == "__main__":
    main()
