#!/usr/bin/env python3
"""Bounded read-only zizhan Android completion witness after a device cycle."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import time


def wait_android(adb="adb", timeout=120, interval=3, *, run=subprocess.run,
                 clock=time.monotonic, sleep=time.sleep, report=lambda _: None):
    if not math.isfinite(timeout) or not 0 < timeout <= 300:
        raise ValueError("timeout must be finite and within (0, 300]")
    if not math.isfinite(interval) or not 0 < interval <= timeout:
        raise ValueError("interval must be finite, positive and <= timeout")
    started = clock()
    deadline = started + timeout
    result = dict(status="RUNNING", timeout_seconds=timeout, attempts=0,
                  started_at=datetime.now(timezone.utc).isoformat(), observations=[])

    def command(*args):
        remaining = deadline - clock()
        if remaining <= 0:
            return None
        try:
            p = run([adb, *args], capture_output=True, text=True,
                    timeout=min(6, remaining))
            if p.returncode == 0 and clock() < deadline:
                return p.stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
        return None

    while clock() < deadline:
        result["attempts"] += 1
        observation = dict(state=command("get-state"))
        if observation["state"] == "device":
            product = command("shell", "getprop", "ro.product.device")
            observation["product"] = product
            if product and product != "zizhan":
                result.update(status="FAIL", reason="unexpected-product")
            elif product == "zizhan":
                complete = command("shell", "getprop", "sys.boot_completed")
                observation["boot_completed"] = complete
                if complete == "1":
                    result["status"] = "PASS"
        result["observations"].append(observation)
        result["elapsed_seconds"] = round(clock() - started, 3)
        report(result)
        if result["status"] != "RUNNING":
            break
        remaining = deadline - clock()
        if remaining > 0:
            sleep(min(interval, remaining))
    if result["status"] == "RUNNING":
        result.update(status="FAIL", reason="android-completion-timeout")
    result["elapsed_seconds"] = round(clock() - started, 3)
    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    report(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--interval", type=float, default=3)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error("existing return witness retained; choose a new output")

    def report(result):
        if args.output:
            temporary = args.output.with_suffix(args.output.suffix + ".tmp")
            temporary.write_text(json.dumps(result, indent=2) + "\n")
            temporary.replace(args.output)

    try:
        result = wait_android(args.adb, args.timeout, args.interval, report=report)
    except ValueError as error:
        parser.error(str(error))
    print("WAIT_ANDROID: {} attempts={} elapsed={} reason={}".format(
        result["status"], result["attempts"], result["elapsed_seconds"],
        result.get("reason", "boot_completed=1 product=zizhan")))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
