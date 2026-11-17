#!/usr/bin/env python3
"""Production silent HALO core full chain with synthetic or retained inputs."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-amp-run-") as temp:
    binary=Path(temp)/"run"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-I"+str(ROOT/"drivers/PongPower"),
                    str(ROOT/"drivers/PongPower/GeniI2c.c"),str(ROOT/"drivers/PongPower/ZizhanCs35l41.c"),
                    str(ROOT/"tools/zizhan/tests/cs35l41-run.c"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary),*sys.argv[1:]],check=True,timeout=60)
