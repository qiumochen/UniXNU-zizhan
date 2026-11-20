#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Mac: derive a new OS trial from the installed one by editing files in its system image, install it, verify it.

usage: os-trial-edit.py NEW SNIPPET [--from TRIAL] [--full] [--activate]
       os-trial-edit.py --reinstall TRIAL [--full] [--activate]

A trial is work/userland-22H374/<name>/ (ufs-layout.json, resume-install.json, libpong-ui.dylib, wifi-service18), the unit the
round preflight (app-preflight.py, NIGHT_OS_TRIAL in work/m1-firstsound/run-daily-10.sh) verifies against the phone.
  1. clone the FROM trial's OS image (default: the active NIGHT_OS_TRIAL) into the NEW trial directory (APFS clone)
  2. mount it read-write and run SNIPPET as root with $M = the mount point (it must only touch files under $M)
  3. compare it with FROM's image in install_pinned CHUNK units; the new resume state keeps every unchanged OS chunk as done,
     so only changed chunks are written (--full writes the whole OS window, for a phone that does not hold FROM's image)
  4. install_pinned.py --only OS --resume, then app-preflight.py reads the whole installed OS back and checks its hash
  5. --activate points run-daily-10.sh's NIGHT_OS_TRIAL at NEW (later rounds verify against it)
--reinstall TRIAL writes an existing trial back (rollback). The phone must be in Android with adb; no round may be running.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

os.environ["PATH"] = "/opt/homebrew/bin:" + os.environ.get("PATH", "")   # adb, fastboot, uhubctl
ROOT = Path(os.environ.get("ZIZHAN_SRC", Path.home() / "zizhan")).resolve()
REPO = ROOT / "UniXNU-zizhan"
TRIALS = ROOT / "work/userland-22H374"
RUN_TEMPLATE = ROOT / "work/m1-firstsound/run-daily-10.sh"
sys.path.insert(0, str(REPO / "tools/zizhan/mac"))
import install_pinned as install  # noqa: E402  (CHUNK, manifest, ADB)


def log(msg):
    print(msg, flush=True)


def template_env():
    text = RUN_TEMPLATE.read_text()
    env = dict(re.findall(r"^export (NIGHT_OS_TRIAL|APP01_EXPECTED_UI_SHA256|APP01_EXPECTED_HELPER_SHA256)=(\S+)", text, re.M))
    return text, env


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(8 << 20), b""):
            h.update(block)
    return h.hexdigest()


def chunk_hashes(path, size):
    out = []
    with open(path, "rb") as f:
        for off in range(0, size, install.CHUNK):
            out.append(hashlib.sha256(f.read(min(install.CHUNK, size - off))).hexdigest())
    return out


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def guard():
    if subprocess.run(["bash", str(REPO / "tools/zizhan/mac/adb-visible.sh")], env=dict(os.environ, ZIZHAN_SRC=str(ROOT))).returncode:
        sys.exit("Android adb device required")
    ps = subprocess.check_output(["ps", "-axo", "command"], text=True)
    if any(m in ps for m in ("cycle.sh ", "install_pinned.py --", "app01-cycle-v2.py")):
        sys.exit("a device round or writer is running")


def edit_image(image, snippet):
    mount = Path(subprocess.check_output(["mktemp", "-d", str(image.parent / "os-mount.XXXXXX")], text=True).strip())
    out = subprocess.check_output(["sudo", "-n", "hdiutil", "attach", "-readwrite", "-nobrowse", "-noverify", "-owners", "on",
                                   "-mountpoint", str(mount), str(image)], text=True, stderr=subprocess.DEVNULL)
    dev = next((line.split()[0] for line in out.splitlines() if str(mount) in line), None)
    try:
        if not dev or not (mount / "System/Library").is_dir():
            sys.exit("mount failed")
        run(["sudo", "-n", "env", f"M={mount}", "bash", str(snippet)])
    finally:
        if dev:
            subprocess.run(["sudo", "-n", "hdiutil", "detach", dev], check=False, stdout=subprocess.DEVNULL)
        mount.rmdir()


def install_and_verify(trial, plan, ui_sha, helper_sha):
    layout = trial / "ufs-layout.json"
    run([sys.executable, str(REPO / "tools/zizhan/mac/install_pinned.py"), "--layout", str(layout),
         "--state", str(trial / "resume-install.json"), "--resume", "--only", "OS"], cwd=ROOT)
    state = json.loads((trial / "resume-install.json").read_text())
    state["manifest"] = True
    (trial / "resume-install.json").write_text(json.dumps(state, indent=1))
    out = TRIALS / f"{trial.name}.preflight.json"
    if out.exists():
        out.unlink()
    env = dict(os.environ, ZIZHAN_SRC=str(ROOT))
    run([sys.executable, str(REPO / "tools/zizhan/mac/app-preflight.py"), "--trial", trial.name,
         "--backup", str(ROOT.parent / "backup/2026-10-28-V14.0.15.0"), "--expected-ui", ui_sha, "--expected-helper", helper_sha,
         "--out", str(out)], env=env, cwd=ROOT)
    plan.update(installed=True)
    (trial / "prepared-only.json").write_text(json.dumps(plan, indent=2) + "\n")
    log(f"PASS: {trial.name} installed and read back")


def activate(name):
    text, env = template_env()
    if env.get("NIGHT_OS_TRIAL") == name:
        return
    RUN_TEMPLATE.write_text(re.sub(r"^export NIGHT_OS_TRIAL=\S+", f"export NIGHT_OS_TRIAL={name}", text, flags=re.M))
    log(f"run-daily-10.sh: NIGHT_OS_TRIAL={name} (was {env.get('NIGHT_OS_TRIAL')})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("new", nargs="?")
    ap.add_argument("snippet", nargs="?", type=Path)
    ap.add_argument("--from", dest="source")
    ap.add_argument("--reinstall")
    ap.add_argument("--full", action="store_true")
    ap.add_argument("--activate", action="store_true")
    a = ap.parse_args()
    _, env = template_env()
    guard()
    if a.reinstall:
        trial = TRIALS / a.reinstall
        layout = json.loads((trial / "ufs-layout.json").read_text())
        state = json.loads((trial / "resume-install.json").read_text())
        if a.full:
            state["done"] = {k: v for k, v in state["done"].items() if not k.startswith("OS:")}
        (trial / "resume-install.json").write_text(json.dumps(state, indent=1))
        plan = json.loads((trial / "prepared-only.json").read_text())
        install_and_verify(trial, plan, sha_file(trial / "libpong-ui.dylib"), sha_file(trial / "wifi-service18"))
        if a.activate:
            activate(trial.name)
        return
    if not a.new or not a.snippet or not re.fullmatch(r"[A-Za-z0-9_-]+", a.new) or not a.snippet.is_file():
        ap.error("NEW (letters, digits, _ -) and an existing SNIPPET are required")
    src = TRIALS / (a.source or env["NIGHT_OS_TRIAL"])
    trial = TRIALS / a.new
    if trial.exists():
        sys.exit(f"{trial} exists")
    layout = json.loads((src / "ufs-layout.json").read_text())
    state = json.loads((src / "resume-install.json").read_text())
    window = next(w for w in layout["windows"] if w["name"] == "OS")
    old_image, old_sha = Path(window["image"]), window["sha256"]
    if sha_file(src / "libpong-ui.dylib") != env["APP01_EXPECTED_UI_SHA256"] and not a.source:
        sys.exit("the active trial's UI library does not match run-daily-10.sh")
    trial.mkdir()
    for name in ("libpong-ui.dylib", "wifi-service18"):
        shutil.copy2(src / name, trial / name)
    image = trial / "pong-ipados18-system.cdr"
    run(["cp", "-c", str(old_image), str(image)])
    edit_image(image, a.snippet)
    log("comparing chunks")
    before, after = chunk_hashes(old_image, window["size"]), chunk_hashes(image, window["size"])
    changed = [i * install.CHUNK for i, (x, y) in enumerate(zip(before, after)) if x != y]
    new_sha = sha_file(image)
    log(f"OS image {window['sha256'][:12]} -> {new_sha[:12]}, changed chunks {len(changed)} of {len(after)}")
    window.update(image=str(image), sha256=new_sha)
    (trial / "ufs-layout.json").write_text(json.dumps(layout, indent=2) + "\n")
    done = {k: v for k, v in state["done"].items() if not k.startswith("OS:")}
    if not a.full:
        done.update({f"OS:{i * install.CHUNK}": h for i, h in enumerate(after) if i * install.CHUNK not in changed})
    state.update(done=done, manifest=False)
    (trial / "resume-install.json").write_text(json.dumps(state, indent=1))
    plan = {"from_trial": src.name, "snippet": str(a.snippet), "old_os_sha256": old_sha,
            "new_os_sha256": new_sha, "ui_sha256": env["APP01_EXPECTED_UI_SHA256"],
            "helper_sha256": env["APP01_EXPECTED_HELPER_SHA256"], "changed_chunks": changed, "full_write": a.full,
            "installed": False, "Data_modified_offline": False}
    (trial / "prepared-only.json").write_text(json.dumps(plan, indent=2) + "\n")
    install_and_verify(trial, plan, plan["ui_sha256"], plan["helper_sha256"])
    if a.activate:
        activate(trial.name)


if __name__ == "__main__":
    main()
