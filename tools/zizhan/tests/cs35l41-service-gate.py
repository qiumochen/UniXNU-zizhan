#!/usr/bin/env python3
"""ZizhanAudio I1 owner: host decisions plus static default-off checks of the IOKit glue."""
import os
import re
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
P=R/"drivers/PongPower"
with tempfile.TemporaryDirectory(prefix="zizhan-service-gate-") as temp:
    binary=Path(temp)/"gate"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I"+str(P),str(R/"tools/zizhan/tests/cs35l41-service-gate.c"),"-o",str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=10)
source=(P/"ZizhanAudioService.cpp").read_text()
probe=source[source.index('extern "C" void zizhan_audio_service_probe'):]
# Default off: the gate returns before any allocation, class registration or thread.
gate=probe.index("if(gate==ZizhanGateOff)return;")
for later in ("inTaskWithOptions","preModLoad","OSTypeAlloc","kernel_thread_start"):
    assert probe.index(later)>gate,later
# No graph, output or amp path is reachable from the I1 service.
for forbidden in ("sheng_adsp_send","zizhan_audio_dma_map_begin","zizhan_cs35l41","ZizhanAudioOutputReady","GLOBAL_EN","PongAudio","sheng_audio_memory","sheng_audio_request"):
    assert forbidden not in source,forbidden
# Start is granted only through the one-shot handoff, and only when PRM mode 40 registered as owner.
assert source.count("zizhan_service_request(")==1 and ",owner)){" in source and "zizhan_handoff_request_start(&gHandoff)" in source
assert "zizhan_handoff_register" not in source,"only the PRM owner registers"
# The self-test producer exists only with PRM mode 40 and the explicit boot-arg.
assert "zizhan_service_owner_mode(prm)" in probe and "zizhan_audio_selftest" in probe
# Kernel thread stack: no large locals in this file.
assert not re.search(r"\[\s*(\d{3,})\s*\]",source.replace("native_zizhan_audio_service[8]","")),"large local array"
build=(R/"scripts/build-pong-power-native.py").read_text()
assert "'ZizhanAudioService.cpp'" in build and "late_init(obj,name,'ZizhanAudioServiceStaticInit')" in build
power=(R/"drivers/PongPower/PongPower.cpp").read_text()
assert power.count("zizhan_audio_service_probe();")==1
print("cs35l41-service-gate.py: PASS")
