#!/usr/bin/env python3
"""Host test for drivers/PongPower/ZizhanFat.h (MODEM-01 stage 1 firmware reader).

Builds FAT16 and FAT32 volumes in Python (no mkfs needed) inside a 4 KiB-block GPT, with an IMAGE directory, a long-name
entry that must be skipped, a deleted entry, and two files whose clusters interleave so the chain walk matters. The real
NON-HLOS partition can be checked too: ZIZHAN_FAT_IMAGE=<modem_b dump> runs the reader against it and lists image/modem.*.
"""
from pathlib import Path
import os
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "drivers/PongPower/ZizhanFat.h"
BLOCK = 4096


def short(name):
    base, _, ext = name.upper().partition(".")
    return (base.ljust(8) + ext.ljust(3)).encode()


def dirent(name83, attr, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", name83, attr, 0, 0, 0, 0, 0, cluster >> 16, 0, 0, cluster & 0xFFFF, size)


def lfn_entries(long, name83):
    """VFAT long-name entries (last fragment first) for `long`, bound to the 11-byte short name."""
    s = 0
    for c in name83:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    chars = [ord(c) for c in long] + [0]
    chars += [0xFFFF] * ((-len(chars)) % 13)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    out = []
    for n in range(len(parts), 0, -1):
        e = bytearray(32)
        e[0] = n | (0x40 if n == len(parts) else 0)
        e[11], e[13] = 0x0F, s
        for k, at in enumerate((1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)):
            struct.pack_into("<H", e, at, parts[n - 1][k])
        out.append(bytes(e))
    return b"".join(out)


def build_fat(kind, files, spc=4, bps=512):
    """files: {"IMAGE/MODEM.MDT": bytes}. Returns the volume bytes."""
    reserved = 32 if kind == 32 else 4
    root_entries = 0 if kind == 32 else 512
    clusters = 70000 if kind == 32 else 20000
    fat_entry = 4 if kind == 32 else 2
    fat_sectors = ((clusters + 2) * fat_entry + bps - 1) // bps
    root_sectors = root_entries * 32 // bps
    data_sector = reserved + 2 * fat_sectors + root_sectors
    total = data_sector + clusters * spc
    cb = spc * bps
    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
    eoc = 0x0FFFFFFF if kind == 32 else 0xFFFF
    data = {}
    nxt = [3 if kind == 32 else 2]

    def link(chain):
        for a, b in zip(chain, chain[1:]):
            fat[a] = b
        fat[chain[-1]] = eoc

    def write_chain(chain, payload):
        for i, c in enumerate(chain):
            data[c] = payload[i * cb:(i + 1) * cb].ljust(cb, b"\0")

    entries = [dirent(b".          ", 0x10, 0, 0), dirent(b"..         ", 0x10, 0, 0)]
    entries.append(b"\xe5" + dirent(short("GONE.BIN"), 0x20, 0, 0)[1:])          # deleted
    entries.append(b"\x41" + b"m\0o\0d\0e\0m\0" + bytes([0x0f, 0, 0]) + b"\0" * 18)   # 32 bytes   # long-name fragment
    names = sorted(files)
    need = {n: max(1, (len(files[n]) + cb - 1) // cb) for n in names}
    a, b = names[0], names[1]                      # these two interleave: a, b, a, b, ...
    ca, cbn = [], []
    while len(ca) < need[a] or len(cbn) < need[b]:
        if len(ca) < need[a]:
            ca.append(nxt[0]); nxt[0] += 1
        if len(cbn) < need[b]:
            cbn.append(nxt[0]); nxt[0] += 1
    chains = {a: ca, b: cbn}
    for n in names[2:]:
        chains[n] = list(range(nxt[0], nxt[0] + need[n])); nxt[0] += need[n]
    for n in names:
        link(chains[n])
        write_chain(chains[n], files[n])
        first = chains[n][0] if files[n] else 0
        if not files[n]:
            fat[chains[n][0]] = 0
        entries.append(dirent(short(n.split("/")[1]), 0x20, first, len(files[n])))
    image_chain = [nxt[0]]; nxt[0] += 1
    link(image_chain)
    image_dir = b"".join(entries).ljust(cb, b"\0")
    write_chain(image_chain, image_dir)
    # like the real NON-HLOS partition: "image" exists only as a long name (short name PCAFUI~2)
    root_list = [dirent(b"NON-HLOS   ", 0x08, 0, 0), lfn_entries("image", b"PCAFUI~2   "),
                 dirent(b"PCAFUI~2   ", 0x10, image_chain[0], 0)]
    root_bytes = b"".join(root_list)
    if kind == 32:
        fat[2] = eoc
        data[2] = root_bytes.ljust(cb, b"\0")
    bs = bytearray(bps)
    bs[0:3] = b"\xeb\x3c\x90"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, bps, spc, reserved, 2, root_entries, 0 if total > 0xFFFF else total,
                     0xF8, 0 if kind == 32 else fat_sectors, 63, 255, 0, total if total > 0xFFFF else 0)
    if kind == 32:
        struct.pack_into("<IHHI", bs, 36, fat_sectors, 0, 0, 2)
    bs[510:512] = b"\x55\xaa"
    vol = bytearray(total * bps)
    vol[0:bps] = bs
    packer = (lambda v: struct.pack("<I", v & 0x0FFFFFFF)) if kind == 32 else (lambda v: struct.pack("<H", v & 0xFFFF))
    fat_bytes = b"".join(packer(v) for v in fat).ljust(fat_sectors * bps, b"\0")
    for k in range(2):
        o = (reserved + k * fat_sectors) * bps
        vol[o:o + len(fat_bytes)] = fat_bytes
    if kind != 32:
        o = (reserved + 2 * fat_sectors) * bps
        vol[o:o + len(root_bytes)] = root_bytes
    for c, payload in data.items():
        o = data_sector * bps + (c - 2) * cb
        vol[o:o + cb] = payload
    return bytes(vol)


def gpt(parts):
    """parts: [(name, bytes)]. 4 KiB blocks; returns the LUN image."""
    first = 6
    lun = bytearray(BLOCK * first)
    entries = bytearray(BLOCK * 4)
    lba = first
    blobs = []
    for i, (name, blob) in enumerate(parts):
        nblocks = (len(blob) + BLOCK - 1) // BLOCK
        e = bytearray(128)
        e[0:16] = bytes(range(1, 17))
        e[16:32] = os.urandom(16)
        struct.pack_into("<QQ", e, 32, lba, lba + nblocks - 1)
        e[56:56 + 2 * len(name)] = name.encode("utf-16-le")
        entries[i * 128:(i + 1) * 128] = e
        blobs.append((lba, blob))
        lba += nblocks
    hdr = bytearray(92)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<Q", hdr, 72, 2)
    struct.pack_into("<II", hdr, 80, 128, 128)
    lun[BLOCK:BLOCK + 92] = hdr
    lun[2 * BLOCK:2 * BLOCK + len(entries)] = entries[:4 * BLOCK]
    lun = bytearray(lun.ljust(lba * BLOCK, b"\0"))
    for at, blob in blobs:
        lun[at * BLOCK:at * BLOCK + len(blob)] = blob
    return bytes(lun)


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ZizhanFat.h"
using namespace zizhan_fat;
static FILE *img;
static unsigned reads;
static int rd(void *, uint64_t off, void *dst, uint32_t len) {
  ++reads;
  if (fseeko(img, (off_t)off, SEEK_SET)) return -1;
  return fread(dst, 1, len, img) == len ? 0 : -1;
}
static uint64_t fnv(const uint8_t *p, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h; }
int main(int argc, char **argv) {
  img = fopen(argv[1], "rb"); assert(img);
  const char *part = argv[2];
  GptPart p{};
  const int g = gpt_find(rd, nullptr, 4096, part, &p);
  if (g) { printf("gpt %d\n", g); return 2; }
  assert(gpt_find(rd, nullptr, 4096, "nosuchpart", &p) == -7);
  gpt_find(rd, nullptr, 4096, part, &p);
  static Volume v;
  const int m = mount(&v, rd, nullptr, p.first_lba * 4096);
  if (m) { printf("mount %d\n", m); return 3; }
  printf("type=%u cluster=%u clusters=%u\n", v.type, v.cluster_bytes, v.cluster_count);
  for (int i = 3; i < argc; ++i) {
    File f;
    const int o = open_path(&v, argv[i], &f);
    if (o) { printf("%s open %d\n", argv[i], o); continue; }
    uint8_t *buf = (uint8_t *)malloc(f.size ? f.size : 1);
    /* whole file in uneven pieces, then a backwards seek */
    uint64_t at = 0; unsigned piece = 777;
    while (at < f.size) { uint32_t n = (uint32_t)(f.size - at < piece ? f.size - at : piece); assert(!file_read(&v, &f, at, buf + at, n)); at += n; piece = piece * 3 % 70001 + 513; }
    if (f.size > 10) { uint8_t b[10]; assert(!file_read(&v, &f, 1, b, 10)); assert(!memcmp(b, buf + 1, 10)); }
    assert(file_read(&v, &f, f.size, buf, 1) == -1);
    printf("%s size=%u fnv=%016llx\n", argv[i], f.size, (unsigned long long)fnv(buf, f.size));
    free(buf);
  }
  File d; assert(open_path(&v, "image", &d) == -11);
  File x; assert(open_path(&v, "image/gone.bin", &x) == -2);
  assert(open_path(&v, "image/toolongname.bin", &x) == -2);
  assert(open_path(&v, "IMAGE/MODEM.MDT", &x) == 0);
  return 0;
}
'''


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    cxx = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="zizhan-fat-") as tmp:
        t = Path(tmp)
        (t / "t.cpp").write_text(HARNESS)
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-I" + str(HEADER.parent), str(t / "t.cpp"), "-o", str(t / "t")], check=True, timeout=60)
        rng = random.Random(3)
        files = {"IMAGE/MODEM.MDT": bytes(rng.getrandbits(8) for _ in range(5884)),
                 "IMAGE/MODEM.B02": bytes(rng.getrandbits(8) for _ in range(80472)),
                 "IMAGE/MODEM.B00": bytes(rng.getrandbits(8) for _ in range(1076)),
                 "IMAGE/EMPTY.BIN": b""}
        for kind in (16, 32):
            lun = gpt([("modemst1", b"\0" * 8192), ("modem_b", build_fat(kind, files))])
            (t / "lun.img").write_bytes(lun)
            out = subprocess.run([str(t / "t"), str(t / "lun.img"), "modem_b", "image/modem.mdt", "image/modem.b02",
                                  "image/modem.b00", "image/empty.bin"], capture_output=True, text=True, timeout=60)
            assert out.returncode == 0, out.stdout + out.stderr
            assert f"type={kind}" in out.stdout, out.stdout
            for n, payload in files.items():
                line = f"{n.lower()} size={len(payload)} fnv={fnv(payload):016x}"
                assert line in out.stdout, (line, out.stdout)
            print(f"PASS FAT{kind}: GPT lookup, 8.3 path, interleaved cluster chains, skip LFN/deleted, bounds")
        real = os.environ.get("ZIZHAN_FAT_IMAGE")
        if real:
            out = subprocess.run([str(t / "t"), real, os.environ.get("ZIZHAN_FAT_PART", "modem_b"), "image/modem.mdt",
                                  "image/modem.b23", "image/modem.b31"], capture_output=True, text=True, timeout=300)
            print(out.stdout.strip())
            assert out.returncode == 0, out.stderr
    print("PASS ZizhanFat host test")


if __name__ == "__main__":
    main()
