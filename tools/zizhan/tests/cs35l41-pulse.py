#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='zizhan-pulse-') as temp:
    binary=Path(temp)/'pulse'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(R/'tools/zizhan/tests/cs35l41-pulse.cpp'),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
