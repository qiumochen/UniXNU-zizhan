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


def main():
    run_case(0x4ee7, "phone\tdevice", 2)
    run_case(0xd00d, "zizhan\tfastboot", 0)
    run_case(0xd00d, "first\tfastboot\nsecond\tfastboot", 2)
    print("PASS: USB attach requires one stable bootloader object and fastboot identity")


if __name__ == "__main__":
    main()
