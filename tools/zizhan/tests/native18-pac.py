#!/usr/bin/env python3
"""Build a freestanding Android EL0 PAC probe as a static ELF, without an NDK.

Assemble the actual AArch64 code, require zero relocations, and wrap its text
in a minimal read/execute ELF PT_LOAD and non-executable GNU_STACK. This tool
does not run the ELF or access a phone; device operations are separately
bounded and recorded by the operator.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--correct-only', action='store_true', help='Control without mismatched-modifier authentication')
    parser.add_argument('--record-cpu', action='store_true', help='Record actual Linux getcpu result before the bounded PAC stages')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior probe artifacts; choose a new output')
    args.out.mkdir(parents=True)
    source = Path(__file__).with_suffix('.S')
    obj = args.out / 'pac-probe.o'
    command = ['xcrun', 'clang', '-target', 'aarch64-linux-android28', '-march=armv8.3-a+pauth',
               *(['-DPAC_CORRECT_ONLY=1'] if args.correct_only else []),
               *(['-DPAC_RECORD_CPU=1'] if args.record_cpu else []),
               '-c', str(source), '-o', str(obj)]
    subprocess.run(command, check=True)
    raw = obj.read_bytes()
    fields = struct.unpack_from('<16sHHIQQQIHHHHHH', raw)
    if fields[0][:7] != b'\x7fELF\x02\x01\x01' or fields[1:4] != (1, 183, 1):
        parser.error('Assembler did not produce actual AArch64 little-endian ELF')
    section_offset, stride, count, strings_index = fields[6], fields[11], fields[12], fields[13]
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, section_offset + stride * i) for i in range(count)]
    string_section = sections[strings_index]
    strings = raw[string_section[4]:string_section[4] + string_section[5]]
    text = []
    for row in sections:
        if row[1] in (4, 9) and row[5]:
            parser.error('Probe has unresolved ELF relocations')
        name = strings[row[0]:strings.find(b'\0', row[0])]
        if name == b'.text':
            if row[1] != 1 or row[2] != 6:
                parser.error('Probe text is not allocatable read/execute code')
            text.append(raw[row[4]:row[4] + row[5]])
    if len(text) != 1 or not 0 < len(text[0]) <= 4096:
        parser.error('Require one bounded actual text section')
    base, code_offset = 0x400000, 4096
    image_size = code_offset + len(text[0])
    identity = b'\x7fELF' + bytes((2, 1, 1, 0)) + bytes(8)
    header = struct.pack('<16sHHIQQQIHHHHHH', identity, 2, 183, 1, base + code_offset,
                         64, 0, 0, 64, 56, 2, 64, 0, 0)
    load = struct.pack('<IIQQQQQQ', 1, 5, 0, base, 0, image_size, image_size, 4096)
    stack = struct.pack('<IIQQQQQQ', 0x6474e551, 6, 0, 0, 0, 0, 0, 16)
    image = header + load + stack
    image += bytes(code_offset - len(image)) + text[0]
    binary = args.out / 'zizhan-pac-probe'
    binary.write_bytes(image)
    binary.chmod(0o700)
    report = {'architecture': 'AArch64 Android ELF64 ET_EXEC', 'entry': hex(base + code_offset),
              'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'correct_only': args.correct_only,
              'record_actual_cpu': args.record_cpu,
              'elf_sha256': hashlib.sha256(image).hexdigest(), 'elf_bytes': len(image),
              'text_bytes': len(text[0]), 'unresolved_relocations': 0,
              'load_permissions': 'read/execute', 'stack_permissions': 'read/write',
              'syscalls': ['write stdout', 'exit', *(['getcpu'] if args.record_cpu else [])], 'bounded': {'env_entries': 4096, 'auxv_entries': 256, 'vectors_per_key': 16},
              'scope': 'Android EL0 PAuth instructions; native XNU EL1/per-CPU key lifecycle not qualified',
              'device_access': False, 'command': command}
    (args.out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print('HOST static bounded Android PAC ELF assembled:', len(image), 'bytes; not executed')


if __name__ == '__main__':
    main()
