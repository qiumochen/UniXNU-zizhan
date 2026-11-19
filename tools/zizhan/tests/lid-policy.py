#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='zizhan-lid-') as temp:
    binary=Path(temp)/'lid'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/ArmDTPlatform'),str(R/'tools/zizhan/tests/lid-policy.cpp'),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
