#!/usr/bin/env python3
"""Compile and run zizhan DBG-01 C unit tests + live loopback HTTP + zctl.py E2E under ASan/UBSan."""
from pathlib import Path
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib

REPO = Path(__file__).resolve().parents[3]
SRC_C = Path(__file__).with_suffix(".c")
ZCTL = REPO / "tools/zizhan/mac/zctl.py"


def find_cc() -> list[str]:
    for cand in ("clang", "gcc", "cc"):
        if shutil.which(cand):
            return [cand]
    raise RuntimeError("No C compiler found")


def verify_png_bytes(data: bytes) -> tuple[int, int]:
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "invalid PNG signature"
    pos = 8
    w = h = 0
    idat = bytearray()
    saw_iend = False
    while pos < len(data):
        length, ctype = struct.unpack_from(">I4s", data, pos)
        payload = data[pos + 8 : pos + 8 + length]
        crc_got = struct.unpack_from(">I", data, pos + 8 + length)[0]
        crc_exp = zlib.crc32(data[pos + 4 : pos + 8 + length]) & 0xFFFFFFFF
        assert crc_got == crc_exp, f"CRC mismatch on {ctype!r}"
        if ctype == b"IHDR":
            w, h = struct.unpack_from(">II", payload, 0)
        elif ctype == b"IDAT":
            idat.extend(payload)
        elif ctype == b"IEND":
            saw_iend = True
            break
        pos += 12 + length
    assert saw_iend and w > 0 and h > 0
    raw = zlib.decompress(bytes(idat))
    assert len(raw) == h * (1 + w * 4)
    return w, h


def main() -> None:
    cc = find_cc()
    with tempfile.TemporaryDirectory(prefix="zizhan-debugd-test-") as td:
        td_path = Path(td)
        bin_path = td_path / "zizhan-debugd-test"
        cmd = [
            *cc,
            "-std=c11",
            "-O1",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(SRC_C),
            "-lpthread",
            "-o",
            str(bin_path),
        ]
        subprocess.run(cmd, check=True, timeout=45)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")

        # 1. Pure-C unit tests
        subprocess.run([str(bin_path), "--unit"], check=True, env=env, timeout=15)

        # 2. Emit valid 4K-signed MH_DYLIB for hot-UI reload test
        dylib_path = td_path / "candidate-ui.dylib"
        subprocess.run([str(bin_path), "--emit-dylib", str(dylib_path), "-"], check=True, env=env, timeout=15)

        # 3. Boot real C HTTP server on loopback and test auth + all zctl.py commands
        token = "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"
        tok_file = td_path / "dbg.token"
        tok_file.write_text(token + "\n")
        hot_dir = td_path / "hot-ui"

        srv = subprocess.Popen(
            [str(bin_path), "--serve", token, str(hot_dir)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        try:
            assert srv.stdout is not None
            port_line = srv.stdout.readline().strip()
            assert port_line.startswith("PORT="), f"Unexpected server output: {port_line!r}"
            port = int(port_line.split("=", 1)[1])

            # 3a. Unauthenticated and wrong-token requests MUST return 401
            for hdrs in ({}, {"Authorization": "Bearer wrongtoken0123456789abcdef"}):
                req = urllib.request.Request(f"http://127.0.0.1:{port}/api/status", headers=hdrs)
                try:
                    urllib.request.urlopen(req, timeout=3)
                    raise AssertionError("Expected HTTP 401 for unauthenticated request")
                except urllib.error.HTTPError as exc:
                    assert exc.code == 401, exc.code

            zctl_env = dict(os.environ, ZIZHAN_SRC=str(td_path))

            def run_zctl(*zargs: str) -> str:
                res = subprocess.run(
                    [
                        sys.executable,
                        str(ZCTL),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(port),
                        "--token-file",
                        str(tok_file),
                        *zargs,
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                    env=zctl_env,
                    timeout=10,
                )
                return res.stdout

            # 3b. Status, keepalive, fold, touch, key, open, sample, log, pmsg
            st = json.loads(run_zctl("status", "--json"))
            assert st["batt_soc_pct"] == 84 and st["inner_w"] == 2160

            ka_out = run_zctl("keepalive", "600")
            assert "600s remaining" in ka_out

            shot_inner = td_path / "inner.png"
            run_zctl("shot", "--panel", "inner", "--scale", "2", "--format", "png", "-o", str(shot_inner))
            assert verify_png_bytes(shot_inner.read_bytes()) == (40, 30)

            run_zctl("fold", "closed")
            shot_outer = td_path / "outer.png"
            run_zctl("shot", "--panel", "auto", "--scale", "2", "--format", "png", "-o", str(shot_outer))
            assert verify_png_bytes(shot_outer.read_bytes()) == (32, 48)

            run_zctl("tap", "200", "300")
            run_zctl("swipe", "540", "2100", "540", "1500", "--panel", "outer")
            run_zctl("key", "home")
            run_zctl("open", "bundle:com.zizhan.installer")
            run_zctl("sample", "SpringBoard", "2")

            klog_txt = run_zctl("log")
            assert "BOOT_CONSOLE_LINE_1" in klog_txt
            assert "MOCK_TOUCH_OK" in klog_txt
            assert "MOCK_OPEN:bundle:com.zizhan.installer" in klog_txt
            assert "ZIZHAN_SAMPLE proc=SpringBoard" in klog_txt

            pmsg_txt = run_zctl("log", "--pmsg")
            assert "PONG DIAGNOSTIC v92" in pmsg_txt

            # 3c. Sysctl diagnostic read/write allowed, unattended speaker/haptics writes refused (HTTP 403)
            assert "kern.zizhan_cam_ctrl: 2" in run_zctl("sysctl", "kern.zizhan_cam_ctrl", "2")
            for blocked_args in (
                ("sysctl", "kern.zizhan_audio_attended", "1"),
                ("sysctl", "kern.zizhan_cam_ctrl", "0x101"),
                ("sysctl", "kern.zizhan_cam_ctrl", "0x200"),
            ):
                bad_sysctl = subprocess.run(
                    [
                        sys.executable,
                        str(ZCTL),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(port),
                        "--token-file",
                        str(tok_file),
                        *blocked_args,
                    ],
                    text=True,
                    capture_output=True,
                    env=zctl_env,
                    timeout=10,
                )
                assert bad_sysctl.returncode != 0 and "HTTP 403" in bad_sysctl.stderr, bad_sysctl.stderr
            assert not (td_path / "work/.zizhan-dbg-host").exists(), "loopback must not pollute .zizhan-dbg-host"

            # 3d. Hot-UI invalid upload rejected (400), valid 4K dylib staged -> 120ms watchdog reverts unconfirmed trial!
            bad_file = td_path / "garbage.dylib"
            bad_file.write_bytes(b"not-a-mach-o" * 20)
            bad_rl = subprocess.run(
                [
                    sys.executable,
                    str(ZCTL),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(port),
                    "--token-file",
                    str(tok_file),
                    "reload-ui",
                    str(bad_file),
                    "--no-wait",
                ],
                text=True,
                capture_output=True,
                env=zctl_env,
                timeout=10,
            )
            assert bad_rl.returncode != 0 and "HTTP 400" in bad_rl.stderr

            rl_out = run_zctl("reload-ui", str(dylib_path), "--no-wait")
            assert "state=trial" in rl_out
            assert (hot_dir / "libpong-ui18.dylib").is_file()
            time.sleep(0.22)  # let 120ms host-mode watchdog auto-revert unconfirmed trial
            st_after = json.loads(run_zctl("status", "--json"))
            assert st_after["hot_ui_state"] == 0, st_after

            # 3e. Web console HTML served with valid token
            _, _, html = urllib.request.urlopen(
                urllib.request.Request(
                    f"http://127.0.0.1:{port}/?token={token}",
                ),
                timeout=5,
            ).getcode(), {}, b""
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/?token={token}", timeout=5) as wresp:
                assert b"Zizhan Live Debug Console" in wresp.read()

            # 3f. Clean remote reboot shuts down server cleanly
            rb_out = run_zctl("reboot", "android")
            assert "android" in rb_out

            rc = srv.wait(timeout=5)
            assert rc == 0, f"Server exited with {rc}: {srv.stderr.read() if srv.stderr else ''}"
        finally:
            if srv.poll() is None:
                srv.kill()
                srv.wait(timeout=3)
    print("zizhan-debugd host E2E suite: PASS")


if __name__ == "__main__":
    main()
