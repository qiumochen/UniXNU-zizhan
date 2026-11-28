#!/usr/bin/env python3
"""Host tests for the IO-01 physical volume-key injection schedule."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-vol-inject-") as temp:
    binary = Path(temp) / "vol-inject"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(root / "drivers/ArmDTPlatform"),
        str(root / "tools/zizhan/tests/volume-inject.c"), "-o", str(binary),
    ], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
src = (root / "drivers/ArmDTPlatform/ZizhanVolumeInject.h").read_text()
assert 'PE_parse_boot_argn("zizhan_vol_inject"' in src
assert src.index('PE_parse_boot_argn("zizhan_vol_inject"') < src.index("kernel_thread_start")
ui = (root / "userland/ui_compat18.c").read_text()
assert 'sel_registerName("cancelVolumeEvent")' in ui, "volume18 must disarm 22H374 SBVolumeControl performSelector repeat"
assert "if(!volume18_keys[i].held && (steps || was_held)" in ui
print("volume-inject.py: PASS")
