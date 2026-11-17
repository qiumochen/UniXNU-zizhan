#!/usr/bin/env python3
from pathlib import Path
import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("adt_contract", ROOT / "scripts/pong_adt_contract.py")
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)

valid = {
    "tlmm-base": (0x0f000000).to_bytes(8, "little"),
    "tlmm-size": (0x200000).to_bytes(4, "little"),
    "gpio-offset": (0x100000).to_bytes(4, "little"),
    "hall-gpio": (62).to_bytes(4, "little"),
    "hall-active-low": (1).to_bytes(4, "little"),
    "hall-debounce-ms": (15).to_bytes(4, "little"),
}
assert contract.audit_zizhan_hall(valid)["register_offset"] == 0x13e004
for key, value in (("hall-gpio", 63), ("hall-active-low", 0), ("hall-debounce-ms", 0),
                   ("gpio-offset", 0x1ff000), ("tlmm-size", 0x100000)):
    invalid = dict(valid)
    invalid[key] = value.to_bytes(len(valid[key]), "little")
    try:
        contract.audit_zizhan_hall(invalid)
    except ValueError:
        continue
    raise AssertionError(f"accepted invalid Hall property {key}")
missing = dict(valid)
del missing["hall-gpio"]
try:
    contract.audit_zizhan_hall(missing)
except ValueError:
    pass
else:
    raise AssertionError("accepted missing Hall property")

# Exercise the actual package encoder, not a second copy of its properties.
with tempfile.TemporaryDirectory(prefix="zizhan-hall-adt-") as temp:
    output = Path(temp) / "test.adt"
    env = dict(os.environ)
    env.pop("PONG_HARDWARE_DTB", None)
    subprocess.run([sys.executable, str(ROOT / "scripts/make-pong-adt-ipados18.py"),
                    "--output", str(output), "--dram-size", "0x300000000", "--ufs",
                    "--skip-image-verification"], env=env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    props = contract.parse_adt(output.read_bytes())["/arm-io/pong-touch"]
    assert contract.audit_zizhan_hall(props)["gpio"] == 62
    assert props["compatible"] == b"st,fts-zizhan-pri\0"
print("PASS Hall exact resources, negative inputs and actual package ADT encoding; no MMIO")
