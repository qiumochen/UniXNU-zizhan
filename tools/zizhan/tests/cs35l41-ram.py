#!/usr/bin/env python3
"""Actual RAM/GENI executor, synthetic bytes and optional original private WMFW."""
import os
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-amp-ram-") as temp:
    binary = Path(temp) / "ram"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "drivers/PongPower"),
                    str(ROOT / "drivers/PongPower/GeniI2c.c"),
                    str(ROOT / "drivers/PongPower/ZizhanCs35l41.c"),
                    str(ROOT / "tools/zizhan/tests/cs35l41-ram.c"), "-o", str(binary)], check=True, timeout=30)
    result = subprocess.run([str(binary), *sys.argv[1:]], timeout=60, capture_output=True, text=True)
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        result.check_returncode()
    payload = Path(sys.argv[1]).read_bytes() if len(sys.argv) == 2 else bytes((i*37+19)&255 for i in range(34068))
    hashes = 0
    for line in result.stdout.splitlines():
        if line.startswith("HASH "):
            _, size, digest = line.split()
            assert hashlib.sha256(payload[:int(size)]).hexdigest() == digest
            hashes += 1
        else:
            print(line)
    assert hashes == 257
    print("PASS production SHA256 versus independent hashlib:257 lengths including complete firmware")
