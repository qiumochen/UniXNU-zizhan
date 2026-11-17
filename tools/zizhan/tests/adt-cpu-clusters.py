#!/usr/bin/env python3
"""Regress the lost AMP cluster properties that prevented early boot."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))
from pong_adt_contract import require_cpu_clusters

spec = importlib.util.spec_from_file_location('actual_adt_encoder', ROOT / 'scripts/make-pong-adt-ipados18.py')
encoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(encoder)


def fixture(types):
    cpus = [encoder.node('cpu' + str(i), [('device_type', b'cpu\0'),
            ('reg', struct.pack('<I', i << 8)),
            *([('cluster-type', value)] if value is not None else [])])
            for i, value in enumerate(types)]
    return encoder.node('device-tree', children=[encoder.node('cpus', children=cpus)])


class ClusterContractTests(unittest.TestCase):
    def test_real_encoder_valid_amp_input(self):
        blob = fixture([struct.pack('<I', ord('E')), struct.pack('<I', ord('P'))])
        self.assertEqual(require_cpu_clusters(blob, 2)['cpu_count'], 2)

    def test_missing_environment_output_refused(self):
        with self.assertRaisesRegex(ValueError, 'every CPU'):
            require_cpu_clusters(fixture([None, None]), 2)

    def test_one_missing_or_invalid_type_refused(self):
        for value in [None, b'E\0', struct.pack('<I', ord('X'))]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                require_cpu_clusters(fixture([struct.pack('<I', ord('E')), value]), 2)

    def test_requested_cpu_count_mismatch_refused(self):
        with self.assertRaisesRegex(ValueError, 'CPU count'):
            require_cpu_clusters(fixture([struct.pack('<I', ord('E'))]), 2)


if __name__ == '__main__':
    unittest.main()
