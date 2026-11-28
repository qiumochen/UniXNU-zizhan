#!/usr/bin/env python3
"""zizhan DBG-01 (`zctl.py`): Mac LAN Live Debug CLI for Xiaomi MIX Fold 2 (iPadOS 18).

Talks to `zizhand` (`backboardd` :8089 when booted with `zizhan_debugd=1`) using the
local 256-bit bearer token in `$ZIZHAN_SRC/work/.zizhan-dbg-token` (never committed).
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

REPO = Path(__file__).resolve().parents[3]
DEFAULT_SRC = Path(os.environ.get("ZIZHAN_SRC", str(REPO.parent)))


def discover_host(src: Path) -> str:
    env_host = os.environ.get("ZIZHAN_HOST", "").strip()
    if env_host:
        return env_host
    cache_file = src / "work/.zizhan-dbg-host"
    if cache_file.is_file():
        cached = cache_file.read_text(errors="ignore").strip()
        if cached and not cached.startswith(("127.", "localhost", "::1")):
            return cached
    logs_dir = src / "logs"
    if logs_dir.is_dir():
        ip_re = re.compile(r"PongWifi: runtime\b.*?\bip=(\d+\.\d+\.\d+\.\d+)")
        candidates = sorted(logs_dir.glob("*.txt"), key=lambda p: p.stat().st_mtime, reverse=True)[:12]
        for log_path in candidates:
            try:
                text = log_path.read_text(errors="ignore")
            except OSError:
                continue
            matches = ip_re.findall(text)
            if matches and matches[-1] != "0.0.0.0":
                return matches[-1]
    raise SystemExit(
        "Cannot determine phone LAN IP; pass --host <ip> or set ZIZHAN_HOST=<ip> "
        f"(or write {cache_file})"
    )


def load_token(args: argparse.Namespace, src: Path) -> str:
    env_tok = os.environ.get("ZIZHAN_DBG_TOKEN", "").strip()
    if env_tok:
        return env_tok
    tok_path = Path(args.token_file) if args.token_file else (src / "work/.zizhan-dbg-token")
    if tok_path.is_file():
        return tok_path.read_text(errors="ignore").strip()
    return ""


class ZizhanClient:
    def __init__(self, host: str, port: int, token: str, timeout: float, save_host_file: Path | None = None):
        if ":" in host and not host.startswith("["):
            h_part, p_part = host.rsplit(":", 1)
            if p_part.isdigit():
                host = h_part
                port = int(p_part)
        self.host = host
        self.port = port
        self.base = f"http://{host}:{port}"
        self.token = token
        self.timeout = timeout
        self.save_host_file = save_host_file

    def request(
        self,
        method: str,
        path: str,
        params: dict[str, object] | None = None,
        body: bytes | None = None,
        content_type: str | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        url = self.base + path
        if params:
            clean = {k: str(v) for k, v in params.items() if v is not None}
            if clean:
                url += "?" + urllib.parse.urlencode(clean)
        headers: dict[str, str] = {}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if content_type:
            headers["Content-Type"] = content_type
        if body is not None:
            headers["Content-Length"] = str(len(body))
        req = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read()
                hdrs = {k.lower(): v for k, v in resp.headers.items()}
                if (
                    self.save_host_file is not None
                    and self.host not in ("127.0.0.1", "localhost", "::1")
                    and self.port == 8089
                ):
                    try:
                        self.save_host_file.parent.mkdir(parents=True, exist_ok=True)
                        self.save_host_file.write_text(f"{self.host}:{self.port}\n")
                    except OSError:
                        pass
                return resp.status, hdrs, raw
        except urllib.error.HTTPError as exc:
            err_body = exc.read()
            msg = err_body.decode("utf-8", errors="replace").strip()
            raise SystemExit(f"HTTP {exc.code} {exc.reason} on {method} {path}: {msg}") from None
        except urllib.error.URLError as exc:
            raise SystemExit(f"Connection to {self.base}{path} failed: {exc.reason}") from None

    def json_req(
        self,
        method: str,
        path: str,
        params: dict[str, object] | None = None,
        body: bytes | None = None,
        content_type: str | None = None,
    ) -> dict[str, object]:
        _, _, raw = self.request(method, path, params=params, body=body, content_type=content_type)
        return json.loads(raw.decode("utf-8"))

    def maybe_keepalive(self, enabled: bool, seconds: int = 360) -> None:
        if not enabled:
            return
        try:
            self.request("POST", "/api/keepalive", {"seconds": seconds})
        except SystemExit:
            pass


def start_keepalive_daemon(cli: ZizhanClient, stop_ev: threading.Event, lease_s: int = 360) -> threading.Thread:
    def worker() -> None:
        cli.maybe_keepalive(True, lease_s)
        while not stop_ev.wait(60.0):
            cli.maybe_keepalive(True, lease_s)

    th = threading.Thread(target=worker, daemon=True)
    th.start()
    return th


def format_status_human(st: dict[str, object]) -> str:
    up_s = int(st.get("uptime_ms", 0)) // 1000
    lid_v = int(st.get("lid_closed", -1))
    lid_s = {0: "open", 1: "closed"}.get(lid_v, "unknown")
    ovr_v = int(st.get("hall_override", -1))
    ovr_s = {-1: "auto", 0: "forced-open", 1: "forced-closed"}.get(ovr_v, str(ovr_v))
    view_v = int(st.get("outer_active_view", 0))
    view_s = {0: "off", 1: "clock", 2: "desktop-2:1", 3: "desktop-1:1"}.get(view_v, str(view_v))
    hot_v = int(st.get("hot_ui_state", 0))
    hot_s = {0: "stock(/pong/libpong-ui18.dylib)", 1: "confirmed(hot-ui)", 2: "trial(5s-pending)"}.get(
        hot_v, str(hot_v)
    )
    return "\n".join(
        [
            f"Uptime:        {up_s}s  (klog={st.get('klog_total_bytes')} B, pmsg={st.get('pmsg_bytes')} B)",
            f"Watchdog:      rem={st.get('autoreset_rem_s')}s  pings={st.get('autoreset_pings')}",
            f"Battery:       {st.get('batt_soc_pct')}%  {st.get('batt_mv')} mV  {st.get('batt_ma')} mA  charging={st.get('batt_charging')}",
            f"Panels:        lid={lid_s} (hall={ovr_s})  outer={view_s} ({st.get('outer_w')}x{st.get('outer_h')})  "
            f"inner_pwr={st.get('inner_display_pwr')} zc_tag={st.get('zc_flipped_tag')} ({st.get('inner_w')}x{st.get('inner_h')})",
            f"Thermal:       cpu={st.get('cpu_thermal_lvl')}  gpu={st.get('gpu_thermal_lvl')}  user_present={st.get('user_present')}",
            f"Hot-UI:        {hot_s}  sb_execs={st.get('hot_ui_sb_execs')}  rollbacks={st.get('hot_ui_rollbacks')}",
        ]
    )


def compile_local_ui_dylib(src: Path) -> Path:
    out_dir = src / "work/hot-ui-build"
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / "libpong-ui.dylib"
    sdk = subprocess.check_output(
        ["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True, timeout=15
    ).strip()
    cmd = [
        "xcrun",
        "--sdk",
        "iphoneos",
        "clang",
        "-target",
        "arm64-apple-ios18.0",
        "-isysroot",
        sdk,
        "-Os",
        "-fblocks",
        "-dynamiclib",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wl,-no_fixup_chains",
        "-install_name",
        "/pong/libpong-ui18.dylib",
        "-framework",
        "CoreFoundation",
        "-framework",
        "CoreGraphics",
        "-lobjc",
        str(REPO / "userland/ui_compat18.c"),
        str(REPO / "userland/settings_compat18.c"),
        str(REPO / "userland/boot_snapshot18.c"),
        str(REPO / "userland/touch.c"),
        "-o",
        str(dst),
    ]
    subprocess.run(cmd, check=True, timeout=120)
    subprocess.run(["codesign", "--force", "--sign", "-", "--pagesize", "4096", str(dst)], check=True, timeout=30)
    return dst


def cmd_status(cli: ZizhanClient, args: argparse.Namespace) -> int:
    cli.maybe_keepalive(not args.no_keepalive)
    st = cli.json_req("GET", "/api/status")
    if args.json:
        print(json.dumps(st, indent=2))
    else:
        print(format_status_human(st))
    return 0


def cmd_log(cli: ZizhanClient, args: argparse.Namespace) -> int:
    zone = 1 if args.pmsg else 0
    since = args.since
    if since < 0:
        st = cli.json_req("GET", "/api/status")
        tot = int(st.get("pmsg_bytes" if zone == 1 else "klog_total_bytes", 0))
        since = max(0, tot - args.tail)
    stop_ev = threading.Event()
    if args.follow and not args.no_keepalive:
        start_keepalive_daemon(cli, stop_ev)
    else:
        cli.maybe_keepalive(not args.no_keepalive)
    try:
        while True:
            _, hdrs, raw = cli.request("GET", "/api/klog", {"since": since, "zone": zone, "max": 65536})
            if hdrs.get("x-zizhan-dropped") == "1":
                sys.stderr.write(f"\n[zctl: ring wrapped ahead to byte {hdrs.get('x-zizhan-start-seq')}]\n")
            if raw:
                sys.stdout.buffer.write(raw)
                sys.stdout.buffer.flush()
            next_seq = int(hdrs.get("x-zizhan-next-seq", str(since)))
            total_seq = int(hdrs.get("x-zizhan-total-seq", str(next_seq)))
            since = next_seq
            if since < total_seq:
                continue
            if not args.follow:
                break
            time.sleep(args.poll)
    except KeyboardInterrupt:
        pass
    finally:
        stop_ev.set()
    return 0


def take_one_shot(cli: ZizhanClient, panel: str, scale: int, fmt: str, quality: int, out_path: Path) -> dict[str, str]:
    _, hdrs, raw = cli.request(
        "GET",
        "/api/screenshot",
        {"panel": panel, "scale": scale, "format": fmt, "quality": quality},
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(raw)
    return hdrs


def cmd_shot(cli: ZizhanClient, args: argparse.Namespace, src: Path) -> int:
    cli.maybe_keepalive(not args.no_keepalive)
    ext = "png" if args.format == "png" else ("bgra" if args.format == "raw" else "jpg")
    if args.output:
        out_path = Path(args.output)
    else:
        ts = datetime.now().strftime("%Y%m%d-%H%M%S")
        out_path = src / "logs/shots" / f"shot-{ts}-{args.panel}.{ext}"
    t0 = time.monotonic()
    hdrs = take_one_shot(cli, args.panel, args.scale, args.format, args.quality, out_path)
    dt_ms = (time.monotonic() - t0) * 1000.0
    actual_ext = ".png" if hdrs.get("content-type", "").startswith("image/png") and out_path.suffix == ".jpg" else None
    if actual_ext and not args.output:
        new_path = out_path.with_suffix(actual_ext)
        out_path.rename(new_path)
        out_path = new_path
    p_id = hdrs.get("x-zizhan-panel", "?")
    p_name = {"1": "inner", "2": "outer"}.get(p_id, p_id)
    print(
        f"Saved {out_path} ({out_path.stat().st_size} B, panel={p_name}, "
        f"{hdrs.get('x-zizhan-width')}x{hdrs.get('x-zizhan-height')} from "
        f"{hdrs.get('x-zizhan-src-width')}x{hdrs.get('x-zizhan-src-height')}, "
        f"seq={hdrs.get('x-zizhan-seq')}, {dt_ms:.1f} ms)"
    )
    if args.open and sys.platform == "darwin":
        subprocess.run(["open", str(out_path)], check=False)
    return 0


def cmd_watch(cli: ZizhanClient, args: argparse.Namespace, src: Path) -> int:
    out_dir = Path(args.dir) if args.dir else (src / "logs/shots/watch")
    out_dir.mkdir(parents=True, exist_ok=True)
    ext = "png" if args.format == "png" else "jpg"
    stop_ev = threading.Event()
    if not args.no_keepalive:
        start_keepalive_daemon(cli, stop_ev)
    n = 0
    try:
        while args.count <= 0 or n < args.count:
            t0 = time.monotonic()
            latest = out_dir / f"latest.{ext}"
            tmp = out_dir / f".latest.tmp.{ext}"
            hdrs = take_one_shot(cli, args.panel, args.scale, args.format, args.quality, tmp)
            tmp.replace(latest)
            n += 1
            dt_ms = (time.monotonic() - t0) * 1000.0
            print(
                f"[{n}] {latest} ({latest.stat().st_size} B, "
                f"{hdrs.get('x-zizhan-width')}x{hdrs.get('x-zizhan-height')}, "
                f"seq={hdrs.get('x-zizhan-seq')}, {dt_ms:.1f} ms)"
            )
            rem = args.interval - (time.monotonic() - t0)
            if rem > 0 and (args.count <= 0 or n < args.count):
                time.sleep(rem)
    except KeyboardInterrupt:
        pass
    finally:
        stop_ev.set()
    return 0


def cmd_reload_ui(cli: ZizhanClient, args: argparse.Namespace, src: Path) -> int:
    cli.maybe_keepalive(not args.no_keepalive)
    if args.build:
        print("Compiling and 4K ad-hoc signing userland/ui_compat18.c ...")
        dylib_path = compile_local_ui_dylib(src)
    elif args.dylib:
        dylib_path = Path(args.dylib)
    else:
        cand = src / "work/m1-firstsound/build-play206-ui/libpong-ui.dylib"
        if not cand.is_file():
            raise SystemExit("Pass a dylib path or --build")
        dylib_path = cand

    payload = dylib_path.read_bytes()
    res = cli.json_req("POST", "/api/reload-ui", body=payload, content_type="application/octet-stream")
    print(f"Staged {dylib_path} ({res.get('bytes')} B, sha256={res.get('sha256')}) -> state={res.get('state')}")
    if args.no_wait:
        return 0

    st0 = cli.json_req("GET", "/api/status")
    rb0 = int(st0.get("hot_ui_rollbacks", 0))
    deadline = time.monotonic() + 14.0
    while time.monotonic() < deadline:
        time.sleep(0.6)
        st = cli.json_req("GET", "/api/status")
        cur_state = int(st.get("hot_ui_state", 0))
        cur_rb = int(st.get("hot_ui_rollbacks", 0))
        if cur_rb > rb0:
            raise SystemExit(
                f"HOT-UI ROLLED BACK AUTOMATICALLY (rollbacks={cur_rb}) — stock /pong/libpong-ui18.dylib restored!"
            )
        if cur_state == 1:
            print(f"HOT-UI CONFIRMED HEALTHY (sb_execs={st.get('hot_ui_sb_execs')}, rollbacks={cur_rb})")
            return 0
    raise SystemExit("Timed out waiting for SpringBoard 5s hot-UI confirmation (12s watchdog will auto-revert)")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Zizhan LAN Live Debug CLI")
    p.add_argument("--host", help="Phone LAN IP or IP:PORT (default: ZIZHAN_HOST or auto-discover)")
    p.add_argument("--port", type=int, default=8089, help="HTTP port (default: 8089)")
    p.add_argument("--token-file", default=None, help="Bearer token file path (or set ZIZHAN_DBG_TOKEN env)")
    p.add_argument("--timeout", type=float, default=8.0, help="Socket timeout seconds")
    p.add_argument("--no-keepalive", action="store_true", help="Do not extend autoreset watchdog lease")

    sub = p.add_subparsers(dest="cmd", required=True)

    sp_st = sub.add_parser("status", help="Show live kernel/UI/battery/watchdog status")
    sp_st.add_argument("--json", action="store_true", help="Print raw JSON")

    sp_log = sub.add_parser("log", help="Read or follow live ramoops console / pmsg")
    sp_log.add_argument("-f", "--follow", action="store_true", help="Follow new log bytes continuously")
    sp_log.add_argument("--pmsg", action="store_true", help="Read diagnostic pmsg zone instead of console")
    sp_log.add_argument("--since", type=int, default=0, help="Start byte sequence (-1 for --tail)")
    sp_log.add_argument("--tail", type=int, default=32768, help="Bytes from end when --since=-1")
    sp_log.add_argument("--poll", type=float, default=0.25, help="Follow poll interval seconds")

    sp_shot = sub.add_parser("shot", help="Capture inner (2160x1916) or outer (1080x2520) screenshot")
    sp_shot.add_argument("-o", "--output", help="Output file path")
    sp_shot.add_argument("--panel", choices=("auto", "inner", "outer"), default="auto")
    sp_shot.add_argument("--scale", type=int, choices=(1, 2, 4), default=2)
    sp_shot.add_argument("--format", choices=("jpeg", "png", "raw"), default="jpeg")
    sp_shot.add_argument("--quality", type=int, default=82)
    sp_shot.add_argument("--open", action="store_true", help="Open saved image on macOS")

    sp_w = sub.add_parser("watch", help="Continuously update latest screenshot on disk")
    sp_w.add_argument("--dir", help="Output directory")
    sp_w.add_argument("--panel", choices=("auto", "inner", "outer"), default="auto")
    sp_w.add_argument("--scale", type=int, choices=(1, 2, 4), default=2)
    sp_w.add_argument("--format", choices=("jpeg", "png"), default="jpeg")
    sp_w.add_argument("--quality", type=int, default=80)
    sp_w.add_argument("--interval", type=float, default=1.0)
    sp_w.add_argument("--count", type=int, default=0, help="Stop after N frames (0 = until Ctrl-C)")

    sp_tap = sub.add_parser("tap", help="Inject touch tap on inner or outer panel")
    sp_tap.add_argument("x", type=int)
    sp_tap.add_argument("y", type=int)
    sp_tap.add_argument("--panel", choices=("auto", "inner", "outer", "norm"), default="auto")
    sp_tap.add_argument("--ms", type=int, default=55)

    sp_sw = sub.add_parser("swipe", help="Inject touch swipe on inner or outer panel")
    sp_sw.add_argument("x0", type=int)
    sp_sw.add_argument("y0", type=int)
    sp_sw.add_argument("x1", type=int)
    sp_sw.add_argument("y1", type=int)
    sp_sw.add_argument("--panel", choices=("auto", "inner", "outer", "norm"), default="auto")
    sp_sw.add_argument("--ms", type=int, default=200)
    sp_sw.add_argument("--steps", type=int, default=12)

    sp_key = sub.add_parser("key", help="Press power, home, vol_up, or vol_down")
    sp_key.add_argument("name", choices=("power", "home", "vol_up", "vol_down"))
    sp_key.add_argument("--ms", type=int, default=80)

    sp_fold = sub.add_parser("fold", help="Override Hall lid state (closed, open, or auto)")
    sp_fold.add_argument("state", choices=("closed", "open", "auto"))

    sp_open = sub.add_parser("open", help="Open URL, bundle:<id>, pane:<name>, or wake/unlock via SpringBoard")
    sp_open.add_argument("target")

    sp_sc = sub.add_parser("sysctl", help="Read or write whitelisted diagnostic sysctl")
    sp_sc.add_argument("name")
    sp_sc.add_argument("value", nargs="?")

    sp_smp = sub.add_parser("sample", help="Trigger kernel thread stack sampler")
    sp_smp.add_argument("proc", nargs="?", default="SpringBoard")
    sp_smp.add_argument("seconds", nargs="?", type=int, default=3)

    sp_rl = sub.add_parser("reload-ui", help="Hot-reload libpong-ui18.dylib into SpringBoard with 5s auto-rollback")
    sp_rl.add_argument("dylib", nargs="?", help="Path to 4K-signed thin arm64 libpong-ui.dylib")
    sp_rl.add_argument("--build", action="store_true", help="Compile userland/ui_compat18.c locally first")
    sp_rl.add_argument("--no-wait", action="store_true", help="Return immediately without polling 5s confirmation")

    sp_rv = sub.add_parser("revert-ui", help="Revert SpringBoard to stock /pong/libpong-ui18.dylib")
    sp_rv.add_argument("--no-restart", action="store_true", help="Do not immediately SIGTERM SpringBoard")

    sp_ka = sub.add_parser("keepalive", help="Extend hardware autoreset watchdog lease (30..3600s)")
    sp_ka.add_argument("seconds", nargs="?", type=int, default=360)
    sp_ka.add_argument("--exact", action="store_true", help="Set exact lease even if shorter than current")

    sp_rb = sub.add_parser("reboot", help="Flush klog and warm-reset into fastboot or Android")
    sp_rb.add_argument("target", nargs="?", choices=("fastboot", "android"), default="fastboot")

    sp_web = sub.add_parser("web", help="Print or open the single-page Web Remote Console")
    sp_web.add_argument("--no-open", action="store_true", help="Only print URL without opening browser")

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    src = DEFAULT_SRC
    host = args.host or discover_host(src)
    token = load_token(args, src)
    save_file = None if args.host is None else (src / "work/.zizhan-dbg-host")
    cli = ZizhanClient(host, args.port, token, args.timeout, save_host_file=save_file)

    if args.cmd == "status":
        return cmd_status(cli, args)
    if args.cmd == "log":
        return cmd_log(cli, args)
    if args.cmd == "shot":
        return cmd_shot(cli, args, src)
    if args.cmd == "watch":
        return cmd_watch(cli, args, src)
    if args.cmd == "tap":
        cli.maybe_keepalive(not args.no_keepalive)
        st = cli.json_req("POST", "/api/input", {"op": "tap", "panel": args.panel, "x": args.x, "y": args.y, "ms": args.ms})
        print(f"tap ({args.x}, {args.y}) panel={args.panel} -> ok (outer_view={st.get('outer_active_view')})")
        return 0
    if args.cmd == "swipe":
        cli.maybe_keepalive(not args.no_keepalive)
        st = cli.json_req(
            "POST",
            "/api/input",
            {
                "op": "swipe",
                "panel": args.panel,
                "x0": args.x0,
                "y0": args.y0,
                "x1": args.x1,
                "y1": args.y1,
                "ms": args.ms,
                "steps": args.steps,
            },
        )
        print(f"swipe ({args.x0},{args.y0})->({args.x1},{args.y1}) panel={args.panel} -> ok")
        return 0
    if args.cmd == "key":
        cli.maybe_keepalive(not args.no_keepalive)
        cli.json_req("POST", "/api/input", {"op": "key", "name": args.name, "ms": args.ms})
        print(f"key {args.name} -> ok")
        return 0
    if args.cmd == "fold":
        cli.maybe_keepalive(not args.no_keepalive)
        st = cli.json_req("POST", "/api/input", {"op": "fold", "state": args.state})
        print(f"fold override -> {st.get('hall_override')} (lid_closed={st.get('lid_closed')})")
        return 0
    if args.cmd == "open":
        cli.maybe_keepalive(not args.no_keepalive)
        res = cli.json_req("POST", "/api/open", {"target": args.target})
        print(f"dispatched -> {res.get('dispatched')}")
        return 0
    if args.cmd == "sysctl":
        cli.maybe_keepalive(not args.no_keepalive)
        if args.value is None:
            res = cli.json_req("GET", "/api/sysctl", {"name": args.name})
        else:
            res = cli.json_req("POST", "/api/sysctl", {"name": args.name, "value": args.value})
        print(f"{res.get('name')}: {res.get('value')}")
        return 0
    if args.cmd == "sample":
        cli.maybe_keepalive(not args.no_keepalive)
        st = cli.json_req("POST", "/api/sample", {"proc": args.proc, "seconds": args.seconds})
        print(f"sampling {args.proc} for {st.get('op_result')}s (run `zctl.py log -f` to read stacks)")
        return 0
    if args.cmd == "reload-ui":
        return cmd_reload_ui(cli, args, src)
    if args.cmd == "revert-ui":
        cli.maybe_keepalive(not args.no_keepalive)
        st = cli.json_req("POST", "/api/revert-ui", {"restart": 0 if args.no_restart else 1})
        print(f"reverted -> hot_ui_state={st.get('hot_ui_state')}")
        return 0
    if args.cmd == "keepalive":
        st = cli.json_req("POST", "/api/keepalive", {"seconds": args.seconds, "exact": 1 if args.exact else 0})
        print(f"watchdog lease -> {st.get('autoreset_rem_s')}s remaining (pings={st.get('autoreset_pings')})")
        return 0
    if args.cmd == "reboot":
        res = cli.json_req("POST", "/api/reboot", {"target": args.target})
        print(f"rebooting -> {res.get('rebooting')}")
        return 0
    if args.cmd == "web":
        frag = f"#token={urllib.parse.quote(token)}" if token else ""
        url = f"{cli.base}/{frag}"
        print(f"{cli.base}/ (token passed locally via URL fragment -> sessionStorage)")
        if not args.no_open:
            webbrowser.open(url)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
