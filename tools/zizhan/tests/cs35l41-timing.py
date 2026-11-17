#!/usr/bin/env python3
"""Actual GENI preparation core plus timing wrapper under ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-geni-timing-") as temp:
    temp = Path(temp)
    flags = ["-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(ROOT/"drivers/PongPower")]
    obj = temp/"geni.o"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11",*flags,"-c",str(ROOT/"drivers/PongPower/GeniI2c.c"),"-o",str(obj)],check=True,timeout=30)
    binary = temp/"test"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17",*flags,str(ROOT/"tools/zizhan/tests/cs35l41-timing.cpp"),str(obj),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
