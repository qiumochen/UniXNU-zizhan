#!/usr/bin/env python3
"""Qualify bounded clock KPIs by concrete code and real-instruction replay.

The Apple non-speculative counter is implemented on SM8475 by ISB+CNTVCT;
these are explicit platform implementations, not identical raw sysregs. Two
deadline wrappers are checked against complete stock instruction templates,
their concrete 32-bit frequency operand, source helpers and integer saturation
contracts. Unicorn executes the actual source bodies, never replacement
helper implementations. PAC is abstracted only for host arithmetic replay;
this cannot qualify XNU keys, MMIO or the timer interrupt path.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

INTERVAL = (
    0xd503245f, 0x90000008, 0x91000108, 0xb9400108, 0xd538d08b,
    0xf940d569, 0xf9402d2c, 0xaa0c03e9, 0xd53cfaca, 0xf940d56c,
    0xf9402d8c, 0xeb09019f, 0x54ffff61, 0x9ba07c2b, 0xd349fd6b,
    0xd28b4a6c, 0xf2b4136c, 0xf2d705ec, 0xf2e0088c, 0x9bcc7d6b,
    0xd34bfd6b, 0x92993fed, 0xf2b88cad, 0x9b0d7d6d, 0x9ba0342d,
    0x9b087dad, 0xd349fdad, 0x9bcc7dac, 0x9b087d68, 0x8b4c2d08,
    0x8b090149, 0xab080128, 0xda9f3108, 0xf9000048, 0xd65f03c0)
ABSOLUTE = (
    0xd503245f, 0xd538d088, 0xf940d509, 0xf9402d2b, 0xaa0b03e9,
    0xd53cfaca, 0xf940d50b, 0xf9402d6b, 0xeb09017f, 0x54ffff61,
    0x8b090148, 0xab000108, 0xda9f3108, 0xf9000028, 0xd65f03c0)
SOURCE_CONVERSION = (
    0xd503245f, 0x9ba07c28, 0xd349fd08, 0xd28b4a69, 0xf2b41369,
    0xf2d705e9, 0xf2e00889, 0x9bc97d08, 0xd34bfd08, 0x9000000a,
    0x9100014a, 0xb940014b, 0x9b0b7d0b, 0xf900004b, 0x92993fec,
    0xf2b88cac, 0x9b0c7d08, 0x9ba02028, 0xb940014a, 0x9b0a7d08,
    0xd349fd08, 0x9bc97d08, 0x8b482d68, 0xf9000048, 0xd65f03c0)
MASK = (1 << 64) - 1


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'kdk-research', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior replay; choose a new output')
    try:
        import unicorn
        from unicorn import arm64_const as reg
    except ImportError:
        parser.error('Host replay requires Unicorn; use a Python environment with unicorn installed')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require actual-input fresh pairing')
    imports = json.loads(args.imports.read_text())
    research = json.loads(args.kdk_research.read_text())
    if research.get('reference_role') != 'names-only-research' or research['inputs'][0]['sha256'] != sha(args.stock_kernel):
        parser.error('Actual-stock KDK corroboration required; research names alone never authorize bindings')
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Require actual unique source definition: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: image.read(a, b - a) for a, b in zip(starts, starts[1:]) if b - a <= 512}
    old, new = bodies(original), bodies(native)
    targets, global_address = {}, None
    for name, template in (('_clock_interval_to_deadline', INTERVAL), ('_clock_absolutetime_interval_to_deadline', ABSOLUTE)):
        candidates = []
        for address in missing & old.keys():
            calls, refs, shape = scan(old[address], address)
            if tuple(shape) != template:
                continue
            if name == '_clock_interval_to_deadline':
                if len(refs) != 1 or refs[0][2] != 'add':
                    raise RuntimeError('Stock conversion no longer has one complete RTClockData operand')
                address_data = refs[0][1]
                # The full template reads a 32-bit frequency at this exact
                # operand. It needs no inferred identity for the whole data
                # object: the native implementation uses its own clock state.
                # In particular, do not authorize RTClockData relocation from
                # a research map merely to run arithmetic fixtures.
                global_address = address_data
            candidates.append(address)
        if len(candidates) != 1:
            raise RuntimeError('Complete actual deadline body is missing/ambiguous: ' + name)
        corroboration = [row for row in research['functions']
                         if row['original'] == hex(candidates[0]) and name in row['names']]
        if len(corroboration) != 1:
            raise RuntimeError('Complete deadline contract lacks unique Apple KDK name corroboration')
        targets[name] = candidates[0]
    conversion = named('_clock_interval_to_absolutetime_interval')
    calls, refs, shape = scan(new.get(conversion, b''), conversion)
    if calls or tuple(shape) != SOURCE_CONVERSION or len(refs) != 1 or refs[0][1:] != (named('_RTClockData'), 'add'):
        raise RuntimeError('Complete source frequency-field/conversion contract differs')
    hw_name = '_ml_get_hwclock'
    hardware = [address for address in missing & old.keys()
                if old[address] == struct.pack('<3I', 0xd503245f, 0xd53cfac0, 0xd65f03c0)]
    if len(hardware) != 1 or new.get(named(hw_name)) != struct.pack('<4I', 0xd503245f, 0xd5033fdf, 0xd53be040, 0xd65f03c0):
        raise RuntimeError('Ordered Apple/source hardware clock implementation changed')
    targets[hw_name] = hardware[0]
    # These source functions are genuinely executed below; verify their
    # concrete complete wrapper/callee boundary before replay.
    source_interval = new[named('_clock_interval_to_deadline')]
    calls, refs, _ = scan(source_interval, named('_clock_interval_to_deadline'))
    if refs or [target for _, target, link in calls if link] != [named('_clock_interval_to_absolutetime_interval'), named('_mach_absolute_time')]:
        raise RuntimeError('Actual source interval helper/counter contract changed')
    calls, refs, _ = scan(new[named('_clock_absolutetime_interval_to_deadline')], named('_clock_absolutetime_interval_to_deadline'))
    if refs or [target for _, target, link in calls if link] != [named('_mach_absolute_time')]:
        raise RuntimeError('Actual source absolute helper contract changed')
    transported = {pointer[1:] for pointer in cache.fixups().values() if pointer[0] in targets.values()}
    if transported != {(1, 0, 0, 1)}:
        raise RuntimeError('Review changed clock KPI pointer transport')

    registers = [getattr(reg, 'UC_ARM64_REG_X' + str(i)) for i in range(31)]
    class Replay:
        def __init__(self, image, cpu_offset):
            self.image = image
            self.uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
            ranges = sorted((row['va'] & ~4095, (row['va'] + row['size'] + 4095) & ~4095)
                            for row in image.segments if row['size'])
            merged = []
            for first, last in ranges:
                if merged and first <= merged[-1][1]:
                    merged[-1][1] = max(merged[-1][1], last)
                else:
                    merged.append([first, last])
            for first, last in merged:
                self.uc.mem_map(first, last - first)
            for row in image.segments:
                if row['filesize']:
                    self.uc.mem_write(row['va'], image.read(row['va'], row['filesize']))
            self.uc.mem_map(0x1000000, 4096)
            self.uc.mem_map(0x5000000, 0x100000)
            self.uc.mem_map(0x6000000, 0x20000)
            self.uc.mem_write(0x6000000 + cpu_offset, struct.pack('<Q', 0x6010000))
            self.uc.hook_add(unicorn.UC_HOOK_CODE, self.instruction)
            self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)
            self.counter, self.count, self.reads = 0, 0, 0
            self.executed = set()
        def instruction(self, uc, pc, size, _):
            self.count += 1
            self.executed.add(pc)
            if self.count >= 20000:
                raise RuntimeError('Actual clock code exceeds bounded replay')
            word, = struct.unpack('<I', uc.mem_read(pc, 4))
            if word & 0xffffffe0 == 0xd538d080:
                uc.reg_write(registers[word & 31], 0x6000000)
                uc.reg_write(reg.UC_ARM64_REG_PC, pc + 4)
            elif word & 0xffffffe0 in (0xd53be040, 0xd53cfac0):
                self.reads += 1
                uc.reg_write(registers[word & 31], self.counter)
                uc.reg_write(reg.UC_ARM64_REG_PC, pc + 4)
            elif word in (0xd503237f, 0xd503233f, 0xd50323bf, 0xd50323ff):
                uc.reg_write(reg.UC_ARM64_REG_PC, pc + 4)
            elif word in (0xd65f0fff, 0xd65f0bff):
                uc.reg_write(reg.UC_ARM64_REG_PC, uc.reg_read(reg.UC_ARM64_REG_X30))
        def write(self, uc, access, address, size, value, _):
            if not (0x5000000 <= address and address + size <= 0x5100000):
                raise RuntimeError('Clock API unexpectedly writes outside result/stack fixture')
        def run(self, address, arguments, frequency, data_address, counter, base):
            self.uc.mem_write(data_address, struct.pack('<I', frequency))
            self.uc.mem_write(0x6010058, struct.pack('<Q', base))
            self.uc.mem_write(0x50ff000, bytes(16))
            for register in registers:
                self.uc.reg_write(register, 0)
            self.uc.reg_write(reg.UC_ARM64_REG_X30, 0x1000000)
            self.uc.reg_write(reg.UC_ARM64_REG_SP, 0x50f0000)
            for index, value in enumerate(arguments):
                self.uc.reg_write(registers[index], value)
            self.counter, self.count, self.reads = counter, 0, 0
            self.uc.emu_start(address, 0x1000000, count=20000)
            if self.uc.reg_read(reg.UC_ARM64_REG_PC) != 0x1000000 or self.reads != 1:
                raise RuntimeError('Actual clock path did not return after exactly one counter read')
            value = self.uc.reg_read(reg.UC_ARM64_REG_X0) if not arguments else struct.unpack('<Q', self.uc.mem_read(0x50ff000, 8))[0]
            return value
    left, right = Replay(original, 0x1a8), Replay(native, 0x208)
    rng = random.Random(0x22a100)
    vectors, count = [], 0
    for name in targets:
        for index in range(512):
            counter = rng.getrandbits(64) if index >= 32 else (0, 1, MASK, MASK - 255)[index % 4]
            base = rng.getrandbits(64) if index % 3 else 0
            frequency = rng.choice((1, 1000000, 12000000, 19200000, 24000000, 0xffffffff))
            now = (counter + base) & MASK
            if name == '_clock_interval_to_deadline':
                interval = rng.getrandbits(32)
                scale = rng.choice((0, 1, 1000, 1000000, 1000000000, 0xffffffff))
                arguments = [interval, scale, 0x50ff000]
                nanoseconds = interval * scale
                absolute = ((nanoseconds // 1000000000) * frequency + (nanoseconds % 1000000000) * frequency // 1000000000) & MASK
                expected = min(MASK, now + absolute)
            elif name == '_clock_absolutetime_interval_to_deadline':
                absolute = rng.getrandbits(64)
                arguments = [absolute, 0x50ff000]
                expected = min(MASK, now + absolute)
            else:
                arguments, expected = [], counter
            before = left.run(targets[name], arguments, frequency, global_address, counter, base)
            after = right.run(named(name), arguments, frequency, named('_RTClockData'), counter, base)
            if before != after or after != expected:
                raise RuntimeError('Actual stock/native/independent-integer clock result disagrees: ' + repr((name, arguments, frequency, counter, base, before, after, expected)))
            vectors.append((name, arguments, frequency, counter, base, expected))
            count += 1
    vector_hash = hashlib.sha256(json.dumps(vectors).encode()).hexdigest()
    rows = [{'original': hex(address), 'name': name, 'final_import_only': True,
             'evidence': 'Complete reviewed ordered-clock/deadline instruction contract; actual source helpers and stock inline arithmetic replay against independent integer result across configured clock frequencies, base offsets and overflow cases; source ISB+CNTVCT implements non-speculative Apple counter API on SM8475, not raw-register equality; native diagnostic checks retained',
             'original_sha256': hashlib.sha256(old[address]).hexdigest(), 'native_sha256': hashlib.sha256(new[named(name)]).hexdigest(),
             'platform_difference': 'Apple ordered counter instruction versus actual ARMv8.2 ISB+CNTVCT backend',
             'replay_cases': 512} for name, address in targets.items()]
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': rows, 'data': [], 'bootable': False,
              'frequency_operand_contract': {'stock_operand': hex(global_address), 'native_operand': hex(named('_RTClockData')),
                                             'field_width_bits': 32, 'field_offset': 0,
                                             'whole_data_object_binding_authorized': False,
                                             'evidence': 'Complete stock interval and source conversion templates prove the concrete frequency loads and nanosecond scaling; no graph-derived global identity used'},
              'kdk_research_sha256': sha(args.kdk_research),
              'replay': {'result': 'PASS', 'cases': count, 'vectors_sha256': vector_hash,
                         'source_actual_instruction_addresses_executed': len(right.executed),
                         'counter_abstraction': 'one ordered platform hardware read; TPIDR fixture uses concrete per-image CPU-data offsets',
                         'pac_abstraction': 'host arithmetic replay skips PAC prologue/return only; no key/authentication qualification',
                         'scope': 'normal awake clock path; native optional timebase diagnostic checks remain intact; no timer IRQ/device execution'}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Three complete platform-clock imports qualified;', count, 'actual instruction replays against independent integer results')


if __name__ == '__main__':
    main()
