#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='zizhan-cci-') as temp:
    binary=Path(temp)/'cci'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(R/'tools/zizhan/tests/cci.c'),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
