#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rejects(operation):
    try:
        operation()
    except ValueError:
        return
    raise AssertionError("invalid capture was accepted")


def main():
    checker = load("shot_integrity", ROOT / "tools/zizhan/mac/shot-integrity.py")
    encoder = load("shot_decoder", ROOT / "tools/zizhan/mac/shot.py")
    with tempfile.TemporaryDirectory(prefix="zizhan-shot-integrity-") as temporary:
        directory = Path(temporary)
        log = directory / "fixture.txt"
        first = b"\x20\x60\xa0" + b"\0\0\0" + b"\xff\xff\xff"
        second = b"\x20\x60\xa0" + b"\0\0\0" + b"\x10\x10\x10"
        records = "\n".join(
            f"PONG SHOT {identifier} begin 3 1 at {identifier}\n"
            f"PONG SHOT {identifier} 0 0 {pixels.hex()}"
            for identifier, pixels in [(1, first), (2, second)])
        log.write_text(records + "\n")
        encoder.decode(str(log), str(directory))
        result = checker.verify(log)
        assert result["status"] == "PASS" and len(result["shots"]) == 2
        assert all(shot["nonblack_pixels"] == 2 for shot in result["shots"])
        assert result["shots"][0]["rgb_sha256"] != result["shots"][1]["rgb_sha256"]
        path = directory / "fixture.shot1.png"
        original = path.read_bytes()
        encoder.write_png(str(path), 9, 3, [bytes(27)] * 3)
        rejects(lambda: checker.verify(log))
        path.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        rejects(lambda: checker.verify(log))
        path.write_bytes(original)
        log.write_text(records + "\nPONG SHOT 1 0 0 000000\n")
        rejects(lambda: checker.verify(log))
        log.write_text("PONG SHOT 1 begin 3 1 at 1\nPONG SHOT 1 0 0 2060a0\n")
        rejects(lambda: checker.verify(log))
        log.write_text("")
        assert checker.verify(log)["status"] == "SKIP"
    print("PASS: actual PNG encoder round-trips static content and rejects corrupted, conflicting or incomplete captures")


if __name__ == "__main__":
    main()
