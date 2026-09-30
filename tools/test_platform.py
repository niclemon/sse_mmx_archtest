#!/usr/bin/env python3
"""Host checks for RAM range selection and bounded logging, using BIOS stubs."""
import argparse
import ctypes as C
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build' / 'platform-tests'
MIB = 1024 * 1024


class PlatformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(BUILD / ('platform.dll' if os.name == 'nt' else 'platform.so')))
        cls.lib.test_range.argtypes = [C.c_uint64, C.c_uint64, C.c_uint32, C.c_uint32]
        cls.lib.log_puts.argtypes = [C.c_char_p]
        cls.lib.log_filename.restype = C.c_char_p
        cls.ram = (C.c_ubyte * 8192).in_dll(cls.lib, 'test_log_ram')

    def setUp(self):
        self.lib.test_reset()

    def test_contiguous_ram_is_capped(self):
        self.lib.test_range(MIB, 127*MIB, 1, 1)
        self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 64*MIB)

    def test_reserved_hole_trims_usable_range(self):
        self.lib.test_range(15*MIB, MIB, 2, 1)
        self.lib.test_range(MIB, 127*MIB, 1, 1)
        self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 14*MIB)

    def test_reserved_capture_start_is_rejected(self):
        self.lib.test_range(MIB, 127*MIB, 1, 1)
        self.lib.test_range(MIB-512, 1024, 3, 1)
        self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 0)

    def test_disabled_ranges_and_address_wrap(self):
        self.lib.test_range(MIB, 127*MIB, 1, 0)
        self.lib.test_range(0xffffffffffff0000, 0x20000, 1, 1)
        self.lib.test_range(2*MIB, 50*MIB, 1, 1)
        self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 0)

    def test_hardware_error_log_is_not_scratch_memory(self):
        for attributes in (3, 9):
            self.lib.test_reset()
            self.lib.test_range(MIB, 127*MIB, 1, attributes)
            self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 0)

    def test_map_error_does_not_use_partial_results(self):
        self.lib.test_range(MIB, 127*MIB, 1, 1)
        self.lib.test_map_error(1)
        self.assertEqual(self.lib.disk_log_memory_capacity(64*MIB), 0)

    def test_truncation_keeps_summary_inside_buffer(self):
        self.lib.log_configure(4096, 1, 0)
        self.lib.log_prepare(2, 1)
        self.lib.log_puts(b'x'*10000)
        self.assertEqual(self.lib.log_size(), 4096-512)
        self.assertTrue(self.lib.log_is_truncated())
        self.lib.log_begin_summary()
        self.lib.log_puts(b'SUMMARY final counters\r\nAGGREGATE final counters\r\n')
        data = bytes(self.ram[:self.lib.log_size()])
        self.assertIn(b'TRUNCATED', data)
        self.assertTrue(data.endswith(b'AGGREGATE final counters\r\n'))
        self.assertLessEqual(len(data), 4096)
        self.assertEqual(bytes(self.ram[4096:]), b'\xa5'*4096)
        self.assertEqual(self.lib.test_disk_writes(), 0)
        self.assertEqual(self.lib.test_hd_writes(), 0)

    def test_disk_work_is_deferred_and_targets_are_separate(self):
        for hdd in (0, 1):
            with self.subTest(hdd=hdd):
                self.lib.test_reset()
                self.lib.log_configure(4096, hdd, 0)
                self.lib.log_prepare(1, 1)
                self.lib.log_puts(b'captured log\r\n')
                self.assertEqual(self.lib.test_disk_writes(), 0)
                self.assertEqual(self.lib.test_hd_writes(), 0)
                self.assertEqual(self.lib.log_commit(), 0)
                if hdd:
                    self.assertEqual(self.lib.test_hd_writes(), 1)
                    self.assertEqual(self.lib.test_disk_writes(), 0)
                else:
                    self.assertGreater(self.lib.test_disk_writes(), 0)
                    self.assertEqual(self.lib.test_hd_writes(), 0)

    def test_hdd_discard_does_not_touch_floppy_or_old_files(self):
        self.lib.log_configure(4096, 1, 0)
        self.lib.log_prepare(1, 0)
        self.lib.log_discard()
        self.assertEqual(self.lib.test_disk_writes(), 0)
        self.assertEqual(self.lib.test_hd_writes(), 0)

    def test_save_failure_and_floppy_capacity(self):
        self.lib.log_configure(64*MIB, 0, 0)
        self.assertLess(self.lib.log_capacity(), 2*MIB)
        self.lib.log_configure(4096, 1, 0)
        self.lib.log_prepare(1, 0)
        self.lib.test_save_error(1)
        self.assertNotEqual(self.lib.log_commit(), 0)
        self.assertTrue(self.lib.log_has_io_error())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = ap.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    lib = BUILD / ('platform.dll' if os.name == 'nt' else 'platform.so')
    command = [args.cc, '-std=gnu11', '-O2', '-shared', '-Wall', '-Wextra', '-Werror', '-DARCHTEST_HOSTED_LOG', '-Iinclude']
    command += ['-Wl,--export-all-symbols'] if os.name == 'nt' else ['-fPIC']
    subprocess.run(command + ['src/disk.c', 'src/log.c', 'tools/platform_runner.c', '-o', str(lib)], cwd=ROOT, check=True)
    unittest.main(argv=['test_platform.py'])


if __name__ == '__main__': main()
