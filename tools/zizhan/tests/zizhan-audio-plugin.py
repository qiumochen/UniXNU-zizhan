#!/usr/bin/env python3
"""ZizhanAudio HAL bundle: compile for iOS 18 like userland/build-ipados18.py and check its protocol; macOS host only."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
source=(R/"userland/ZizhanAudioPlugIn.c").read_text()
# Protocol checks (docs/plan/2026-11-17-zizhan-audio-service.md): presence is the valid v2 page, IO waits
# for Running, the watermark is published only after the copy loop, and nothing uses the Pong ABI.
assert "ShengAudio" not in source.replace("UniXNU-sheng","") and "SHENG_AUDIO" not in source
assert "ZizhanAudioOutputReady" not in source,"the bundle must not gate on (or set) Ready"
assert "zizhan_audio_control_valid(gControl)" in source and "!running()" in source
io=source[source.index("static OSStatus DoIOOperation"):source.index("static OSStatus EndIOOperation")]
assert io.index("toS16(src[2 * i + 1]")<io.index("zizhan_audio_publish_written(gControl, t)"),"watermark after the copy"
assert "t > written" in io,"watermark never moves backwards"
assert re.search(r'void \*ZizhanAudio_Create\(',source) and "PongAudio_Create" not in source
if not shutil.which("xcrun"):
    print("zizhan-audio-plugin.py: protocol PASS; compile SKIP (no xcrun)");sys.exit(0)
sdk=subprocess.check_output(["xcrun","--sdk","iphoneos","--show-sdk-path"],text=True).strip()
mac=subprocess.check_output(["xcrun","--sdk","macosx","--show-sdk-path"],text=True).strip()
with tempfile.TemporaryDirectory(prefix="zizhan-audio-plugin-") as temp:
    inc=Path(temp)/"CoreAudio";inc.mkdir()
    for name in ("AudioServerPlugIn.h","AudioHardwareBase.h"):
        shutil.copyfile(Path(mac)/"System/Library/Frameworks/CoreAudio.framework/Headers"/name,inc/name)
    out=Path(temp)/"ZizhanAudio"
    subprocess.run(["xcrun","clang","-target","arm64-apple-ios18.0","-isysroot",sdk,"-Os","-fblocks","-bundle","-fvisibility=hidden",
        "-Wall","-Wextra","-Werror","-Wl,-no_fixup_chains","-I"+temp,"-I"+str(R/"drivers/PongPower"),
        "-framework","CoreFoundation","-framework","IOKit",str(R/"userland/ZizhanAudioPlugIn.c"),"-o",str(out)],check=True,timeout=120)
    syms=subprocess.check_output(["xcrun","nm","-gU",str(out)],text=True)
    assert "_ZizhanAudio_Create" in syms and "_PongAudio_Create" not in syms,syms
print("zizhan-audio-plugin.py: PASS (protocol + iOS 18 bundle compile)")
