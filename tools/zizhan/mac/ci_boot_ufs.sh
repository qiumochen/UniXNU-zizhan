#!/bin/bash
# Mac: Pong's full 22H374 package (prelink + UFS root windows + userland boot args) on the zizhan UEFI.
# The root image lives in a pinned file on Android /data (install_pinned.py prints the 4K block).
# usage: [CPUS=8] [PROFILE=zizhan_ipados18_gpu] [DAILY=1] ci_boot_ufs.sh NAME SHNGROOT_4K [extra kernel args...]
set -euo pipefail
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
export PATH=$SRC/venv/bin:/opt/homebrew/bin:$PATH
source $SRC/ci-env.sh
export PONG_BOOT_TEMPLATE=$SRC/out/zizhan-uefi.img
cd $SRC/UniXNU-zizhan
NAME=${1:?name}; BLK=${2:?shngroot 4k block}; shift 2
PROF=${PROFILE:-zizhan_ipados18_gpu}
# The device default includes GPU + Wi-Fi. Reduced CI builds remain explicit.
A=$(PROFILE=$PROF python3 -c "import json,os,pathlib;f=[p for p in pathlib.Path('out').rglob('PONG_FORMAT2_RESULT.json') if p.parent.name==os.environ['PROFILE']];assert len(f)==1,f;print(pathlib.Path(json.loads(f[0].read_text())['output'])/'kernel')")
OUT=$SRC/out/$NAME; rm -rf "$OUT" "$OUT.chosen-bootargs.txt"
# GPU/Wi-Fi startup follows the compiled kernel report. WIFI=0 is an explicit
# radio-off diagnostic override. Touch, audio, BT, SLPI and ADSP follow the
# verified daily profile when their local firmware inputs are present.
# Scaled machine timeouts as UniXNU-sheng's board.toml: the DEVELOPMENT kernel panics when interrupts stay masked over 500 us
# or a lock spins too long; under Gunyah on this SoC that trips on ordinary stalls (g93: 658 us in mk_timer_expire). x40 keeps
# the checks for real hangs. The interrupt-masked check still panicked at about 1.7 ms with its scale given (g96), so it only
# records (interrupt-masked-debug-mode=1, SCHED_HYGIENE_MODE_TRACE) instead of panicking.
# pong_klog_4k (klog.py setup): the kernel copies the ramoops zones to a pinned file before resetting; KLOG=0 leaves it out.
KLOGARG=$( [ "${KLOG:-1}" != 1 ] || [ ! -f "$SRC/klog.py" ] || python3 "$SRC/klog.py" arg )
# The kernel is built with E/P clusters (patches/xnu-ipados18/0008) and hangs early without cluster-type in the ADT (g97).
export PONG_ADT_CLUSTERS=${PONG_ADT_CLUSTERS:-1}
python3 - "$SRC" "$A" "$OUT" "$BLK" "$PROF" "$KLOGARG" "$@" > "$OUT.log" 2>&1 <<'PY' || { tail -20 "$OUT.log"; exit 1; }
import os, subprocess, sys
from pathlib import Path

src, art, out, blk, prof, klog = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6]
cli_extra = [x for arg in sys.argv[7:] for x in arg.split() if x]

def canonical_key(token):
    k = token.split("=", 1)[0]
    return k[1:] if k.startswith("-") and k != "-v" and not k.startswith("-apfs") else k

def set_kv(items, token):
    key = canonical_key(token)
    items[:] = [old for old in items if canonical_key(old) != key]
    items.append(token)

def parse_u32_token(token):
    if "=" not in token:
        return 1
    raw = token.split("=", 1)[1].strip()
    if not raw:
        raise SystemExit(f"Invalid empty integer boot-arg: {token}")
    neg = raw.startswith("-")
    body = raw[1:] if raw[:1] in "+-" else raw
    try:
        if body.startswith(("0x", "0X")):
            val = int(body, 16)
        elif body.startswith("0") and len(body) > 1 and body.isdigit():
            val = int(body, 8)
        else:
            val = int(body, 10)
    except ValueError as exc:
        raise SystemExit(f"Invalid integer boot-arg {token!r}: {exc}") from None
    if neg:
        val = -val
    return val & 0xFFFFFFFF

def xnu_uint_arg(items, name, default=None):
    found = default
    for tok in items:
        if canonical_key(tok) == name:
            found = parse_u32_token(tok)
    return found

args = []
if klog:
    for tok in klog.split():
        set_kv(args, tok)
for tok in (
    f"pong_shngroot_4k={blk}",
    f"pong_touch={os.environ.get('TOUCH', '1')}",
    "pong_audio=0",
    f"pong_adsp={os.environ.get('ADSP', '2')}",
    "vm_compression_limit=1048576",
    "ml-timeout-lock-panic-scale=40",
    "interrupt-masked-debug-mode=1",
):
    set_kv(args, tok)

wifi_env = os.environ.get("WIFI", "")
if wifi_env:
    set_kv(args, f"pong_wifi={wifi_env}")
tz = os.environ.get("PONG_TZ", "Asia/Shanghai")
if tz and tz != "-":
    set_kv(args, f"pong_tz={tz}")

daily = (
    os.environ.get("DAILY", "1") != "0"
    and prof == "zizhan_ipados18_gpu"
    and xnu_uint_arg(cli_extra, "zizhan_daily", 1) != 0
)
attended = os.environ.get("ZIZHAN_USER_PRESENT") == "yes" or os.environ.get("ATTENDED") == "1"

amp_wmfw = Path(os.environ.get("ZIZHAN_AMP_WMFW", str(src / "work/hardware-20261115/amp-original-inputs-v1/cs35l41-dsp1-spk-prot.wmfw")))
amp_ready = (
    os.environ.get("AUDIO", "1") != "0"
    and amp_wmfw.is_file()
    and (amp_wmfw.parent / "T-cs35l41-dsp1-spk-prot.bin").is_file()
    and (amp_wmfw.parent / "B-cs35l41-dsp1-spk-prot.bin").is_file()
)
bt_dir = Path(os.environ.get("ZIZHAN_BT_FW_DIR", str(src / "work/bt-fw")))
bt_ready = (
    os.environ.get("BT", "1") != "0"
    and wifi_env != "0"
    and (bt_dir / "hpbtfw21.tlv").is_file()
    and (bt_dir / "hpnv21g.bin").is_file()
)
sfs = Path(os.environ.get("ZIZHAN_SENSORS_FS", str(src / "work/sensors-vendor/sensors-fs.bin")))
slpi_ready = os.environ.get("SLPI", "1") != "0" and sfs.is_file()

if daily:
    if amp_ready:
        for tok in (
            f"zizhan_cal_t={os.environ.get('ZIZHAN_CAL_T', '9269')}",
            f"zizhan_cal_b={os.environ.get('ZIZHAN_CAL_B', '8771')}",
            "zizhan_audio_service=1",
            "zizhan_audio_capture=1",
            "zizhan_audio_prm=42",
        ):
            set_kv(args, tok)
        if attended:
            for tok in ("zizhan_audio_attended=1", "zizhan_user_present=1", "zizhan_amp_lvl=1"):
                set_kv(args, tok)
        else:
            set_kv(args, "zizhan_pulse_dry=1")
    if os.environ.get("HAPTICS", "1") == "0":
        set_kv(args, "zizhan_haptics=0")
    else:
        set_kv(args, "zizhan_haptics_dry=0" if attended else "zizhan_haptics_dry=1")
        if attended:
            set_kv(args, "zizhan_user_present=1")
    if bt_ready:
        set_kv(args, "pong_inj=/pong/bt.dylib:BlueTool,bluetoothd")
        set_kv(args, "zizhan_bt_probe=4")
    if slpi_ready:
        for tok in ("zizhan_slpi=4", "zizhan_sensors_all=1", "zizhan_als_autobright=1"):
            set_kv(args, tok)

for tok in cli_extra:
    set_kv(args, tok)

if not attended and os.environ.get("ZIZHAN_ATTENDED_PACKAGE") != "1":
    if (
        xnu_uint_arg(args, "zizhan_audio_attended", 0) != 0
        or xnu_uint_arg(args, "zizhan_user_present", 0) != 0
        or xnu_uint_arg(args, "zizhan_pulse_dry", 1) == 0
        or xnu_uint_arg(args, "zizhan_haptics_dry", 1) == 0
        or any(canonical_key(x) in ("zizhan_tone_x", "zizhan_pulse_hold", "zizhan_tone_repeat") for x in args)
    ):
        raise SystemExit("Refusing audible speaker or live haptics flags without ZIZHAN_USER_PRESENT=yes or ATTENDED=1")

# Keep DarwinBoot's <1024-byte command line for loader/early-XNU/kern.bootargs keys; move board zizhan_* and pong_inj
# flags into /chosen zizhan-boot-args when XNU patch 0035 is present in the compiled kernel.
# Userland keys read directly from kern.bootargs (touch.c keyboard test + bt_compat18.c overrides) stay on cmdline.
CMDLINE_ONLY_ZIZHAN = {
    "zizhan_type_url", "zizhan_type_at", "zizhan_key_ms", "zizhan_key_focus",
    "zizhan_bt_vendor", "zizhan_bt_script", "zizhan_bt_transport", "zizhan_bt_oilog", "zizhan_bt_watch",
}
has_chosen_args = os.environ.get("ARGS_BLOB", "1") != "0" and b"zizhan-boot-args" in (art / "mach.development.pong").read_bytes()
moved = []
if has_chosen_args:
    for tok in list(args):
        key = canonical_key(tok)
        if not tok.startswith("-") and ((key.startswith("zizhan_") and key not in CMDLINE_ONLY_ZIZHAN) or key == "pong_inj"):
            moved.append(tok)
            args.remove(tok)

chosen_text = " ".join(moved)
if len(chosen_text.encode("utf-8")) > 1020:
    raise SystemExit(f"/chosen zizhan-boot-args exceeds 1020-byte XNU/sysctl buffer ({len(chosen_text.encode('utf-8'))} bytes)")

audio_svc = xnu_uint_arg(args + moved, "zizhan_audio_service", 0)
audio_prm = xnu_uint_arg(args + moved, "zizhan_audio_prm", 0)
bt_probe = xnu_uint_arg(args + moved, "zizhan_bt_probe", 0)
slpi_mode = xnu_uint_arg(args + moved, "zizhan_slpi", 0)

cmd = [
    sys.executable,
    "scripts/build-ipados18-prelinked-boot.py",
    "--artifact-dir", str(art),
    "--output-dir", str(out),
    "--volume-layout", os.environ["PONG_VOLUME_LAYOUT"],
    "--cpus", os.environ.get("CPUS", "8"),
    "--identity", os.environ.get("IDENTITY", "ipad7-12"),
]
for tok in args:
    cmd += ["--kernel-arg", tok]
if amp_ready and (audio_svc > 0 or audio_prm >= 29):
    cmd += ["--cs35l41-protection", str(amp_wmfw), "--cs35l41-coefficients", str(amp_wmfw.parent)]
chosen = []
if moved:
    blob = Path(str(out) + ".chosen-bootargs.txt")
    blob.write_text(chosen_text + "\0")
    chosen.append(f"zizhan-boot-args={blob}")
if bt_ready and bt_probe >= 3:
    chosen += [f"zizhan-bt-rampatch={bt_dir / 'hpbtfw21.tlv'}", f"zizhan-bt-nvm={bt_dir / 'hpnv21g.bin'}"]
    cmd += ["--bt-node", os.environ.get("BT_NODE", "02:5a:69:7a:68:01")]
if slpi_ready and slpi_mode >= 3:
    chosen.append(f"zizhan-sensors-fs={sfs}")
for spec in chosen:
    cmd += ["--chosen-blob", spec]

subprocess.run(cmd, check=True)
PY
tail -2 "$OUT.log"
grep '^args' "$OUT/stage/EFI/UniXNU/unixnu.toml"
# DarwinBoot24 takes at most 1024 bytes of command line (BootArgs.h); a longer one fails the boot and leaves the phone at the
# UniXNU menu with no watchdog armed (g98: 1033 bytes).
LEN=$(grep '^args' "$OUT/stage/EFI/UniXNU/unixnu.toml" | sed 's/^args = "//; s/"$//' | wc -c | tr -d ' ')
echo "command line $LEN bytes"
# ALLOW_LONG_CMDLINE=1 skips the check, to test DarwinBoot24's refused-boot path (retries, logfs:\BOOTFAIL.TXT, back to fastboot).
# (LEN counts the newline; DarwinBoot24 refuses a line over 1023 characters, see BootOnce.)
[ "$LEN" -le 1020 ] || [ "${ALLOW_LONG_CMDLINE:-0}" = 1 ] || { echo "COMMAND LINE TOO LONG ($LEN bytes, DarwinBoot24 takes 1023 characters)"; exit 1; }
ls -la "$OUT/pong-xnu-initial.img"
