#!/usr/bin/env python3
"""48k/2ch/24-bit test-pattern controls; no IOKit or hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-tone48-") as temp:
    binary=Path(temp)/"tone48"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(R/"tools/zizhan/tests/cs35l41-tone48.c"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
