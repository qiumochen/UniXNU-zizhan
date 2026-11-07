#!/usr/bin/env python3
"""Run zizhan thermal inventory and candidate-policy regressions without a phone."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def main():
    for name in ("thermal-inventory.py", "tsens-probe.py", "xnu-patch-format.py",
                 "wait-android.py", "bringup-policy.py", "power-transaction.py",
                 "power-query.py", "power-inventory.py", "userland-policy-audit.py",
                 "service-policy.py"):
        subprocess.run([sys.executable, str(ROOT / "tools/zizhan/tests" / name)],
                       check=True, timeout=60)


if __name__ == "__main__":
    main()
