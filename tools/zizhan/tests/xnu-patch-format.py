#!/usr/bin/env python3
"""Regress the BOOT-01 patch format that broke repeat kernel builds."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

PATCH = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0019-zizhan-cs-cache-map-diagnostics.patch"
PATCH_0045 = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0045-zizhan-ux-hygiene-upl-fastpath.patch"
PATCH_0046 = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0046-zizhan-platform-sleep.patch"
PATCH_0047 = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0047-zizhan-app-inject-and-cam-sysctl.patch"
PATCH_0048 = Path(__file__).resolve().parents[3] / "patches/xnu-ipados18/0048-zizhan-cover-power-key.patch"


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

    def test_ux_hygiene_upl_patch_roundtrip(self):
        text = PATCH_0045.read_text()
        files = {}
        cur = None
        for line in text.splitlines():
            if line.startswith("--- a/"):
                cur = line[6:]
                files[cur] = []
            elif line.startswith("+++ b/"):
                continue
            elif line.startswith("@@ ") and cur:
                start = int(re.match(r"@@ -(\d+)", line)[1])
                while len(files[cur]) < start - 1:
                    files[cur].append(f"/* unused context {len(files[cur])} */")
            elif cur and line.startswith((" ", "-")):
                files[cur].append(line[1:])
        originals = {rel: "\n".join(src) + "\n" for rel, src in files.items()}
        with tempfile.TemporaryDirectory(prefix="zizhan-ux-patch-") as td:
            for rel, content in originals.items():
                p = Path(td) / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
            for flags in (["--check"], [], ["--reverse", "--check"], ["--reverse"]):
                subprocess.run(["git", "apply", *flags, str(PATCH_0045)], cwd=td, check=True,
                               capture_output=True, timeout=10)
            for rel, content in originals.items():
                self.assertEqual((Path(td) / rel).read_text(), content)

    def test_platform_sleep_patch_roundtrip(self):
        text = PATCH_0046.read_text()
        files = {}
        cur = None
        for line in text.splitlines():
            if line.startswith("--- a/"):
                cur = line[6:]
                files[cur] = []
            elif line.startswith("+++ b/"):
                continue
            elif line.startswith("@@ ") and cur:
                start = int(re.match(r"@@ -(\d+)", line)[1])
                while len(files[cur]) < start - 1:
                    files[cur].append(f"/* unused context {len(files[cur])} */")
            elif cur and line.startswith((" ", "-")):
                files[cur].append(line[1:])
        originals = {rel: "\n".join(src) + "\n" for rel, src in files.items()}
        with tempfile.TemporaryDirectory(prefix="zizhan-sleep-patch-") as td:
            for rel, content in originals.items():
                p = Path(td) / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
            for flags in (["--check"], [], ["--reverse", "--check"], ["--reverse"]):
                subprocess.run(["git", "apply", *flags, str(PATCH_0046)], cwd=td, check=True,
                               capture_output=True, timeout=10)
            for rel, content in originals.items():
                self.assertEqual((Path(td) / rel).read_text(), content)

    def test_app_inject_cam_sysctl_patch_roundtrip(self):
        text = PATCH_0047.read_text()
        files = {}
        cur = None
        for line in text.splitlines():
            if line.startswith("--- a/"):
                cur = line[6:]
                files[cur] = []
            elif line.startswith("+++ b/"):
                continue
            elif line.startswith("@@ ") and cur:
                start = int(re.match(r"@@ -(\d+)", line)[1])
                while len(files[cur]) < start - 1:
                    files[cur].append(f"/* unused context {len(files[cur])} */")
            elif cur and line.startswith((" ", "-")):
                files[cur].append(line[1:])
        originals = {rel: "\n".join(src) + "\n" for rel, src in files.items()}
        with tempfile.TemporaryDirectory(prefix="zizhan-cam-patch-") as td:
            for rel, content in originals.items():
                p = Path(td) / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
            for flags in (["--check"], [], ["--reverse", "--check"], ["--reverse"]):
                subprocess.run(["git", "apply", *flags, str(PATCH_0047)], cwd=td, check=True,
                               capture_output=True, timeout=10)
            for rel, content in originals.items():
                self.assertEqual((Path(td) / rel).read_text(), content)

    def test_cover_power_key_patch_roundtrip(self):
        text = PATCH_0048.read_text()
        files = {}
        cur = None
        for line in text.splitlines():
            if line.startswith("--- a/"):
                cur = line[6:]
                files[cur] = []
            elif line.startswith("+++ b/"):
                continue
            elif line.startswith("@@ ") and cur:
                start = int(re.match(r"@@ -(\d+)", line)[1])
                while len(files[cur]) < start - 1:
                    files[cur].append(f"/* unused context {len(files[cur])} */")
            elif cur and line.startswith((" ", "-")):
                files[cur].append(line[1:])
        originals = {rel: "\n".join(src) + "\n" for rel, src in files.items()}
        with tempfile.TemporaryDirectory(prefix="zizhan-cover-pwr-patch-") as td:
            for rel, content in originals.items():
                p = Path(td) / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
            for flags in (["--check"], [], ["--reverse", "--check"], ["--reverse"]):
                subprocess.run(["git", "apply", *flags, str(PATCH_0048)], cwd=td, check=True,
                               capture_output=True, timeout=10)
            for rel, content in originals.items():
                self.assertEqual((Path(td) / rel).read_text(), content)


if __name__ == "__main__":
    unittest.main()
