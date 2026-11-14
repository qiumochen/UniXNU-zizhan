#!/usr/bin/env python3
"""Run the generated native loader's actual parser under ASan/UBSan.

Mach-O fixtures are synthetic nonexecuted parser inputs. This never starts
firmware or a phone and cannot qualify a real native fileset's ABI closure.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--actual-image", action="append", type=Path, default=[],
                        help="Also parse an actual retained arm64e image, without executing or qualifying its imports")
    args = parser.parse_args()
    text = args.source.read_text()
    begin = text.index("STATIC\nEFI_STATUS\nParseKernel ")
    brace = text.index("{", begin)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            break
    function = text[begin:end + 1]
    structures = text[text.index("typedef struct {"):text.index("STATIC EFI_HANDLE")]
    out = args.out.resolve()
    if out.exists():
        parser.error("Use a new local output directory to retain each result")
    out.mkdir(parents=True)
    harness = out / "parser.c"
    harness.write_text('''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UINT8; typedef uint32_t UINT32; typedef uint64_t UINT64;
typedef size_t UINTN; typedef int BOOLEAN; typedef int EFI_STATUS;
#define STATIC static
#define CONST const
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 1
#define EFI_UNSUPPORTED 3
#define MAX_SEGMENTS 64
#define MAX_UINT64 UINT64_MAX
#define SIZE_2MB 0x200000ULL
#define WINDOW_SIZE 0x10000000ULL
#define EFI_PAGE_MASK 4095
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ZeroMem(p,n) memset(p,0,n)
#define CompareMem(a,b,n) memcmp(a,b,n)
static uint32_t U32(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t U64(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
''' + structures + function + '''
int main(int argc,char**argv){if(argc!=4)return 10;
FILE*f=fopen(argv[1],"rb");if(!f)return 11;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
if(n<0)return 12;unsigned char*b=malloc((size_t)n+1);if(!b)return 13;
if(fread(b,1,(size_t)n,f)!=(size_t)n)return 14;fclose(f);KERNEL k;
int s=ParseKernel(b,(size_t)n,&k);int a=s==0?k.Arm64e:-1;
printf("status=%d arm64e=%d count=%zu base=%llx end=%llx entry=%llx\\n",s,a,
 s==0?k.Count:0,(unsigned long long)(s==0?k.Base:0),(unsigned long long)(s==0?k.End:0),(unsigned long long)(s==0?k.Entry:0));free(b);
return s==atoi(argv[2])&&a==atoi(argv[3])?0:15;}
''')
    executable = out / "parser"
    subprocess.run(["xcrun", "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(harness), "-o", str(executable)], check=True)
    address = 0xfffffff007004000
    segment = struct.pack("<II16sQQQQiiII", 0x19, 72, b"__TEXT", address, 0x4000, 0, 0x4000, 5, 5, 0, 0)
    state = bytearray(272)
    struct.pack_into("<Q", state, 256, address + 0x3000)
    commands = segment + struct.pack("<IIII", 5, 288, 6, 68) + state
    image = bytearray(0x4000)
    image[:32] = struct.pack("<8I", 0xfeedfacf, 0x100000c, 0, 2, 2, len(commands), 0, 0)
    image[32:32 + len(commands)] = commands
    cases = [("arm64", image, 0, 0)]
    arm = bytearray(image)
    struct.pack_into("<I", arm, 8, 0x80000002)
    cases.append(("arm64e-capability", arm, 0, 1))
    missing = bytearray(arm)
    struct.pack_into("<I", missing, 12, 12)
    cases.append(("fileset-missing-kernel", missing, 1, -1))
    fs = bytearray(missing)
    at = 32 + len(commands)
    name = b"com.apple.kernel\0"
    length = (32 + len(name) + 7) & ~7
    fs[0x1000:0x1020] = arm[:32]
    entry = struct.pack("<IIQQII", 0x80000035, length, address + 0x1000, 0x1000, 32, 0) + name
    fs[at:at + length] = entry + bytes(length - len(entry))
    struct.pack_into("<II", fs, 16, 3, len(commands) + length)
    cases.append(("fileset-arm64e", fs, 0, 1))
    bad = bytearray(fs)
    struct.pack_into("<Q", bad, at + 16, len(bad) - 16)
    cases.append(("fileset-header-outside", bad, 1, -1))
    bad = bytearray(fs)
    bad[at + length:at + 2 * length] = fs[at:at + length]
    struct.pack_into("<II", bad, 16, 4, len(commands) + length * 2)
    cases.append(("fileset-duplicate-kernel", bad, 1, -1))
    bad = bytearray(arm)
    struct.pack_into("<I", bad, 8, 3)
    cases.append(("foreign-subtype", bad, 3, -1))
    bad = bytearray(image)
    struct.pack_into("<Q", bad, 32 + 16 + 8, 0xfffffffffffff000)
    cases.append(("segment-overflow", bad, 1, -1))
    cases.append(("truncated-header", image[:24], 3, -1))
    results = []
    for name, data, status, arm64e in cases:
        fixture = out / (name + ".macho")
        fixture.write_bytes(data)
        result = subprocess.run([str(executable), str(fixture), str(status), str(arm64e)],
                                capture_output=True, text=True, timeout=15)
        results.append({"name": name, "expected_status": status, "returncode": result.returncode,
                        "stdout": result.stdout, "stderr": result.stderr})
    for index, actual in enumerate(args.actual_image):
        data = actual.read_bytes()
        if len(data) < 32 or struct.unpack_from('<II', data) != (0xfeedfacf, 0x100000c) or struct.unpack_from('<I', data, 8)[0] & 0xffffff != 2:
            parser.error('Actual nonexecuted input must be a real native arm64e Mach-O')
        result = subprocess.run([str(executable), str(actual.resolve()), '0', '1'],
                                capture_output=True, text=True, timeout=15)
        results.append({'name': 'actual-native-' + str(index), 'path': str(actual),
                        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                        'expected_status': 0, 'returncode': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr,
                        'scope': 'real header/segment/entry bounds only; kernel and firmware never executed, import closure not qualified'})
        # Actual-input negative controls retain all other original bytes;
        # corrupt the entry to unaligned/outside or a segment file range.
        cursor, entry_offset, segment_offset = 32, None, None
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, length = struct.unpack_from('<II', data, cursor)
            if command == 5:
                entry_offset = cursor + 272
            if command == 0x19 and segment_offset is None and struct.unpack_from('<Q', data, cursor + 32)[0]:
                segment_offset = cursor
            cursor += length
        if entry_offset is None or segment_offset is None:
            parser.error('Actual image lacks the parser-tested entry/segment metadata')
        for label in ('unaligned-entry', 'file-range-overrun'):
            changed = bytearray(data)
            if label == 'unaligned-entry':
                value, = struct.unpack_from('<Q', changed, entry_offset)
                struct.pack_into('<Q', changed, entry_offset, value | 1)
            else:
                struct.pack_into('<Q', changed, segment_offset + 40, len(data) + 4096)
            fixture = out / ('actual-native-' + str(index) + '-' + label + '.macho')
            fixture.write_bytes(changed)
            control = subprocess.run([str(executable), str(fixture), '1', '-1'],
                                     capture_output=True, text=True, timeout=15)
            results.append({'name': 'actual-native-' + str(index) + '-' + label,
                            'expected_status': 1, 'returncode': control.returncode,
                            'stdout': control.stdout, 'stderr': control.stderr})
    report = {"scope": "host-only actual parser; fixtures never executed", "source": str(args.source),
              "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
              "parser_sha256": hashlib.sha256(function.encode()).hexdigest(), "cases": results}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    if any(row["returncode"] for row in results):
        raise SystemExit("Native loader parser failed; inspect " + str(out / "results.json"))
    print(str(len(results)) + " native loader parser controls passed under ASan/UBSan")


if __name__ == "__main__":
    main()
