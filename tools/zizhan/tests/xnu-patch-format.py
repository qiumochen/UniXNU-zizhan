#!/usr/bin/env python3
"""Regress the BOOT-01 patch format that broke repeat kernel builds."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

PATCH = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0019-zizhan-cs-cache-map-diagnostics.patch"


class XnuPatchFormatTests(unittest.TestCase):
    def test_actual_patch_roundtrip(self):
        text = PATCH.read_text()
        source = []
        for section in re.split(r"(?=^@@ )", text, flags=re.MULTILINE)[1:]:
            header, *lines = section.splitlines()
            start = int(re.match(r"@@ -(\d+)", header)[1])
            while len(source) < start - 1:
                source.append(f"/* unused context {len(source)} */")
            source.extend(line[1:] for line in lines if line.startswith((" ", "-")))
        before = "\n".join(source) + "\n"
        with tempfile.TemporaryDirectory(prefix="zizhan-cs-patch-") as td:
            file = Path(td) / "bsd/kern/ubc_subr.c"
            file.parent.mkdir(parents=True)
            file.write_text(before)
            for flags in (["--check"], [], ["--reverse", "--check"], ["--reverse"]):
                subprocess.run(["git", "apply", *flags, str(PATCH)], cwd=td, check=True,
                               capture_output=True, timeout=10)
            self.assertEqual(file.read_text(), before)

    def test_missing_context_prefix_negative_control(self):
        broken = PATCH.read_text().replace(" \t\treturn ENOMEM;", "\t\treturn ENOMEM;")
        result = subprocess.run(["git", "apply", "--numstat", "-"], input=broken,
                                text=True, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("corrupt patch", result.stderr)


if __name__ == "__main__":
    unittest.main()
