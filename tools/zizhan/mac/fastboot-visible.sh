#!/bin/bash
# Return success only when a fastboot device is visible. A missing USB device
# can make platform-tools' `fastboot devices` block indefinitely on this host.
python3 - <<'PY'
import os
import plistlib
import subprocess
import sys

# On this Mac `fastboot devices` also opens USB objects that are still
# terminating after adb reboot. Wait for the bootloader's actual USB
# identity first, so probes never open the outgoing Android device.
# This avoids that race; it does not claim to recover a failed attach.
if sys.platform == "darwin" or os.environ.get("IOREG_BIN"):
    try:
        usb = subprocess.run(
            [os.environ.get("IOREG_BIN", "/usr/sbin/ioreg"),
             "-r", "-c", "IOUSBHostDevice", "-a"],
            capture_output=True, timeout=6, check=True,
        )
        nodes = plistlib.loads(usb.stdout)
        def bootloader(node):
            if isinstance(node, list):
                return any(bootloader(child) for child in node)
            if isinstance(node, dict):
                return (node.get("idVendor") == 0x18d1 and
                        node.get("idProduct") == 0xd00d) or bootloader(
                            node.get("IORegistryEntryChildren", []))
            return False
        if not bootloader(nodes):
            sys.exit(1)
    except (subprocess.SubprocessError, OSError, plistlib.InvalidFileException, ValueError):
        sys.exit(1)

try:
    result = subprocess.run(
        [os.environ.get("FASTBOOT_BIN", "fastboot"), "devices"],
        capture_output=True, text=True, timeout=6
    )
except (subprocess.TimeoutExpired, OSError):
    sys.exit(1)
sys.exit(0 if result.returncode == 0 and any(
    "fastboot" in line.split() for line in result.stdout.splitlines()
) else 1)
PY
