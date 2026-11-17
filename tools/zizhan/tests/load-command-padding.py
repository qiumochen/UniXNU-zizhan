#!/usr/bin/env python3
"""Reject header growth that overwrites a tiny executable's entry point."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('actual_load_command', ROOT / 'tools/zizhan/mac/add-load-command.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def executable(padding):
    end = 32 + 152 + 24
    entry = end + padding
    data = bytearray(entry + 32)
    struct.pack_into('<8I', data, 0, 0xfeedfacf, 0x100000c, 0, 2, 2, 176, 0, 0)
    struct.pack_into('<II16sQQQQIIII', data, 32, 0x19, 152, b'__TEXT',
                     0x100000000, len(data), 0, len(data), 7, 5, 1, 0)
    struct.pack_into('<16s16sQQIIIIIIII', data, 104, b'__text', b'__TEXT',
                     0x100000000 + entry, 4, entry, 2, 0, 0, 0x80000400, 0, 0, 0)
    struct.pack_into('<IIQQ', data, 184, 0x80000028, 24, entry, 0)
    data[entry:entry + 4] = b'\x01\0\0\x14'
    return bytes(data)


class PaddingTests(unittest.TestCase):
    def test_enough_zero_padding(self):
        self.assertTrue(module.require_padding(executable(128), '/pong/libpong-ui18.dylib'))

    def test_tiny_entry_refused(self):
        before = executable(16)
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            module.require_padding(before, '/pong/libpong-ui18.dylib')
        self.assertEqual(before[-32:-28], b'\x01\0\0\x14')

    def test_existing_overlap_refused(self):
        bad = bytearray(executable(16))
        struct.pack_into('<I', bad, 20, 240)
        with self.assertRaises(ValueError):
            module.payload_ranges(bad)

    def test_optional_metadata_reclaim_preserves_entry(self):
        original = executable(16)
        # Insert optional commands in the synthetic header's padding, then
        # relocate its unchanged tiny payload for a valid packed fixture.
        data = bytearray(original)
        old_end = 208
        metadata = struct.pack('<IIQ', 0x2a, 16, 1) + struct.pack('<IIII', 0x26, 16, 0, 0)
        entry = 272
        data.extend(bytes(entry + 32 - len(data)))
        data[old_end:old_end + len(metadata)] = metadata
        struct.pack_into('<II', data, 16, 4, 208)
        struct.pack_into('<Q', data, 136, 0x100000000 + entry)
        struct.pack_into('<I', data, 152, entry)
        struct.pack_into('<Q', data, 192, entry)
        data[entry:entry + 4] = b'\x01\0\0\x14'
        cleaned, removed = module.reclaim_metadata(bytes(data))
        self.assertEqual(removed, [0x2a, 0x26])
        self.assertEqual(cleaned[entry:entry + 4], data[entry:entry + 4])
        self.assertTrue(module.require_padding(cleaned, '/pong/libpong-ui18.dylib'))

    def test_nonzero_padding_refused(self):
        bad = bytearray(executable(128))
        bad[209] = 1
        with self.assertRaisesRegex(ValueError, 'padding'):
            module.require_padding(bad, '/pong/libpong-ui18.dylib')


if __name__ == '__main__':
    unittest.main()
