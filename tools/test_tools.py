#!/usr/bin/env python3
"""Regression checks for image layout changes and outcome accounting."""
from pathlib import Path
import struct
import subprocess
import sys
import shutil
import uuid
import unittest

import build_image as layout
from coverage_report import CASES_PER_PASS, check_log

TOOLS = Path(__file__).resolve().parent


class ImageTests(unittest.TestCase):
    def setUp(self):
        # Keep fixtures inside the workspace. This also works in restricted
        # Windows sessions where the system temp directory is not writable.
        fixtures = TOOLS.parent / "build" / "tool-tests"
        fixtures.mkdir(parents=True, exist_ok=True)
        self.root = fixtures / uuid.uuid4().hex
        self.root.mkdir()
        self.assertEqual(self.root.resolve().parent, fixtures.resolve())
        self.addCleanup(shutil.rmtree, self.root)
        self.boot = self.root / "boot.bin"
        self.kernel = self.root / "kernel.bin"
        self.image = self.root / "test.img"
        boot = bytearray(512)
        boot[510:] = b"\x55\xaa"
        boot[54:62] = b"FAT12   "
        boot[16] = 2
        struct.pack_into("<H", boot, 14, layout.RESERVED)
        struct.pack_into("<H", boot, 17, 224)
        struct.pack_into("<H", boot, 22, 9)
        self.boot.write_bytes(boot)
        # Exceed the former 128 KiB reservation to catch a stale layout constant.
        self.kernel.write_bytes(bytes(range(256)) * 600)

    def build(self):
        return subprocess.run([sys.executable, str(TOOLS / "build_image.py"),
                               "--boot", str(self.boot), "--kernel", str(self.kernel),
                               "--output", str(self.image)], capture_output=True, text=True)

    def test_expanded_kernel_and_fat_layout(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        image = self.image.read_bytes()
        self.assertEqual(image[512:512+self.kernel.stat().st_size], self.kernel.read_bytes())
        result = subprocess.run([sys.executable, str(TOOLS / "verify_image.py"), str(self.image)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_old_boot_sector_is_rejected(self):
        boot = bytearray(self.boot.read_bytes())
        struct.pack_into("<H", boot, 14, 257)
        self.boot.write_bytes(boot)
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("reserved-sector count", result.stderr)
        self.assertFalse(self.image.exists())

    def test_kernel_cannot_overwrite_fat(self):
        self.kernel.write_bytes(b"x" * (layout.KERNEL_SECTORS * 512 + 1))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("maximum", result.stderr)
        self.assertFalse(self.image.exists())

    def test_extract_log_across_sectors(self):
        self.assertEqual(self.build().returncode, 0)
        image = bytearray(self.image.read_bytes())
        payload = b"log line\r\n" * 100
        struct.pack_into("<I", image, layout.ROOT * 512 + 28, len(payload))
        image[layout.DATA*512:layout.DATA*512+len(payload)] = payload
        self.image.write_bytes(image)
        out = self.root / "results.txt"
        result = subprocess.run([sys.executable, str(TOOLS / "extract_results.py"),
                                 str(self.image), str(out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.read_bytes(), payload)


class InventoryTests(unittest.TestCase):
    def test_skips_and_failures_still_count(self):
        total = CASES_PER_PASS * 3
        log = (f"AGGREGATE configured-passes=00000003 total={total:08x} "
               f"pass={total-26:08x} fail=00000002 exec=0000000f skip=00000009\r\n")
        self.assertEqual(check_log(log), (3, total))
        with self.assertRaisesRegex(ValueError, 'record capture was truncated'):
            check_log('[TRUNCATED: record capture filled]\r\n' + log)

    def test_missing_or_inconsistent_records_are_rejected(self):
        invalid = ["truncated before summary", "AGGREGATE configured-passes=1 total=1 pass=1 fail=0 exec=0 skip=0",
                   f"AGGREGATE configured-passes=1 total={CASES_PER_PASS:x} pass=0 fail=0 exec=0 skip=0"]
        for log in invalid:
            with self.subTest(log=log), self.assertRaises(ValueError):
                check_log(log)


if __name__ == "__main__":
    unittest.main()
