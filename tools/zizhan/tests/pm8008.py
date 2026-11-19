#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='zizhan-pm8008-') as temp:
    binary=Path(temp)/'pm8008'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(R/'tools/zizhan/tests/pm8008.c'),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
