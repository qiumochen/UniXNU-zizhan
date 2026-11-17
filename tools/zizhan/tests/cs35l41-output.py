#!/usr/bin/env python3
"""Actual native output-disabled setup through production GENI transactions."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-output-") as temp:
    binary=Path(temp)/"output"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(R/"drivers/PongPower/GeniI2c.c"),str(R/"drivers/PongPower/ZizhanCs35l41.c"),str(R/"tools/zizhan/tests/cs35l41-output.c"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary),*sys.argv[1:]],check=True,timeout=60)
