#!/usr/bin/env python3
"""Read-only Android TDM playback witness. Never starts playback or sets controls.

Run only after the user starts normal Android playback. Raw ALSA observations
and reports must stay private. A running PCM is not physical BCLK proof.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import time

REPO=Path(__file__).resolve().parents[3]
NODES=("pcm31p","pcm32p","pcm33c")


def parse_running(params,status):
    """Require actual ALSA RUNNING and complete declared negotiated fields."""
    fields=dict(re.findall(r"^([a-z_]+):[ \t]*(\S+)(?:[ \t]+[^\r\n]*)?$",params,re.MULTILINE))
    if not re.search(r"^state:\s+RUNNING\s*$",status,re.MULTILINE):
        return None
    if not all(k in fields for k in ("access","format","channels","rate")):
        return None
    try:
        channels=int(fields["channels"])
        rate=int(fields["rate"])
    except ValueError:
        return None
    if not 1<=channels<=32 or not 8000<=rate<=384000:
        return None
    return dict(access=fields["access"],format=fields["format"],channels=channels,rate=rate,
                scope="actual running PCM format;not physical clock/slots proof")


def read(adb,script):
    # script consists only of fixed read commands below; never user strings.
    result=subprocess.run([adb,"exec-out","su -c '"+script+"'"],capture_output=True,timeout=15)
    if len(result.stdout)>2*1024*1024 or len(result.stderr)>65536:
        raise ValueError("observation exceeds bound")
    return dict(returncode=result.returncode,stdout=result.stdout.decode(errors="replace"),
                stderr=result.stderr.decode(errors="replace"))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb",default="adb")
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    output=args.output.resolve()
    if output.is_relative_to(REPO) or output.exists():
        raise SystemExit("new private output path required")
    report=dict(status="FAIL",created=datetime.now(timezone.utc).isoformat(),phone_writes=False,
                playback_started=False,mixer_controls_set=False,samples=[],physical_clock_verified=False)
    try:
        identity=read(args.adb,"getprop ro.product.device; getprop sys.boot_completed")
        report["identity"]=identity
        if identity["returncode"] or identity["stdout"].splitlines()!=["zizhan","1"]:
            raise ValueError("actual Android/zizhan completed prerequisite missing")
        report["cards"]=read(args.adb,"cat /proc/asound/cards; cat /proc/asound/pcm")
        for sample in range(3):
            row=dict(observed=datetime.now(timezone.utc).isoformat(),nodes={})
            for node in NODES:
                params=read(args.adb,"cat /proc/asound/card0/"+node+"/sub0/hw_params")
                status=read(args.adb,"cat /proc/asound/card0/"+node+"/sub0/status")
                negotiated=parse_running(params["stdout"],status["stdout"]) if not params["returncode"] and not status["returncode"] else None
                row["nodes"][node]=dict(params=params,status=status,running=negotiated)
            report["samples"].append(row)
            if sample<2:time.sleep(3)
        report["active_playback"]=[dict(sample=i,node=node,negotiated=r["running"])
            for i,sample in enumerate(report["samples"]) for node,r in sample["nodes"].items()
            if node.endswith("p") and r["running"]]
        report["status"]="OBSERVED_RUNNING_ANDROID_TDM_PCM" if report["active_playback"] else "NO_RUNNING_ANDROID_TDM_PCM_OBSERVED"
    except (OSError,ValueError,subprocess.TimeoutExpired) as error:
        report.update(status="FAIL",error=str(error))
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("x") as stream:json.dump(report,stream,indent=2);stream.write("\n")
    print(report["status"])
    for row in report.get("active_playback",[]):print(row)
    return int(report["status"]!="OBSERVED_RUNNING_ANDROID_TDM_PCM")


if __name__=="__main__":
    raise SystemExit(main())
