#!/usr/bin/env python3
"""Bounded PRM owner uses actual GPR/AudioReach codecs under sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-prm-") as temp:
    output=Path(temp)/"owner"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-I"+str(ROOT/"drivers/PongPower"),
                    str(ROOT/"drivers/PongPower/ShengGpr.c"),str(ROOT/"drivers/PongPower/ShengAudioReach.c"),
                    str(ROOT/"tools/zizhan/tests/cs35l41-prm.c"),"-o",str(output)],check=True,timeout=30)
    subprocess.run([str(output)],check=True,timeout=10)
