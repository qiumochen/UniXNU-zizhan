#!/usr/bin/env python3
"""Production owned SMMU transaction against synthetic MMIO;no phone."""
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-dma-map-") as temp:
    d=Path(temp);objects=[]
    for name in ("ArmSmmu4K","ArmSmmu4KContext","ArmSmmu4KProgram"):
        obj=d/(name+".o")
        subprocess.run([os.environ.get("CC","cc"),"-std=c11","-fsanitize=address,undefined","-c",str(R/"drivers/PongAdreno/ArmSmmu4K"/(name+".c")),"-o",str(obj)],check=True,timeout=30)
        objects.append(str(obj))
    binary=d/"map"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(R/"drivers/PongPower"),str(R/"tools/zizhan/tests/cs35l41-dma-map.cpp"),*objects,"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=20)
