#!/usr/bin/env python3
"""Review the boolean wake-one/all KPI at its real typed waitq call boundary.

The complete stock body contains the event hash, boolean-to-count conversion
and count-to-Kern return translation. The actual source API performs those
operations through its real helper chain. Host replay stops before actual
waitq mutation, captures all five arguments, and exercises return translation
from explicit boundary fixtures. No waiter, scheduler wakeup or hardware
completion is invented. The wrong count-taking API is an explicit control.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

OLD = (
    0xd503237f, 0xd100c3ff, 0xa9027bfd, 0x910083fd, 0xaa0003e8,
    0x34000081, 0xb40004a8, 0x52800024, 0x14000003, 0xb4000468,
    0x12800004, 0x90000009, 0xf9400129, 0xca487d0a, 0xd29430ab,
    0xf2ae51cb, 0xf2da652b, 0xf2eff6ab, 0x9b0b7d4a, 0xca4a6d4a,
    0xd29a89ab, 0xf2b785ab, 0xf2dbde8b, 0xf2f03b4b, 0x9b0b7d4a,
    0xd361fd4b, 0x4a0a016a, 0x9000000b, 0xb940016b, 0x5100056b,
    0x0a0a016a, 0x5280030b, 0x9bab7d4a, 0x8b0a0120, 0xaa0803e1,
    0x52800003, 0x94000000, 0x52800608, 0x7100001f, 0x1a9f0100,
    0xa9427bfd, 0x9100c3ff, 0xd65f0fff, 0x14000001, 0x5280efc8,
    0x90000009, 0x91000129, 0x9000000a, 0x9100014a, 0xa900a3e9,
    0xf90003ea, 0x90000000, 0x91000000, 0x94000000)
BOOLEAN = (0xd503245f, 0x7100003f, 0x12800008, 0x5a880501, 0x14000000)
COUNT = (0xd503237f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd,
         0xb4000220, 0xaa0203f4, 0xaa0103f3, 0xaa0003f5, 0x94000000,
         0xaa1503e1, 0xaa1403e2, 0x52800003, 0xaa1303e4, 0x94000000,
         0x7100001f, 0x52800608, 0x1a9f0100, 0xa9427bfd, 0xa9414ff4,
         0xa8c357f6, 0xd65f0fff, 0x94000000)
HASH = (0xd503245f, 0x90000008, 0xf9400108, 0xca407c09, 0xd29430aa,
        0xf2ae51ca, 0xf2da652a, 0xf2eff6aa, 0x9b0a7d29, 0xca496d29,
        0xd29a89aa, 0xf2b785aa, 0xf2dbde8a, 0xf2f03b4a, 0x9b0a7d29,
        0xd361fd2a, 0x4a090149, 0x9000000a, 0xb940014a, 0x5100054a,
        0x0a090149, 0x5280030a, 0x9baa7d29, 0x8b090100, 0xd65f03c0)
MASK = (1 << 64) - 1


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def event_index(value, count):
    # Actual public os_hash_uint64, with its two explicit arithmetic rounds.
    value = ((value ^ (value >> 31)) * 0x7fb5d329728ea185) & MASK
    value = ((value ^ (value >> 27)) * 0x81dadef4bc2dd44d) & MASK
    return ((value ^ (value >> 33)) & 0xffffffff) & (count - 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'panic-anchor', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old wakeup ABI replay; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    panic = json.loads(args.panic_anchor.read_text())
    actual_hashes = sha(args.stock_kernel), sha(args.native_kernel)
    if (panic['stock_sha256'], panic['native_sha256']) != actual_hashes or built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == actual_hashes[1] for row in built['artifacts']):
        parser.error('Actual source image and independently reviewed genuine panic ABI required')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Actual wakeup/helper definition not unique: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:]) if last - first <= 8192}
    old, new = bodies(original), bodies(native)
    original_candidates = [address for address, code in old.items() if len(code) == len(OLD) * 4 and tuple(scan(code, address)[2]) == OLD]
    api, count_api, hash_api = named('_thread_wakeup_prim'), named('_thread_wakeup_nthreads_prim'), named('__global_eventq')
    if len(original_candidates) != 1 or tuple(scan(new[api], api)[2]) != BOOLEAN or tuple(scan(new[count_api], count_api)[2]) != COUNT or tuple(scan(new[hash_api], hash_api)[2]) != HASH:
        raise RuntimeError('Complete actual boolean/count/hash wakeup instruction contracts differ')
    start = original_candidates[0]
    old_calls, old_refs, _ = scan(old[start], start)
    original_waitq_call = next((row for row in old_calls if row[0] == start + 144 and row[2]), None)
    original_panic = next(int(row['original'], 16) for row in panic['functions'] if row['name'] == '_panic')
    if not original_waitq_call or old_calls[-1][1] != original_panic:
        raise RuntimeError('Actual original waitq/fatal call boundaries changed')
    literals = {original.cstring(value) for _, value, _ in old_refs}
    if not {'sched_prim.c', 'thread_wakeup_nthreads_prim', '%s() called with NO_EVENT @%s:%d'} <= literals:
        raise RuntimeError('Actual original invalid-event path metadata differs')
    calls, _, _ = scan(new[count_api], count_api)
    source_boundary = named('_waitq_wakeup64_nthreads')
    if calls[0][1] != hash_api or calls[1][1] != source_boundary or calls[1][0] != count_api + 56:
        raise RuntimeError('Actual source count wrapper must invoke its real typed hash/waitq implementations')
    if scan(new[api], api)[0] != [(api + 16, count_api, False)]:
        raise RuntimeError('Actual source boolean API no longer tail-delegates its converted count')
    original_globals = [value for _, value, kind in old_refs if kind == 'ldst']
    source_globals = [value for _, value, kind in scan(new[hash_api], hash_api)[1] if kind == 'ldst']
    if len(original_globals) != 2 or source_globals != [named('_global_waitqs'), named('_g_num_waitqs')]:
        raise RuntimeError('Actual event hash table/size operand layout changed')
    source_file = args.native_build / 'tree/.cache/xnu-work/osfmk/kern/sched_prim.c'
    source = source_file.read_text()
    if 'return thread_wakeup_nthreads_prim(event, 1, result);' not in source or 'return thread_wakeup_nthreads_prim(event, UINT32_MAX, result);' not in source:
        raise RuntimeError('Actual compiled source no longer declares the reviewed wake-one/all contract')
    queue_base = 0x50b0000
    class BoundaryReplay(KernelReplay):
        def __init__(self, image, boundary, panic_entry):
            super().__init__(image)
            self.boundary, self.panic_entry = boundary, panic_entry
            self.captured, self.kind = None, None
            self.uc.hook_add(self.unicorn.UC_HOOK_CODE, self.capture)
        def capture(self, uc, pc, size, _):
            if pc == self.boundary:
                self.kind = 'waitq-call'
                self.captured = tuple(uc.reg_read(self.registers[index]) for index in range(5))
                uc.emu_stop()
            elif pc == self.panic_entry:
                self.kind = 'real-panic-not-executed'
                self.captured = ()
                uc.emu_stop()
        def prepare(self, entry, arguments):
            for register in self.registers:
                self.uc.reg_write(register, 0)
            self.uc.reg_write(self.reg.UC_ARM64_REG_SP, self.stack)
            self.uc.reg_write(self.reg.UC_ARM64_REG_X30, self.return_address)
            for index, value in enumerate(arguments):
                self.uc.reg_write(self.registers[index], value & MASK)
            self.count, self.kind, self.captured = 0, None, None
            self.uc.emu_start(entry, self.return_address, count=self.limit)
            if self.kind is None:
                raise RuntimeError('Actual wakeup wrapper did not reach a reviewed call boundary')
        def translate_count(self, value):
            if self.kind != 'waitq-call':
                raise RuntimeError('Cannot resume a genuine fatal path')
            # Explicit caller-side return fixture only: the waitq body is
            # not run, replaced, or declared to have woken any real thread.
            site = self.uc.reg_read(self.reg.UC_ARM64_REG_X30)
            self.uc.reg_write(self.registers[0], value)
            self.uc.emu_start(site, self.return_address, count=self.limit)
            if self.uc.reg_read(self.reg.UC_ARM64_REG_PC) != self.return_address:
                raise RuntimeError('Actual wakeup return translation did not return')
            return self.uc.reg_read(self.registers[0]) & 0xffffffff
    left = BoundaryReplay(original, original_waitq_call[1], original_panic)
    right = BoundaryReplay(native, source_boundary, named('_panic'))
    rng = random.Random(0x22ea11)
    vectors = []
    for index in range(768):
        event = rng.getrandbits(64) or 1
        one = rng.choice((0, 1, 2, 0xffffffff, 0x100000000))
        result = rng.getrandbits(32)
        table_count = rng.choice((1, 2, 4, 16, 64, 256, 1024))
        wake_count = rng.choice((0, 1, 2, 33, 0xffffffff))
        expected_count = 1 if one & 0xffffffff else 0xffffffff
        expected = (queue_base + event_index(event, table_count) * 24, event, result, 0, expected_count)
        for machine, entry, globals_ in ((left, start, original_globals), (right, api, source_globals)):
            machine.uc.mem_write(globals_[0], struct.pack('<Q', queue_base))
            machine.uc.mem_write(globals_[1], struct.pack('<I', table_count))
            machine.prepare(entry, [event, one, result])
            if machine.kind != 'waitq-call' or machine.captured != expected:
                raise RuntimeError('Actual boolean/hash/waitq argument ABI disagrees: ' + repr((index, machine.kind, machine.captured, expected)))
            if machine.translate_count(wake_count) != (0 if wake_count else 48):
                raise RuntimeError('Actual waitq-count to KERN return contract differs')
        vectors.append((event, one, result, table_count, wake_count))
    for machine, entry in ((left, start), (right, api)):
        for one in (0, 1, 2):
            machine.prepare(entry, [0, one, 0])
            if machine.kind != 'real-panic-not-executed':
                raise RuntimeError('Actual invalid-event API no longer takes genuine panic path')
    wrong_api_controls = []
    for one in (0, 2, 0xffffffff):
        right.prepare(count_api, [0x123456789, one, 7])
        expected = 1 if one else 0xffffffff
        if right.captured[4] == expected:
            raise RuntimeError('Count-taking wrong API control did not expose the original mismatch')
        wrong_api_controls.append({'boolean_input': one, 'required_count': expected,
                                   'wrong_api_actual_count': right.captured[4], 'result': 'REJECTED_WRONG_API'})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': actual_hashes[0], 'native_sha256': actual_hashes[1],
                                   'functions': [{'original': hex(start), 'name': '_thread_wakeup_prim', 'final_import_only': True,
                                                  'api_identity_proven': True,
                                                  'evidence': 'Complete stock boolean-to1/UINT32_MAX conversion, public event hash and count-to-KERN return contract matches actual source boolean/count/hash helper chain at its real typed waitq boundary;768 concrete argument/return fixtures and invalid-event genuine-panic paths agree; wrong count-taking API demonstrably disagrees; actual waitq mutation/scheduler success not emulated',
                                                  'original_sha256': hashlib.sha256(old[start]).hexdigest(), 'native_sha256': hashlib.sha256(new[api]).hexdigest(),
                                                  'actual_source_contract_sha256': sha(source_file)}],
                                   'data': [], 'bootable': False,
                                   'boundary_replay': {'result': 'PASS', 'cases': len(vectors), 'vector_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                                                       'wrong_api_controls': wrong_api_controls, 'waitq_body_executed': False,
                                                       'real_waiters_or_kernel_threads_created': False, 'authentication': 'host computational abstraction only'}}, indent=2) + '\n')
    print('Actual boolean wake-one/all call/return contract qualified;768 cases; count-taking wrong API controls rejected')


if __name__ == '__main__':
    main()
