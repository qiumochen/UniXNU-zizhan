#!/usr/bin/env python3
"""Replay Wi-Fi context/readback failures and WMI send-wait ownership cases.

Requires a C compiler and the locally provisioned Ath12kWifi shared headers.
Does not access a phone, firmware or network credentials.
"""
import argparse
import json
import os
import plistlib
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import pong_paths

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default=os.environ.get("CC", "cc"))
    p.add_argument("--shared", type=Path, default=pong_paths.sheng_root() / "drivers/Ath12kWifi")
    args = p.parse_args()
    tests = ROOT / "tools/zizhan/tests"
    smmu = ROOT / "drivers/PongAdreno/ArmSmmu4K"
    with tempfile.TemporaryDirectory(prefix="zizhan-wifi-host-") as td:
        for name, inputs, include in [
            ("context", [tests / "wifi-context-readback.c", smmu / "ArmSmmu4KContext.c"], smmu),
            ("wmi-wait", [tests / "wifi-wmi-wait.c"], args.shared / "shim/include"),
        ]:
            binary = Path(td) / name
            subprocess.run([args.cc, "-std=gnu11", "-Wno-macro-redefined", "-Wno-unused-function",
                "-I", str(include), *map(str, inputs), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        # Test the real probe script with isolated USB/tool processes.
        usb = Path(td) / "usb.plist"
        called = Path(td) / "fastboot-called"
        ioreg = Path(td) / "ioreg"
        fastboot = Path(td) / "fastboot"
        ioreg.write_text("#!/bin/sh\ncat " + shlex.quote(str(usb)) + "\n")
        fastboot.write_text("#!/bin/sh\necho called >> " + shlex.quote(str(called)) +
            "\nprintf 'test\t fastboot\n'\nexit ${TEST_FASTBOOT_STATUS:-0}\n")
        ioreg.chmod(0o700)
        fastboot.chmod(0o700)
        env = dict(os.environ, IOREG_BIN=str(ioreg), FASTBOOT_BIN=str(fastboot))
        for product, status, expected_called, expected_status in [
            (0x4ee7, "0", False, 1), (None, "0", False, 1),
            (0xd00d, "0", True, 0), (0xd00d, "3", True, 1),
        ]:
            usb.write_bytes(plistlib.dumps([] if product is None else [
                {"idVendor": 0x18d1, "idProduct": product}]))
            called.unlink(missing_ok=True)
            result = subprocess.run(["bash", str(ROOT / "tools/zizhan/mac/fastboot-visible.sh")],
                env=dict(env, TEST_FASTBOOT_STATUS=status))
            assert called.exists() == expected_called
            assert result.returncode == expected_status
        print("PASS: USB guard excludes Android/missing devices; real fastboot identity and tool status required")
        root = Path(td) / "usb-work"
        (root / "logs").mkdir(parents=True)
        recorded = root / "logs/phone-port.txt"
        status = Path(td) / "hub-status"
        hub = Path(td) / "uhubctl"
        hub.write_text("""#!/usr/bin/env python3
import json, os, pathlib, sys
if '-a' in sys.argv:
    pathlib.Path(os.environ['TEST_CALL']).write_text(json.dumps(sys.argv[1:]))
    sys.exit(int(os.environ.get('TEST_CYCLE_STATUS','0')))
print(pathlib.Path(os.environ['TEST_STATUS']).read_text(), end='')
""")
        hub.chmod(0o700)
        env = dict(os.environ, ZIZHAN_SRC=str(root), UHUBCTL_BIN=str(hub),
                   TEST_CALL=str(called), TEST_STATUS=str(status))
        usbkick = ["bash", str(ROOT / "tools/zizhan/mac/usbkick.sh")]
        for descriptor, cycle_status, expected in [
            ("0100 power", "0", 0),
            ("0103 power enable connect [046d:c53f Receiver]", "0", 1),
            ("0100 power", "4", 4),
        ]:
            recorded.write_text("8-3 3\n")
            status.write_text("Current status for hub 8-3 [05ac:8009]\n  Port 3: " + descriptor + "\n")
            called.unlink(missing_ok=True)
            result = subprocess.run(usbkick + ["kick"], env=dict(env, TEST_CYCLE_STATUS=cycle_status),
                                    capture_output=True, text=True)
            assert result.returncode == expected
            if expected == 1:
                assert not called.exists() and "refusing occupied" in result.stdout
            else:
                assert json.loads(called.read_text()) == ["-e", "-l", "8-3", "-p", "3", "-a", "cycle", "-d", "3"]
                assert ("power-cycled" in result.stdout) == (expected == 0)
        result = subprocess.run(usbkick + ["remember"], env=env, capture_output=True)
        assert result.returncode == 1 and not recorded.exists()
        print("PASS: USB recovery targets only the recorded port, refuses foreign devices, propagates errors")
if __name__ == "__main__":
    main()
