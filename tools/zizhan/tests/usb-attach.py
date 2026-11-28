#!/usr/bin/env python3
"""Exercise USB identity filtering and stable fastboot readiness."""
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/zizhan/mac/usb-attach.py"


def run_case(product, fastboot_output, expected):
    with tempfile.TemporaryDirectory(prefix="zizhan-usb-attach-") as directory:
        root = Path(directory)
        ioreg = root / "ioreg"
        fastboot = root / "fastboot"
        ioreg.write_text('#!/bin/sh\ncat "$USB_PLIST"\n')
        fastboot.write_text("#!/bin/sh\nif [ \"$1\" = devices ]; then printf '%s\\n' \"$FASTBOOT_OUTPUT\"; else printf 'product: zizhan\\n'; fi\n")
        ioreg.chmod(0o700)
        fastboot.chmod(0o700)
        plist = root / "usb.plist"
        plist.write_bytes(plistlib.dumps([{"idVendor": 0x18d1, "idProduct": product,
                                          "IORegistryEntryID": 7}]))
        env = dict(os.environ, IOREG_BIN=str(ioreg), FASTBOOT_BIN=str(fastboot),
                   USB_PLIST=str(plist), FASTBOOT_OUTPUT=fastboot_output,
                   ZIZHAN_SRC=str(root))
        result = subprocess.run(["python3", str(SCRIPT), "--timeout", "10", "--settle", "1"],
                                env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == expected, result.stdout + result.stderr


def run_enter_second_kick():
    with tempfile.TemporaryDirectory(prefix="zizhan-usb-enter-2kick-") as directory:
        root = Path(directory)
        script_dir = root / "mac"
        script_dir.mkdir()
        local_script = script_dir / "usb-attach.py"
        local_script.write_text(SCRIPT.read_text())
        local_script.chmod(0o700)

        plist = root / "usb.plist"
        plist.write_bytes(plistlib.dumps([{"idVendor": 0x18d1, "idProduct": 0x4ee7,
                                           "IORegistryEntryID": 3}]))
        kicks_file = root / "kicks.txt"
        usbkick = script_dir / "usbkick.sh"
        usbkick.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = remember ]; then exit 0; fi\n"
            "n=$(cat \"$KICKS_FILE\" 2>/dev/null || echo 0)\n"
            "n=$((n + 1))\n"
            "printf '%s\\n' \"$n\" > \"$KICKS_FILE\"\n"
            "if [ \"$FAIL_FIRST_KICK\" = 1 ]; then exit 1; fi\n"
            "if [ \"$n\" -ge 2 ]; then cp \"$READY_PLIST\" \"$USB_PLIST\"; fi\n"
            "exit 0\n"
        )
        usbkick.chmod(0o700)

        ready_plist = root / "ready.plist"
        ready_plist.write_bytes(plistlib.dumps([{"idVendor": 0x18d1, "idProduct": 0xd00d,
                                                 "IORegistryEntryID": 9,
                                                 "USB Serial Number": "test-dev"}]))

        adb = root / "adb"
        adb.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = devices ]; then printf 'List of devices attached\\ntest-dev\\tdevice\\n'; exit 0; fi\n"
            "cp \"$EMPTY_PLIST\" \"$USB_PLIST\"\n"
            "exit 0\n"
        )
        adb.chmod(0o700)
        empty_plist = root / "empty.plist"
        empty_plist.write_bytes(plistlib.dumps([]))

        ioreg = root / "ioreg"
        ioreg.write_text('#!/bin/sh\ncat "$USB_PLIST"\n')
        ioreg.chmod(0o700)

        fastboot = root / "fastboot"
        fastboot.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = devices ]; then printf 'test-dev\\tfastboot\\n'; else printf 'product: zizhan\\n'; fi\n"
        )
        fastboot.chmod(0o700)

        env = dict(os.environ, IOREG_BIN=str(ioreg), FASTBOOT_BIN=str(fastboot),
                   ADB_BIN=str(adb), USB_PLIST=str(plist), READY_PLIST=str(ready_plist),
                   EMPTY_PLIST=str(empty_plist), KICKS_FILE=str(kicks_file),
                   ZIZHAN_SRC=str(root), ANDROID_SERIAL="test-dev")

        result = subprocess.run(
            ["python3", str(local_script), "--enter", "--initial-wait", "1",
             "--timeout", "15", "--settle", "1", "--max-kicks", "2", "--kick-interval", "1"],
            env=env, capture_output=True, text=True, timeout=25,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        assert int(kicks_file.read_text().strip()) == 2
        logs = list((root / "logs").glob("usb-attach-*.json"))
        assert len(logs) == 1
        import json
        evidence = json.loads(logs[0].read_text())
        assert evidence["status"] == "PASS" and evidence["reason"] == "READY"
        attempts = [e["attempt"] for e in evidence["events"] if e["phase"] == "recovery_begin"]
        assert attempts == [1, 2], attempts

        # Port recovery failure must fail closed immediately without a second kick.
        kicks_file.unlink()
        plist.write_bytes(plistlib.dumps([{"idVendor": 0x18d1, "idProduct": 0x4ee7,
                                           "IORegistryEntryID": 3}]))
        for old in (root / "logs").glob("usb-attach-*.json"):
            old.unlink()
        fail_res = subprocess.run(
            ["python3", str(local_script), "--enter", "--initial-wait", "1",
             "--timeout", "15", "--settle", "1", "--max-kicks", "2", "--kick-interval", "1"],
            env=dict(env, FAIL_FIRST_KICK="1"), capture_output=True, text=True, timeout=25,
        )
        assert fail_res.returncode == 2, fail_res.stdout + fail_res.stderr
        assert int(kicks_file.read_text().strip()) == 1
        fail_ev = json.loads(next((root / "logs").glob("usb-attach-*.json")).read_text())
        assert fail_ev["reason"] == "PORT_RECOVERY_FAILED"


def main():
    run_case(0x4ee7, "phone\tdevice", 2)
    run_case(0xd00d, "zizhan\tfastboot", 0)
    run_case(0xd00d, "first\tfastboot\nsecond\tfastboot", 2)
    run_enter_second_kick()
    print("PASS: USB attach requires one stable bootloader object and recovers on second bounded kick")


if __name__ == "__main__":
    main()
