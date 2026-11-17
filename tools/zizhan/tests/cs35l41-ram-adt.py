#!/usr/bin/env python3
"""Actual ADT encoder default/invalid input and optional original-file delivery."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("amp_adt", ROOT / "scripts/pong_adt_contract.py")
adt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adt)
with tempfile.TemporaryDirectory(prefix="zizhan-amp-adt-") as temp:
    temp = Path(temp)
    output = temp / "test.adt"
    env = dict(os.environ)
    env.pop("PONG_HARDWARE_DTB", None)
    cmd = [sys.executable, str(ROOT / "scripts/make-pong-adt-ipados18.py"),
           "--output", str(output), "--dram-size", "0x300000000", "--ufs", "--skip-image-verification"]
    subprocess.run(cmd, env=env, check=True, capture_output=True, timeout=30)
    assert "zizhan-cs35l41-protection" not in adt.parse_adt(output.read_bytes())["/chosen"]
    original = output.read_bytes()
    for size in (0, 34067, 34068, 34069):
        bad = temp / "invalid.wmfw"
        bad.write_bytes(bytes(size))
        result = subprocess.run([*cmd, "--cs35l41-protection", str(bad)],
                                env=env, capture_output=True, timeout=30)
        assert result.returncode != 0
        assert output.read_bytes() == original
    if len(sys.argv) >= 2:
        firmware = Path(sys.argv[1])
        subprocess.run([*cmd, "--cs35l41-protection", str(firmware)], env=env,
                       check=True, capture_output=True, timeout=30)
        chosen = adt.parse_adt(output.read_bytes())["/chosen"]
        assert chosen["zizhan-cs35l41-protection"] == firmware.read_bytes()
        assert "zizhan-cs35l41-prot-t" not in chosen and "zizhan-cs35l41-prot-b" not in chosen
        if len(sys.argv) == 3:
            bundle=Path(sys.argv[2])
            subprocess.run([*cmd,"--cs35l41-protection",str(firmware),"--cs35l41-coefficients",str(bundle)],
                           env=env,check=True,capture_output=True,timeout=30)
            chosen=adt.parse_adt(output.read_bytes())["/chosen"]
            for prefix in ("T","B"):
                assert chosen["zizhan-cs35l41-prot-"+prefix.lower()] == (bundle/(prefix+"-cs35l41-dsp1-spk-prot.bin")).read_bytes()
            original=output.read_bytes()
            invalid=temp/"coefficients"
            invalid.mkdir()
            for prefix,size in (("T",5060),("B",5148)):
                (invalid/(prefix+"-cs35l41-dsp1-spk-prot.bin")).write_bytes(bytes(size))
            result=subprocess.run([*cmd,"--cs35l41-protection",str(firmware),"--cs35l41-coefficients",str(invalid)],env=env,capture_output=True,timeout=30)
            assert result.returncode and output.read_bytes()==original
    result=subprocess.run([*cmd,"--cs35l41-coefficients",str(temp)],env=env,capture_output=True,timeout=30)
    assert result.returncode
print("PASS actual ADT encoder default-off/no payload,wrong size/hash rejection and supplied input delivery if requested")
