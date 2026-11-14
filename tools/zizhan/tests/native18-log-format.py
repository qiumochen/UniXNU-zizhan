#!/usr/bin/env python3
"""Replay real stock/native formatters through their actual buffer callbacks.

This tests computation and vararg transport, not logging sinks or hardware.
No callback is replaced with a host success stub; console writes are forbidden.
Results are review material and deliberately authorize no import bindings.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys


def vectors():
    rows = []
    def add(fmt, args, expected, radix=16):
        rows.append({'format': fmt, 'args': args, 'expected': expected, 'radix': radix})
    add('', [], '')
    add('plain text', [], 'plain text')
    add('%% %%', [], '% %')
    add('a\nb\tc', [], 'a\nb\tc')
    for text in ('', 'hello', 'Pong/zizhan', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
        add('%s', [text], text)
        add('[%32s]', [text], '[' + text.rjust(32) + ']')
        add('[%-32s]', [text], '[' + text.ljust(32) + ']')
        add('%.3s', [text], text[:3])
    for value in (0, 1, 31, 32, 65, 126, 127, 128, 255):
        add('%c', [value], chr(value))
    rng = random.Random(0x22f04a7)
    numbers = [0, 1, 7, 8, 15, 16, 255, 256, 32767, 32768, 65535, 65536,
               0x7fffffff, 0x80000000, 0xffffffff, 0x7fffffffffffffff,
               0x8000000000000000, 0xffffffffffffffff] + [rng.getrandbits(64) for _ in range(32)]
    for value in numbers:
        u32 = value & 0xffffffff
        i32 = u32 if u32 < 0x80000000 else u32 - 0x100000000
        i64 = value if value < 0x8000000000000000 else value - (1 << 64)
        for fmt, expected in (('%d', str(i32)), ('%u', str(u32)), ('%x', format(u32, 'x')),
                              ('%X', format(u32, 'X')), ('%o', format(u32, 'o')),
                              ('%lld', str(i64)), ('%llu', str(value)), ('%llx', format(value, 'x')),
                              ('%llX', format(value, 'X')), ('%llo', format(value, 'o')),
                              ('%020llu', str(value).zfill(20)), ('%+d', format(i32, '+d')),
                              ('% d', format(i32, ' d')), ('%-24llu', str(value).ljust(24)),
                              ('%#llx', ('0x' if value else '') + format(value, 'x')),
                              ('%#llo', ('0' if value else '') + format(value, 'o'))):
            add(fmt, [value], expected)
    for value in (0, 1, 0x1234, 0xffffffff, 0x0000ffffffffffff):
        add('%p', [value], ('0x' if value else '') + format(value, 'x'))
    add('%s:%d:%#llx', ['hello', 0xffffffff, 0x1234], 'hello:-1:0x1234')
    add('%d/%d/%d/%d/%d/%d/%d/%d', list(range(8)), '/'.join(str(x) for x in range(8)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'qualified-map', 'kdk-research', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    if args.out.exists() or args.out.resolve() == root or root in args.out.resolve().parents:
        parser.error('Use a new private output directory')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    sys.path.insert(0, str(root / 'scripts'))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    raw = [args.stock_kernel.read_bytes(), args.native_kernel.read_bytes()]
    hashes = [hashlib.sha256(blob).hexdigest() for blob in raw]
    cache, native = Image(raw[0]), Image(raw[1])
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    names = {}
    for address, values in native.symbols().items():
        for name in values:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Require unique actual native symbol: ' + name)
        return next(iter(names[name]))
    def body(image, address):
        end = min(value for value in image.function_starts() if value > address)
        return image.read(address, end - address)
    qualified = json.loads(args.qualified_map.read_text())
    if qualified.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or Path(qualified['original']).resolve() != args.stock_kernel.resolve() or Path(qualified['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require current independent actual-input callback qualification')
    callback = named('_console_printbuf_putc')
    stock_callbacks = [int(address, 16) for address, row in qualified['functions'].items()
                       if row['name'] == '_console_printbuf_putc' and int(row['native'], 16) == callback]
    if len(stock_callbacks) != 1:
        raise RuntimeError('Actual buffer callback is not independently qualified')
    research = json.loads(args.kdk_research.read_text())
    if research.get('reference_role') != 'names-only-research' or research['inputs'][0]['sha256'] != hashes[0]:
        parser.error('Require actual-stock KDK naming research')
    callees = []
    for name in ('_IOLog', '_printf'):
        leads = [int(row['original'], 16) for row in research['functions'] if name in row['names']]
        if len(leads) != 1:
            raise RuntimeError('Ambiguous outer logging wrapper lead')
        calls, refs, _ = scan(body(original, leads[0]), leads[0])
        if stock_callbacks[0] not in {address for _, address, _ in refs}:
            raise RuntimeError('Outer wrapper does not prepare the qualified real callback')
        callees.append({address for _, address, _ in calls})
    formatter_candidates = []
    for address in callees[0] & callees[1]:
        code = body(original, address)
        _, refs, _ = scan(code, address)
        literals = {original.cstring(value) for _, value, _ in refs}
        if 1024 <= len(code) <= 8192 and '0123456789abcdef0123456789ABCDEF' in literals:
            formatter_candidates.append(address)
    if len(formatter_candidates) != 1:
        raise RuntimeError('No unique common complete formatter/table implementation')
    entries = [formatter_candidates[0], named('___doprnt')]
    callbacks = [stock_callbacks[0], callback]
    # Both wrappers also share their real console_write target. Any entry
    # into that sink is a failed prerequisite, not an emulated successful IO.
    native_sink = named('_console_write')
    callback_body = body(original, callbacks[0])
    callback_calls, _, _ = scan(callback_body, callbacks[0])
    stock_sinks = {address for _, address, _ in callback_calls
                   if not callbacks[0] <= address < callbacks[0] + len(callback_body)}
    if len(stock_sinks) != 1 or not stock_sinks <= callees[0] & callees[1]:
        raise RuntimeError('Buffer callback/outer wrapper console sink disagrees')
    sinks = [next(iter(stock_sinks)), native_sink]
    machines = [KernelReplay(original), KernelReplay(native)]
    for machine, sink in zip(machines, sinks):
        def refuse_sink(uc, pc, size, _, forbidden=sink):
            if pc == forbidden:
                raise RuntimeError('FORMAT_REPLAY_FORBIDS_REAL_CONSOLE_WRITE')
        machine.uc.hook_add(machine.unicorn.UC_HOOK_CODE, refuse_sink)
    args.out.mkdir(parents=True)
    report = {'observed_at': datetime.now().astimezone().isoformat(), 'result': 'RUNNING',
              'stock_sha256': hashes[0], 'native_sha256': hashes[1],
              'formatter_addresses': [hex(value) for value in entries],
              'formatter_body_sha256': [hashlib.sha256(body(image, address)).hexdigest()
                                        for image, address in zip((original, native), entries)],
              'qualified_map_sha256': hashlib.sha256(args.qualified_map.read_bytes()).hexdigest(),
              'binding_authorized': False, 'functions': [], 'device_access': False,
              'scope': 'actual formatter/actual bounded-buffer callback computation; no logger, console IO or hardware qualification',
              'cases': []}
    record = args.out / 'result.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    saved_rng = random.Random(0x22a8164)
    try:
        for index, row in enumerate(vectors()):
            captures = []
            for machine, entry, target in zip(machines, entries, callbacks):
                fmt, argv, output, strings = 0x50a0000, 0x50a1000, 0x50b0000, 0x50c0000
                machine.uc.mem_write(fmt, row['format'].encode('latin-1') + b'\0')
                values = []
                for argument in row['args']:
                    if isinstance(argument, str):
                        machine.uc.mem_write(strings, argument.encode('latin-1') + b'\0')
                        values.append(strings)
                        strings += 512
                    else:
                        values.append(argument & ((1 << 64) - 1))
                machine.uc.mem_write(argv, struct.pack('<' + 'Q' * len(values), *values))
                # Deliberately no newline flush; real output callback still
                # executes and maintains position, total, terminator and text.
                machine.uc.mem_write(output, bytes(0x110))
                preserved = {number: saved_rng.getrandbits(64) for number in range(19, 30)}
                result = machine.run(entry, [fmt, argv, target, output, row['radix'], 0], preserved)
                pos, total, flags = struct.unpack('<3I', machine.uc.mem_read(output, 12))
                if pos > 255:
                    raise RuntimeError('Actual buffer position exceeds fixture capacity')
                text = bytes(machine.uc.mem_read(output + 12, pos + 1))
                expected = row['expected'].encode('latin-1')
                if text != expected + b'\0' or result != len(expected) or total != result or pos != result or flags or any(machine.uc.reg_read(machine.registers[number]) != value for number, value in preserved.items()) or machine.uc.reg_read(machine.reg.UC_ARM64_REG_SP) != machine.stack:
                    raise RuntimeError('Actual format/output/count/object/ABI mismatch: ' + json.dumps({'case': row, 'actual_hex': text.hex(), 'return': result, 'pos': pos, 'total': total, 'side': len(captures)}))
                captures.append({'text_hex': text.hex(), 'return': result, 'instructions': machine.count})
            report['cases'].append({'index': index, **row, 'captures': captures, 'result': 'PASS'})
        # Positive guard control: actual callback on newline with flushing
        # enabled must reach the forbidden sink, rather than report success.
        for machine, target in zip(machines, callbacks):
            output = 0x50b0000
            machine.uc.mem_write(output, struct.pack('<3I', 0, 0, 1) + bytes(0x104))
            try:
                machine.run(target, [10, output])
            except RuntimeError as error:
                if 'FORMAT_REPLAY_FORBIDS_REAL_CONSOLE_WRITE' not in str(error):
                    raise
            else:
                raise RuntimeError('Real console-write guard did not trigger')
        report.update(result='PASS', case_count=len(report['cases']), sink_guard_control='PASS')
    except Exception as error:
        report.update(result='FAIL', error=str(error), case_count=len(report['cases']))
        record.write_text(json.dumps(report, indent=2) + '\n')
        raise
    record.write_text(json.dumps(report, indent=2) + '\n')
    print('Actual formatters/buffer callbacks:', report['case_count'], 'cases PASS; real console writes refused; zero bindings authorized')


if __name__ == '__main__':
    main()
