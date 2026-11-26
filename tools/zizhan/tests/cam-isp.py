#!/usr/bin/env python3
"""Host unit test runner for ZizhanCamIsp.h (Stage 6 CAM-01 ISP/MIPI/RAW10 logic)."""
import os
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SRC = os.path.join(ROOT, "tools/zizhan/tests/cam-isp.c")
SMMU = os.path.join(ROOT, "drivers/PongAdreno/ArmSmmu4K/ArmSmmu4K.c")
INC = os.path.join(ROOT, "drivers/PongPower")

import shutil
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
with tempfile.TemporaryDirectory() as td:
    exe = os.path.join(td, "cam-isp")
    subprocess.check_call([CC, "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-I", INC, SRC, SMMU, "-o", exe])
    out = subprocess.check_output([exe], text=True).strip()
    assert "PASS cam-isp" in out, out
    print(out)
