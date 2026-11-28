#!/usr/bin/env python3
"""Zizhan audio capture ring + ADSP TX graph packet controls; no IOKit or hardware."""
import os
from pathlib import Path
import subprocess
import tempfile

R = Path(__file__).resolve().parents[3]
P = R / "drivers/PongPower"
with tempfile.TemporaryDirectory(prefix="zizhan-audio-capture-") as temp:
    binary = Path(temp) / "capture"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(P),
        str(P / "ShengGpr.c"),
        str(P / "ShengAudioReach.c"),
        str(R / "tools/zizhan/tests/cs35l41-capture.c"),
        "-lm", "-o", str(binary),
    ], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
