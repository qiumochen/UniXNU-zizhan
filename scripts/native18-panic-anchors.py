#!/usr/bin/env python3
"""Qualify the real panic and stack-protector fatal ABI without running panic.

The complete variadic panic wrapper builds the original caller's va_list,
preserves format/LR and clears all optional flags, then unconditionally calls
its actual platform fatal trap. Distinct self-identifying fatal wrappers
must agree on that endpoint. Host replay stops before the real trap; no
panic is replaced, returned from, suppressed or triggered on a phone.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain previous fatal-ABI evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Actual source build identity required')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Fatal API/source definition not unique: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:]) if last - first <= 256}
    old, new = bodies(original), bodies(native)
    def panic_contract(image, address, code):
        if len(code) != 60:
            return None
        words = list(struct.unpack('<15I', code))
        calls, refs, shape = scan(code, address)
        if refs or len(calls) != 1 or not calls[0][2] or calls[0][0] != address + 56:
            return None
        register = words[4] & 31
        expected = [0xd503237f, 0xd10083ff, 0xa9017bfd, 0x910043fd,
                    0x910043a0 | register, 0xf90007e0 | register,
                    0xaa1e03e6, 0xdac143e6, 0x910023e1, 0x52800002,
                    0xd2800003, 0xd2800004, 0xd2800005, 0xd2800007, words[14]]
        if words != expected or not 8 <= register <= 15:
            return None
        return calls[0][1]
    panic_candidates = [(address, target) for address, code in old.items()
                        if (target := panic_contract(original, address, code)) is not None]
    source_panic = named('_panic')
    source_trap = panic_contract(native, source_panic, new.get(source_panic, b''))
    if len(panic_candidates) != 1 or source_trap != named('_panic_trap_to_debugger'):
        raise RuntimeError('Complete actual variadic fatal-trap wrapper is ambiguous/refused')
    old_panic, old_trap = panic_candidates[0]
    fatal_sources = {}
    checker = []
    for address, code in old.items():
        calls, refs, _ = scan(code, address)
        if len(calls) != 1 or not calls[0][2] or calls[0][1] != old_panic:
            continue
        literals = {original.cstring(value) for _, value, _ in refs}
        for name in ('pmap_claim_reserved_ppl_page', 'pmap_free_reserved_ppl_page'):
            if {name, 'pmap.c', '%s: function not supported in this environment @%s:%d'} <= literals and calls[0][0] + 4 == address + len(code):
                fatal_sources[name] = address
        if {'stack_protector.c', 'Kernel stack memory corruption detected @%s:%d'} <= literals:
            trimmed = code[:-4] if code[-4:] == bytes(4) else code
            call_rows, _, shape = scan(trimmed, address)
            expected = [0xd503237f, 0xd10083ff, 0xa9017bfd, 0x910043fd,
                        0x90000008, 0x91000108, 0x528004a9, 0xa90027e8,
                        0x90000000, 0x91000000, 0x94000000]
            if list(shape) == expected and len(call_rows) == 1 and call_rows[0][0] == address + len(trimmed) - 4:
                checker.append((address, trimmed))
    if len(fatal_sources) != 2 or len(checker) != 1:
        raise RuntimeError('Actual independent self-identifying fatal consumers do not corroborate panic endpoint')
    source_checker = named('___stack_chk_fail')
    check_code = new.get(source_checker, b'')
    calls, refs, shape = scan(check_code, source_checker)
    if len(calls) != 1 or calls[0][1] != source_panic or calls[0][0] + 4 != source_checker + len(check_code) or list(shape) != expected:
        raise RuntimeError('Actual source stack corruption path is not the same complete fatal contract')
    if {native.cstring(value) for _, value, _ in refs} != {'stack_protector.c', 'Kernel stack memory corruption detected @%s:%d'}:
        raise RuntimeError('Actual source stack corruption format/file metadata changed')
    left, right = KernelReplay(original), KernelReplay(native)
    rng = random.Random(0x22fa7a1)
    captured = []
    for machine, entry, trap in ((left, old_panic, old_trap), (right, source_panic, source_trap)):
        captures = []
        def stop_before_trap(uc, pc, size, _):
            if pc == trap:
                arguments = [uc.reg_read(machine.registers[index]) for index in range(8)]
                va_address = struct.unpack('<Q', uc.mem_read(arguments[1], 8))[0]
                captures.append({'format': arguments[0], 'flags': arguments[2:6], 'caller': arguments[6],
                                 'panic_data': arguments[7], 'varargs': bytes(uc.mem_read(va_address, 64)).hex(),
                                 'va_is_original_caller_stack': va_address == machine.stack})
                uc.emu_stop()
        hook = machine.uc.hook_add(machine.unicorn.UC_HOOK_CODE, stop_before_trap)
        rng.seed(0x22fa7a1)
        for _ in range(64):
            stack_bytes = bytes(rng.getrandbits(8) for _ in range(64))
            machine.uc.mem_write(machine.stack, stack_bytes)
            for register in machine.registers:
                machine.uc.reg_write(register, 0)
            fmt = 0x50a0000 + rng.randrange(1024)
            machine.uc.reg_write(machine.registers[0], fmt)
            machine.uc.reg_write(machine.reg.UC_ARM64_REG_X30, machine.return_address)
            machine.uc.reg_write(machine.reg.UC_ARM64_REG_SP, machine.stack)
            machine.count = 0
            machine.uc.emu_start(entry, machine.return_address, count=100)
            if not captures or captures[-1] != {'format': fmt, 'flags': [0, 0, 0, 0],
                                               'caller': machine.return_address, 'panic_data': 0,
                                               'varargs': stack_bytes.hex(), 'va_is_original_caller_stack': True}:
                raise RuntimeError('Actual panic wrapper changes format/va_list/caller/flags ABI')
        machine.uc.hook_del(hook)
        captured.append(captures)
    if captured[0] != captured[1]:
        raise RuntimeError('Actual stock/native fatal API argument captures disagree')
    rows = [{'original': hex(old_panic), 'name': '_panic', 'final_import_only': True,
             'evidence': 'Unique complete variadic panic frame/va_list/caller/zero-option preparation, corroborated by distinct self-identifying fatal consumers and stack-corruption wrapper;64 actual stock/source instruction ABI captures agree at real fatal-trap boundary; trap is not emulated/returned/suppressed',
             'original_sha256': hashlib.sha256(old[old_panic]).hexdigest(), 'native_sha256': hashlib.sha256(new[source_panic]).hexdigest()},
            {'original': hex(checker[0][0]), 'name': '___stack_chk_fail', 'final_import_only': True,
             'evidence': 'Complete actual stack_protector.c fatal wrapper with identical file/line and corruption format calls the independently reviewed real panic and has no return path; only unreachable stock UDF padding differs',
             'original_sha256': hashlib.sha256(checker[0][1]).hexdigest(), 'native_sha256': hashlib.sha256(check_code).hexdigest()}]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': rows, 'data': [], 'bootable': False,
                                   'replay': {'result': 'PASS', 'argument_cases': 64, 'scope': 'actual code preparation only; host stops before genuine fatal trap',
                                              'authentication': 'host PAC abstraction only', 'phone_panic_triggered': False}}, indent=2) + '\n')
    print('Actual panic/stack-corruption fatal ABI qualified;64 argument captures; genuine trap never executed or suppressed')


if __name__ == '__main__':
    main()
