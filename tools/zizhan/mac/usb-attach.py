#!/usr/bin/env python3
"""Check USB readiness, or enter fastboot once with bounded port recovery."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile
import time


class Attach:
    def __init__(self, root, timeout=100, settle=3, clock=time.monotonic, pause=time.sleep):
        self.root = Path(root)
        self.clock, self.pause = clock, pause
        self.started = clock()
        self.deadline = self.started + timeout
        self.settle = settle
        self.serial = os.environ.get("ANDROID_SERIAL")
        self.events = []
        self.previous = None
        self.since = self.started
        self.reason = "NO_FASTBOOT"

    def event(self, phase, **fields):
        self.events.append(dict(seconds=round(self.clock() - self.started, 3),
                                phase=phase, **fields))

    def command(self, arguments, timeout=6):
        remaining = self.deadline - self.clock()
        if remaining <= 0:
            return None
        try:
            result = subprocess.run(arguments, capture_output=True,
                                    timeout=min(timeout, remaining))
            self.event("command", tool=Path(arguments[0]).name, arguments=arguments[1:],
                       returncode=result.returncode,
                       stdout=result.stdout.decode(errors="replace")[-16384:],
                       stderr=result.stderr.decode(errors="replace")[-16384:])
            return result
        except (OSError, subprocess.TimeoutExpired) as error:
            self.event("command", tool=Path(arguments[0]).name,
                       error=type(error).__name__)
            return None

    def usb(self):
        result = self.command([os.environ.get("IOREG_BIN", "/usr/sbin/ioreg"),
                               "-r", "-c", "IOUSBHostDevice", "-a"])
        if result is None or result.returncode:
            return None
        try:
            nodes = plistlib.loads(result.stdout)
        except (ValueError, plistlib.InvalidFileException):
            return None
        devices = []

        def visit(node):
            if isinstance(node, list):
                for child in node:
                    visit(child)
            elif isinstance(node, dict):
                if node.get("idVendor") == 0x18d1 and node.get("idProduct") == 0xd00d:
                    devices.append(node)
                visit(node.get("IORegistryEntryChildren", []))

        visit(nodes)
        if self.serial:
            devices = [node for node in devices
                       if node.get("USB Serial Number") == self.serial]
        self.event("usb", fastboot_objects=len(devices))
        if len(devices) != 1:
            return None
        node = devices[0]
        identity = (node.get("IORegistryEntryID"), node.get("locationID"),
                    node.get("USB Serial Number"))
        return identity if identity[0] is not None else None

    def ready(self):
        identity = self.usb()
        if identity is None or identity != self.previous:
            self.previous, self.since = identity, self.clock()
            return False
        if self.clock() - self.since < self.settle:
            return False
        fastboot = [os.environ.get("FASTBOOT_BIN", "fastboot")]
        result = self.command(fastboot + ["devices"])
        if result is None or result.returncode:
            return False
        devices = [parts[0] for line in result.stdout.decode(errors="replace").splitlines()
                   if len(parts := line.split()) == 2 and parts[1] == "fastboot"]
        if self.serial:
            devices = [serial for serial in devices if serial == self.serial]
        if len(devices) != 1:
            self.reason = "AMBIGUOUS_FASTBOOT"
            return False
        result = self.command(fastboot + ["-s", devices[0], "getvar", "product"])
        if result is None or result.returncode:
            return False
        text = (result.stdout + result.stderr).decode(errors="replace")
        if not re.search(r"(?:^|\n)(?:\(bootloader\)\s*)?product:\s*zizhan\s*(?:\n|$)", text):
            self.reason = "WRONG_PRODUCT"
            return False
        self.serial = devices[0]
        self.reason = "READY"
        return True

    def wait_ready(self, seconds):
        deadline = min(self.deadline, self.clock() + seconds)
        while self.clock() < deadline:
            if self.ready():
                self.event("ready")
                return True
            self.pause(max(0, min(1, deadline - self.clock())))
        return False

    def enter(self):
        if self.wait_ready(self.settle + 2):
            return True
        if self.reason in ("AMBIGUOUS_FASTBOOT", "WRONG_PRODUCT"):
            return False
        adb = [os.environ.get("ADB_BIN", "adb")]
        result = self.command(adb + ["devices"])
        if result is None or result.returncode:
            self.reason = "NO_ANDROID"
            return False
        devices = [parts[0] for line in result.stdout.decode(errors="replace").splitlines()
                   if len(parts := line.split()) == 2 and parts[1] == "device"]
        if self.serial:
            devices = [serial for serial in devices if serial == self.serial]
        if len(devices) != 1:
            self.reason = "NO_UNIQUE_ANDROID"
            return False
        self.serial = devices[0]
        result = self.command(["bash", str(self.root / "usbkick.sh"), "remember"], 10)
        self.event("recovery_port", recorded=result is not None and result.returncode == 0)
        port_recorded = result is not None and result.returncode == 0
        result = self.command(adb + ["-s", self.serial, "reboot", "bootloader"], 30)
        if result is None or result.returncode:
            self.reason = "ADB_REBOOT_FAILED"
            return False
        self.previous = None
        self.event("reboot_dispatched")
        if self.wait_ready(25):
            return True
        if self.reason in ("AMBIGUOUS_FASTBOOT", "WRONG_PRODUCT"):
            return False
        if not port_recorded:
            self.reason = "NO_RECORDED_RECOVERY_PORT"
            return False
        self.event("recovery_begin")
        result = self.command(["bash", str(self.root / "usbkick.sh"), "kick"], 15)
        if result is None or result.returncode:
            self.reason = "PORT_RECOVERY_FAILED"
            return False
        self.previous = None
        return self.wait_ready(max(0, self.deadline - self.clock()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enter", action="store_true", help="reboot Android; allow one recorded-port cycle")
    parser.add_argument("--timeout", type=float, default=100)
    parser.add_argument("--settle", type=float, default=3)
    args = parser.parse_args()
    if not 10 <= args.timeout <= 180 or not 1 <= args.settle <= 10:
        parser.error("timeout must be 10..180 seconds; settle must be 1..10 seconds")
    work = Path(os.environ.get("ZIZHAN_SRC", Path.home() / "zizhan"))
    logs = work / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix="usb-attach-", suffix=".json", dir=logs)
    attach = Attach(Path(__file__).resolve().parent, args.timeout, args.settle)
    passed = False
    try:
        passed = attach.enter() if args.enter else attach.wait_ready(args.timeout)
    finally:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(dict(status="PASS" if passed else "FAIL" if args.enter else "SKIP",
                           reason=attach.reason, entered=args.enter,
                           recorded_at=datetime.now().astimezone().isoformat(),
                           serial=attach.serial, events=attach.events), stream, indent=2)
            stream.write("\n")
    print(f"USB attach {'PASS' if passed else attach.reason}; evidence={name}")
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
