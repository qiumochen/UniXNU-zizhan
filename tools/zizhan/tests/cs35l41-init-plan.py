#!/usr/bin/env python3
"""Synthetic ELF/OTP bitstream controls. No real trim/OTP values required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools/zizhan/mac/cs35l41-init-plan.py"
spec = importlib.util.spec_from_file_location("otp_plan", TOOL)
otp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(otp)


def fixture():
    # Artificial100-field layout consuming688 bits; no phone OTP/table data.
    regs = sorted(otp.TRIM_REGISTERS)[:25]
    rows = [(regs[i // 4], (i % 4) * 7, 7 if i < 88 else 6, 0) for i in range(100)]
    table = b"".join(struct.pack("<IBBH", *row) for row in rows)
    rodata = bytearray(table + table)
    map_start = len(rodata)
    for identity in (1, 2, 3, 6, 8):
        rodata += struct.pack("<IIQII", identity, 100, 0, 16, 2)
    patch_start = len(rodata)
    rodata += b"".join(struct.pack("<III", reg, value, 0) for reg, value in otp.B2_PATCH)
    strings = bytearray(b"\0")
    symbols = bytearray(24)
    definitions = [("cs35l41_otp_map_map", map_start, 120), ("otp_map_1.synthetic", 0, 800),
                   ("otp_map_2.synthetic", 800, 800), ("cs35l41_revb2_errata_patch", patch_start, 120)]
    for name, value, size in definitions:
        pos = len(strings)
        strings += name.encode() + b"\0"
        symbols += struct.pack("<IBBHQQ", pos, 1, 0, 3, value, size)
    relocations = bytearray()
    for i, target in enumerate((2, 3, 3, 3, 2)):
        relocations += struct.pack("<QQq", map_start + i * 24 + 8, target << 32 | 257, 0)
    sections = [b"", b"\0.shstrtab\0.strtab\0.rodata\0.symtab\0.arbitrary-relocations\0", bytes(strings),
                bytes(rodata), bytes(symbols), bytes(relocations)]
    names = [0, 1, 11, 19, 27, 35]
    headers, data = [], bytearray(64)
    locations = []
    for i, raw in enumerate(sections):
        data += bytes(-len(data) % 8)
        at = len(data)
        locations.append(at)
        data += raw
        kind = [0, 3, 3, 1, 2, 4][i]
        link = 2 if i == 4 else 4 if i == 5 else 0
        info = 3 if i == 5 else 0
        headers.append(struct.pack("<IIQQQQIIQQ", names[i], kind, 0, 0, at, len(raw), link, info, 8, 24 if i in (4, 5) else 0))
    data += bytes(-len(data) % 8)
    section_table = len(data)
    data += b"".join(headers)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HH", data, 16, 1, 183)
    struct.pack_into("<Q", data, 40, section_table)
    struct.pack_into("<HHH", data, 58, 64, len(headers), 1)
    return data, dict(rodata=locations[3], relocations=locations[5], maps=map_start, patch=patch_start, sht=section_table)


class Plan(unittest.TestCase):
    def test_actual_parser_uses_relocation_target_not_section_name(self):
        raw, _ = fixture()
        result = otp.extract(bytes(raw))
        self.assertEqual([m["id"] for m in result["otp_maps"]], [1, 2, 3, 6, 8])
        self.assertTrue(all(m["consumed_bits"] == 688 for m in result["otp_maps"]))
        self.assertFalse(result["plan_executable"])
        self.assertTrue(result["snapshot_required"])

    def test_runtime_b2_patch_matches_reviewed_actual_table(self):
        source = (ROOT / "drivers/PongPower/ZizhanCs35l41.c").read_text()
        block = source.split("static const uint32_t b2_patch[10][2] = {", 1)[1].split("};", 1)[0]
        import re
        rows = [(int(a, 0), int(b, 0)) for a, b in re.findall(r"\{(0x[0-9a-f]+),(0x[0-9a-f]+|0)\}", block)]
        self.assertEqual(rows, otp.B2_PATCH)

    def test_runtime_trim8_decoder_matches_independent_host_bitstream(self):
        import ctypes
        import re
        header = ROOT / "drivers/PongPower/ZizhanCs35l41TrimMap.h"
        rows = [dict(register=int(a, 16), shift=int(b), bits=int(c))
                for a, b, c in re.findall(r"\{0x([0-9a-f]+),(\d+),(\d+)\}", header.read_text())]
        self.assertEqual(len(rows), 100)
        self.assertEqual(otp.fields(rows, 2, 16)[1], 768)
        class Update(ctypes.Structure):
            _fields_ = [("reg", ctypes.c_uint32), ("mask", ctypes.c_uint32), ("value", ctypes.c_uint32)]
        with tempfile.TemporaryDirectory() as temp:
            source, lib = Path(temp) / "decode.c", Path(temp) / "decode.so"
            source.write_text('#include "ZizhanCs35l41TrimMap.h"\nint decode(unsigned id,const uint32_t *words,ZizhanAmpTrimUpdate *out){return zizhan_amp_decode_trim8(id,words,out)&&zizhan_amp_trim_register(out[0].reg); }\n')
            subprocess.run(["cc", "-shared", "-fPIC", "-I" + str(header.parent), str(source), "-o", str(lib)], check=True, timeout=30)
            fn = ctypes.CDLL(str(lib)).decode
            fn.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Update)]
            fn.restype = ctypes.c_int
            for trial in range(40):
                words = [(i * 0x1234567 ^ trial * 0x3a5a5a5a) & 0xffffffff for i in range(32)]
                expected = otp.decode(words, rows)
                result = (Update * 98)()
                self.assertEqual(fn(8, (ctypes.c_uint32 * 32)(*words), result), 1)
                self.assertEqual([(u.reg, u.mask, u.value) for u in result],
                                 [(u["register"], u["mask"], u["value"]) for u in expected])
            self.assertEqual(fn(2, (ctypes.c_uint32 * 32)(), (Update * 98)()), 0)

    def test_elf_arch_headers_and_extents(self):
        raw, info = fixture()
        variants = [bytes(raw[:40]), b"bad!" + bytes(raw[4:]), bytes(raw[:-1])]
        for field, fmt, value in ((18, "<H", 62), (40, "<Q", 0xffffffffffffffff), (58, "<H", 63), (60, "<H", 513)):
            changed = bytearray(raw)
            struct.pack_into(fmt, changed, field, value)
            variants.append(bytes(changed))
        for data in variants:
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                otp.extract(data)

    def test_pointer_relocation_type_target_and_addend(self):
        raw, info = fixture()
        for offset, fmt, value in ((8, "<Q", (2 << 32) | 258), (8, "<Q", (99 << 32) | 257),
                                   (16, "<q", 8), (0, "<Q", 123)):
            changed = bytearray(raw)
            struct.pack_into(fmt, changed, info["relocations"] + offset, value)
            with self.assertRaises(ValueError):
                otp.extract(bytes(changed))

    def test_map_id_count_start_padding_and_errata_drift(self):
        raw, info = fixture()
        edits = [(info["rodata"] + info["maps"], "<I", 9),
                 (info["rodata"] + info["maps"] + 4, "<I", 99),
                 (info["rodata"] + info["maps"] + 16, "<I", 17),
                 (info["rodata"] + 6, "<H", 1),
                 (info["rodata"] + info["patch"] + 4, "<I", 0)]
        for at, fmt, value in edits:
            changed = bytearray(raw)
            struct.pack_into(fmt, changed, at, value)
            with self.assertRaises(ValueError):
                otp.extract(bytes(changed))

    def test_word_crossing_reserved_and_zero_size(self):
        rows = [dict(register=0x2030, shift=7, bits=8), dict(register=0, shift=0, bits=1),
                dict(register=0x208c, shift=24, bits=6), dict(register=0, shift=0, bits=0)]
        words = [0] * 32
        stream = (0xab << 28) | (1 << 36) | (0x35 << 37)
        words[0] = stream & 0xffffffff
        words[1] = stream >> 32
        result = otp.decode(words, rows, 0, 28)
        self.assertEqual(result, [dict(register=0x2030, mask=0x7f80, value=0x5580, operation="volatile_update_bits"),
                                  dict(register=0x208c, mask=0x3f000000, value=0x35000000, operation="volatile_update_bits")])
        self.assertTrue(all(not 0x400 <= row["register"] < 0x480 for row in result))

    def test_destination_guards_bit_exhaustion_and_overlap(self):
        variants = [[dict(register=reg, shift=0, bits=1)] for reg in (0x40, 0x400, 0x2014, 0x2018, 0x2bc1000)]
        variants += [[dict(register=0x2030, shift=31, bits=2)], [dict(register=0x2030, shift=0, bits=0)],
                     [dict(register=0, shift=0, bits=33)], [dict(register=0x2030, shift=0, bits=4)] * 2]
        for rows in variants:
            with self.assertRaises(ValueError):
                otp.decode([0] * 32, rows)
        with self.assertRaises(ValueError):
            otp.decode([0] * 32, [dict(register=0, shift=0, bits=2)], 31, 31)

    def test_input_words_are_exact_unsigned32_not_per_chip_defaults(self):
        rows = [dict(register=0x2030, shift=0, bits=4)]
        for words in ([0] * 31, [0] * 33, [-1] * 32, [1 << 32] * 32, [True] * 32):
            with self.assertRaises(ValueError):
                otp.decode(words, rows)
        a, b = [0] * 32, [0xffffffff] * 32
        self.assertEqual(otp.decode(a, rows)[0]["value"], 0)
        self.assertEqual(otp.decode(b, rows)[0]["value"], 15)

    def test_all_fields_reconstruct_with_independent_bit_reader(self):
        raw, _ = fixture()
        result = otp.extract(bytes(raw))
        words = [(i * 0x1234567 ^ 0xa5a55a5a) & 0xffffffff for i in range(32)]
        table = result["otp_maps"][0]
        decoded = otp.decode(words, table["table_rows"])
        for row, schema in zip(decoded, table["fields"]):
            value = 0
            for j in range(schema["bits"]):
                pos = schema["source_bit"] + j
                value |= ((words[pos // 32] >> (pos % 32)) & 1) << j
            self.assertEqual(row["value"], value << schema["shift"])

    def test_cli_source_hash_scope_and_retention(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            raw, _ = fixture()
            module, manifest, output = root / "module.ko", root / "manifest.json", root / "report.json"
            module.write_bytes(raw)
            manifest.write_text(json.dumps(dict(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw),
                                                 origin="/vendor_dlkm/lib/modules/cs35l41_dlkm.ko")))
            cmd = [sys.executable, str(TOOL), "--module", str(module), "--manifest", str(manifest), "--output", str(output)]
            self.assertEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            r = json.loads(output.read_text())
            self.assertEqual(r["status"], "HOST_VERIFIED_B2_OTP_SCHEMA")
            for key in ("phone_operations", "hardware_writes", "plan_executable", "otp_programmed", "otp_snapshot_read",
                        "otp_unpacked", "dsp_started", "audio_output"):
                self.assertIs(r[key], False)
            prior = output.read_bytes()
            self.assertNotEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            self.assertEqual(output.read_bytes(), prior)
            module.write_bytes(bytes(raw) + b"drift")
            failed = root / "failed.json"
            self.assertNotEqual(subprocess.run(cmd[:-1] + [str(failed)], capture_output=True, timeout=10).returncode, 0)
            self.assertEqual(json.loads(failed.read_text())["status"], "FAIL")
            self.assertNotEqual(subprocess.run(cmd[:-1] + [str(ROOT / "not-allowed.json")], capture_output=True, timeout=10).returncode, 0)


if __name__ == "__main__":
    unittest.main()
