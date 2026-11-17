#!/usr/bin/env python3
"""Add an LC_LOAD_DYLIB followed by an optional original-entitlement re-sign."""

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile


def run(command, capture=False, check=True):
    return subprocess.run(command, check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)


def payload_ranges(data):
    if len(data) < 32 or struct.unpack_from('<I', data)[0] != 0xfeedfacf:
        raise ValueError('Require a thin little-endian 64-bit Mach-O')
    count, size = struct.unpack_from('<II', data, 16)
    end, at, ranges = 32 + size, 32, []
    if end > len(data) or count > 4096:
        raise ValueError('Invalid Mach-O command extent')
    for _ in range(count):
        if at + 8 > end:
            raise ValueError('Truncated Mach-O command')
        kind, length = struct.unpack_from('<II', data, at)
        if length < 8 or length % 8 or at + length > end:
            raise ValueError('Invalid Mach-O command size')
        if kind == 0x19:
            if length < 72:
                raise ValueError('Truncated segment command')
            sections = struct.unpack_from('<I', data, at + 64)[0]
            if length < 72 + sections * 80:
                raise ValueError('Truncated section table')
            for i in range(sections):
                section = at + 72 + i * 80
                size = struct.unpack_from('<Q', data, section + 40)[0]
                offset = struct.unpack_from('<I', data, section + 48)[0]
                kind = struct.unpack_from('<I', data, section + 64)[0] & 0xff
                if size and kind not in (1, 0xc, 0x12):  # zero-fill sections have no file bytes
                    if not offset or offset + size > len(data):
                        raise ValueError('Invalid file-backed section')
                    ranges.append((offset, size))
        elif kind == 0x80000028:
            if length < 24:
                raise ValueError('Truncated entry command')
            entry = struct.unpack_from('<Q', data, at + 8)[0]
            if not entry or entry + 4 > len(data):
                raise ValueError('Invalid entry offset')
            ranges.append((entry, 4))
        at += length
    if at != end or not ranges:
        raise ValueError('Unproven Mach-O header padding')
    if min(offset for offset, _ in ranges) < end:
        raise ValueError('Existing load commands overlap executable payload')
    return end, ranges


def require_padding(data, library):
    end, ranges = payload_ranges(data)
    needed = (24 + len(library.encode()) + 1 + 7) & ~7
    first = min(offset for offset, _ in ranges)
    if end + needed > first or any(data[end:end + needed]):
        raise ValueError('Insufficient proven header padding; adding a command would overwrite payload')
    return ranges


def reclaim_metadata(data):
    """Explicitly drop only optional source/function/data annotation commands.

    Keep UUID, platform, entry, all segments/sections, imports, exports, fixups
    and signature commands. Original payload offsets and bytes never move.
    This is opt-in for a reviewed diagnostic candidate, not a silent fallback.
    """
    end, _ranges = payload_ranges(data)
    count = struct.unpack_from('<I', data, 16)[0]
    at, commands, removed = 32, [], []
    for _ in range(count):
        kind, size = struct.unpack_from('<II', data, at)
        if kind in (0x2a, 0x26, 0x29):
            removed.append(kind)
        else:
            commands.append(data[at:at + size])
        at += size
    result = bytearray(data)
    joined = b''.join(commands)
    result[32:end] = joined + bytes(end - 32 - len(joined))
    struct.pack_into('<II', result, 16, len(commands), len(joined))
    payload_ranges(result)
    return bytes(result), removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("library")
    parser.add_argument('--reclaim-metadata', action='store_true',
                        help='Explicit diagnostic option: drop optional source/function/data annotations to free command padding')
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"missing Mach-O: {binary}")
    linked = run(["otool", "-L", str(binary)], capture=True).stdout
    verify_command = ["codesign", "--verify", "--strict"]
    if binary.parent.suffix != '.app':
        verify_command += ["--ignore-resources"]
    verified = run(verify_command + [str(binary)],
                   capture=True, check=False).returncode == 0
    if args.library in linked and verified:
        payload_ranges(binary.read_bytes())
        print(f"{binary}: already links {args.library}")
        return

    before = binary.read_bytes()
    patch_input = before
    try:
        if args.reclaim_metadata and args.library not in linked:
            patch_input, removed = reclaim_metadata(before)
            print('Explicitly reclaimed optional metadata commands: ' + ','.join(hex(value) for value in removed))
        ranges = require_padding(patch_input, args.library) if args.library not in linked else payload_ranges(before)[1]
    except ValueError as error:
        raise SystemExit(f'{binary}: refusing unchanged input: {error}')

    signed = run(["codesign", "-d", "--entitlements", ":-", str(binary)], capture=True)
    entitlements = signed.stdout.strip()
    temporary_parent = binary.parent.parent if binary.parent.suffix == '.app' else binary.parent
    with tempfile.TemporaryDirectory(prefix=".pong-load-command.", dir=temporary_parent) as temporary:
        candidate = pathlib.Path(temporary) / binary.name
        shutil.copy2(binary, candidate)
        if patch_input != before:
            candidate.write_bytes(patch_input)
        entitlement_path = pathlib.Path(temporary) / "entitlements.plist"
        if entitlements:
            entitlement_path.write_text(entitlements + "\n")
        if args.library not in linked:
            run(["ipsw", "macho", "patch", "add", str(candidate), "LC_LOAD_DYLIB",
                 args.library, "1.0.0", "1.0.0", "--overwrite", "--re-sign"])
        command = ["codesign", "--force", "--sign", "-", "--pagesize", "4096",
                   "--preserve-metadata=identifier,requirements,flags", "--ignore-resources"]
        if entitlements:
            command += ["--entitlements", str(entitlement_path)]
        run(command + [str(candidate)])
        after = candidate.read_bytes()
        payload_ranges(after)
        for offset, length in ranges:
            if after[offset:offset + length] != before[offset:offset + length]:
                raise SystemExit('Refusing patched executable: original section/entry bytes changed')
        if args.library not in run(["otool", "-L", str(candidate)], capture=True).stdout:
            raise SystemExit(f"{binary}: LC_LOAD_DYLIB verification failed")
        run(["codesign", "--verify", "--strict", "--ignore-resources", str(candidate)])
        os.replace(candidate, binary)
        if binary.parent.suffix == '.app':
            # A detached executable has no Info.plist/resource context. Bind
            # the final signature to the real bundle and retain a rollback
            # copy until both signature and original payload are checked.
            resources = binary.parent / '_CodeSignature/CodeResources'
            original_resources = resources.read_bytes() if resources.exists() else None
            try:
                bundle_command = ["codesign", "--force", "--sign", "-", "--pagesize", "4096",
                                  "--preserve-metadata=identifier,requirements,flags"]
                if entitlements:
                    bundle_command += ["--entitlements", str(entitlement_path)]
                run(bundle_command + [str(binary.parent)])
                run(["codesign", "--verify", "--strict", str(binary)])
                final = binary.read_bytes()
                for offset, length in ranges:
                    if final[offset:offset + length] != before[offset:offset + length]:
                        raise ValueError('Bundle signing changed executable payload')
            except BaseException:
                binary.write_bytes(before)
                if original_resources is None:
                    resources.unlink(missing_ok=True)
                else:
                    resources.write_bytes(original_resources)
                raise
    print(f"{binary}: added {args.library} and re-signed with original entitlements")


if __name__ == "__main__":
    main()
