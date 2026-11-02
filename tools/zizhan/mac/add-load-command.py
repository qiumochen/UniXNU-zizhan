#!/usr/bin/env python3
"""Add an LC_LOAD_DYLIB followed by an optional original-entitlement re-sign."""

import argparse
import pathlib
import subprocess
import tempfile


def run(command, capture=False, check=True):
    return subprocess.run(command, check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("library")
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"missing Mach-O: {binary}")
    linked = run(["otool", "-L", str(binary)], capture=True).stdout
    verified = run(["codesign", "--verify", "--strict", "--ignore-resources", str(binary)],
                   capture=True, check=False).returncode == 0
    if args.library in linked and verified:
        print(f"{binary}: already links {args.library}")
        return

    signed = run(["codesign", "-d", "--entitlements", ":-", str(binary)], capture=True)
    entitlements = signed.stdout.strip()
    with tempfile.TemporaryDirectory(prefix="pong-load-command.") as temporary:
        entitlement_path = pathlib.Path(temporary) / "entitlements.plist"
        if entitlements:
            entitlement_path.write_text(entitlements + "\n")
        if args.library not in linked:
            run(["ipsw", "macho", "patch", "add", str(binary), "LC_LOAD_DYLIB",
                 args.library, "1.0.0", "1.0.0", "--overwrite", "--re-sign"])
        command = ["codesign", "--force", "--sign", "-", "--pagesize", "4096",
                   "--preserve-metadata=identifier,requirements,flags", "--ignore-resources"]
        if entitlements:
            command += ["--entitlements", str(entitlement_path)]
        run(command + [str(binary)])

    if args.library not in run(["otool", "-L", str(binary)], capture=True).stdout:
        raise SystemExit(f"{binary}: LC_LOAD_DYLIB verification failed")
    run(["codesign", "--verify", "--strict", "--ignore-resources", str(binary)])
    print(f"{binary}: added {args.library} and re-signed with original entitlements")


if __name__ == "__main__":
    main()
