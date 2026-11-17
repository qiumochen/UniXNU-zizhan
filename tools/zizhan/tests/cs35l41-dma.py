#!/usr/bin/env python3
"""Actual read-only audio DMA/SMMU prerequisite core;no phone."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-dma-") as temp:
    binary=Path(temp)/"dma"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(R/"tools/zizhan/tests/cs35l41-dma.cpp"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
