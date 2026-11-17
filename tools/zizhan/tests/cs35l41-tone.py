#!/usr/bin/env python3
"""Actual finite data preparation with independent numeric waveform checks."""
import os,math,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2] if HERE.name=='tests' else Path(__file__).resolve().parents[3]/'UniXNU-zizhan'
INCLUDE=ROOT/'drivers/PongPower' if HERE.name=='tests' else HERE
with tempfile.TemporaryDirectory(prefix='zizhan-tone-') as temp:
    binary=Path(temp)/'tone';wave=Path(temp)/'wave'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(INCLUDE),str(HERE/'cs35l41-tone.cpp'),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
    subprocess.run([str(binary),str(wave)],check=True,timeout=10)
    words=struct.unpack('<2880i',wave.read_bytes());left=words[0::3]
    assert words[0::3]==words[1::3] and not any(words[2::3])
    assert not left[0] and not left[-1] and max(map(abs,left))==33554432
    assert all((v&255)==0 for v in words)
    for frame,v in enumerate(left):
        taper=min(frame,959-frame,96)/96
        expected=math.sin(2*math.pi*1000*frame/96000)*33554432*taper
        assert abs(v-expected)<512,(frame,v,expected)
    # Integer1kHz phase: exact zero at every48frames; correct polarity.
    assert all(left[i]==0 for i in range(0,960,48)) and left[24]>0 and left[72]<0
print('PASS actual generated waveform11520B/1kHz/10ms/threechannels/24logical/1ms taper/-36dBFSpeak;no output/hardware')
