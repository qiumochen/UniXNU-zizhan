#!/usr/bin/env python3
"""Production TDM endpoint encoder and strict lifecycle;no hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-tdm-") as temp:
    binary=Path(temp)/"tdm"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(R/"drivers/PongPower/ShengGpr.c"),str(R/"drivers/PongPower/ShengAudioReach.c"),str(R/"tools/zizhan/tests/cs35l41-tdm.c"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
