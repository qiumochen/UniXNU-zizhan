#!/usr/bin/env python3
"""Actual bounded SLPI inventory against native SMEM/SMP2P producer fixtures."""
from pathlib import Path
import subprocess,tempfile,os
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='zizhan-slpi-transport-') as temp:
 d=Path(temp);objects=[]
 for name in ('QcomSmem','QcomSmp2p'):
  obj=d/(name+'.o');subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-c',str(R/'drivers/PongPower'/(name+'.c')),'-o',str(obj)],check=True,timeout=30);objects.append(str(obj))
 binary=d/'transport';subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(R/'tools/zizhan/tests/slpi-transport.cpp'),*objects,'-o',str(binary)],check=True,timeout=30)
 subprocess.run([str(binary)],check=True,timeout=10)
