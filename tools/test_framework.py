#!/usr/bin/env python3
"""Exercise the real C result formatter and Python parser with diagnostic data."""
import argparse
import ctypes as C
import os
from pathlib import Path
import subprocess
import unittest

from parse_results import parse_log, parse_record
from coverage_report import CASES_PER_PASS, check_log

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build' / 'framework-tests'


class FrameworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(BUILD / ('framework.dll' if os.name == 'nt' else 'framework.so')))
        for name in ('tf_begin', 'tf_group', 'tf_set_detail', 'tf_skip', 'tf_fail_text'):
            getattr(cls.lib, name).argtypes = [C.c_char_p]
        cls.lib.tf_skip_many.argtypes = [C.c_char_p, C.c_char_p, C.c_uint32]
        cls.lib.tf_fail_guest_reg.argtypes = [C.c_char_p, C.c_uint32, C.c_uint32]
        cls.lib.tf_begin_indexed.argtypes = [C.c_char_p, C.c_uint32]
        for name in ('tf_check_u32', 'tf_check_fault'):
            getattr(cls.lib, name).argtypes = [C.c_uint32, C.c_uint32]
        cls.lib.tf_check_mask_u32.argtypes = [C.c_uint32] * 3
        for name in ('tf_check_u64', 'tf_check_u128', 'tf_check_approx4', 'tf_check_approx_scalar'):
            getattr(cls.lib, name).argtypes = [C.c_void_p, C.c_void_p]
        for name in ('tf_check_bytes', 'tf_check_qnan_vector'):
            getattr(cls.lib, name).argtypes = [C.c_void_p, C.c_void_p, C.c_uint32]
        cls.lib.tf_check_property.argtypes = [C.c_void_p, C.c_uint32, C.c_int, C.c_char_p]
        cls.ram = (C.c_ubyte * 8192).in_dll(cls.lib, 'test_log_ram')
        cls.mode = C.c_uint32.in_dll(cls.lib, 'g_log_mode')

    def setUp(self):
        self.lib.test_reset()
        self.lib.tf_reset_counts()
        self.lib.log_configure(8192, 1, 0)
        self.lib.log_prepare(1, 1)
        self.mode.value = 1
        C.c_uint32.in_dll(self.lib, 'g_current_pass').value = 1
        self.lib.tf_group(b'formatter tests')
        self.lib.tf_begin(b'PADDB example')

    def text(self):
        return bytes(self.ram[:self.lib.log_size()]).decode('ascii')

    def records(self):
        return list(parse_log(self.text()))

    def test_exact_masked_and_fault_values_on_pass_and_fail(self):
        self.lib.tf_check_u32(0x12345678, 0x12345678)
        self.lib.tf_check_u32(0x12345678, 0x87654321)
        self.lib.tf_check_mask_u32(0xdead0004, 4, 0xff)
        self.lib.tf_check_fault(0xffffffff, 13)
        a, b, masked, fault = self.records()
        self.assertEqual((a['status'], a['expected'], a['actual']), ('PASS', '0x12345678', '0x12345678'))
        self.assertEqual((b['status'], b['expected'], b['actual']), ('FAIL', '0x87654321', '0x12345678'))
        self.assertEqual((masked['status'], masked['actual'], masked['mask']), ('PASS', '0xDEAD0004', '0x000000FF'))
        self.assertEqual((fault['check'], fault['actual']), ('fault-vector', '0xFFFFFFFF'))

    def test_vector_order_and_full_guard_buffer(self):
        actual = (C.c_ubyte * 16)(*range(16))
        expected = (C.c_ubyte * 16)(*range(16))
        self.lib.tf_check_u64(actual, expected)
        self.lib.tf_check_u128(actual, expected)
        expected[7] = 99
        self.lib.tf_check_bytes(actual, expected, 16)
        u64, u128, buf = self.records()
        self.assertEqual(u64['actual'], '0x0706050403020100')
        self.assertEqual(u128['actual'], '0x0F0E0D0C0B0A09080706050403020100')
        self.assertEqual(buf['actual'], '0x000102030405060708090A0B0C0D0E0F')
        self.assertEqual(buf['expected'], '0x000102030405066308090A0B0C0D0E0F')
        self.assertEqual(buf['first_mismatch'], '7')

    def test_approximation_and_nan_contracts(self):
        expected = (C.c_uint32 * 4)(*[0x3f800000] * 4)
        actual = (C.c_uint32 * 4)(*[0x3f800000 - 6144] * 4)
        self.lib.tf_check_approx4(actual, expected)
        actual[0] -= 1
        self.lib.tf_check_approx4(actual, expected)
        actual[:] = expected[:]
        actual[0] += 3072
        self.lib.tf_check_approx_scalar(actual, expected)
        actual[1] += 1  # Upper scalar lanes still require exact equality.
        self.lib.tf_check_approx_scalar(actual, expected)
        actual[:] = expected[:]
        actual[0] = 0xffc12345
        self.lib.tf_check_qnan_vector(actual, expected, 1)
        actual[0] = 0x7fa12345  # Signaling NaN must fail the quiet-class check.
        self.lib.tf_check_qnan_vector(actual, expected, 1)
        records = self.records()
        self.assertEqual([r['status'] for r in records], ['PASS', 'FAIL', 'PASS', 'FAIL', 'PASS', 'FAIL'])
        self.assertEqual(records[0]['relative_bound'], '3/8192')
        self.assertEqual(records[-1]['qnan_mask'], '0x00000001')

    def test_property_captures_raw_value_without_exact_oracle(self):
        actual = C.c_uint32(0xffc12345)
        self.lib.tf_check_property(C.byref(actual), 4, 1, b'quiet-NaN; payload unspecified')
        r = self.records()[0]
        self.assertEqual((r['check'], r['expected'], r['actual']),
                         ('property', 'quiet-NaN; payload unspecified', '0xFFC12345'))

    def test_text_escaping_and_counted_skip(self):
        name = b'XORPS\tname=with\\escapes\r\nFAIL\x01'
        self.lib.tf_begin(name)
        self.lib.tf_set_detail(b'detail\twith=equals')
        self.lib.tf_check_u32(1, 1)
        self.lib.tf_skip_many(b'SQRTSS group', b'prerequisite\nmissing', 55)
        records = self.records()
        self.assertEqual(len(records), 2)
        self.assertEqual(records[0]['test'], name.decode())
        self.assertEqual(records[0]['detail'], 'detail\twith=equals')
        self.assertEqual((records[1]['status'], records[1]['count'], records[1]['id']), ('SKIP', 55, 56))
        self.assertEqual(records[1]['reason'], 'prerequisite\nmissing')

    def test_failure_mode_and_execution_only(self):
        self.mode.value = 0
        self.lib.tf_check_u32(1, 1)
        self.lib.tf_exec()
        self.lib.tf_check_u32(1, 2)
        self.lib.tf_skip(b'unsupported')
        self.assertEqual([r['status'] for r in self.records()], ['FAIL', 'SKIP'])
        self.assertEqual([r['id'] for r in self.records()], [3, 4])
        self.mode.value = 1
        self.lib.tf_exec()
        self.assertEqual(self.records()[-1]['expected'], 'not-asserted')

    def test_guest_register_and_case_detail(self):
        self.lib.tf_begin_indexed(b'PADDB self', 0xab)
        self.lib.tf_fail_guest_reg(b'EDI', 0x12345678, 0)
        r = self.records()[0]
        self.assertEqual((r['register'], r['detail']), ('EDI', 'case=0x000000ab'))
        self.assertEqual((r['expected'], r['actual']), ('0x12345678', '0x00000000'))

    def test_parser_rejects_damage_and_truncation(self):
        self.lib.tf_check_u32(1, 1)
        line = next(x for x in self.text().splitlines() if x.startswith('PASS\t'))
        for damaged in (line + '\tcount=1', line.replace('count=1', 'count=0'),
                        line.replace('actual=0x00000001', 'actual=\\z'), line.split('\tactual=')[0]):
            with self.subTest(damaged=damaged), self.assertRaises(ValueError):
                parse_record(damaged)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            list(parse_log(self.text() + '[TRUNCATED: test]'))

    def test_coverage_checks_records_not_only_totals(self):
        self.lib.tf_skip_many(b'all cases', b'fixture', CASES_PER_PASS)
        log = self.text() + (f'AGGREGATE configured-passes=1 total={CASES_PER_PASS:x} '
                             f'pass=0 fail=0 exec=0 skip={CASES_PER_PASS:x}\r\n')
        self.assertEqual(check_log(log), (1, CASES_PER_PASS))
        with self.assertRaises(ValueError):
            check_log(log.replace(f'count={CASES_PER_PASS}', 'count=1'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    library = BUILD / ('framework.dll' if os.name == 'nt' else 'framework.so')
    command = [args.cc, '-std=gnu11', '-O2', '-shared', '-Wall', '-Wextra', '-Werror',
               '-mno-sse', '-mno-mmx', '-mno-80387', '-DARCHTEST_HOSTED_LOG', '-Iinclude']
    command += ['-Wl,--export-all-symbols'] if os.name == 'nt' else ['-fPIC']
    subprocess.run(command + ['src/testfw.c', 'src/log.c', 'src/disk.c', 'tools/platform_runner.c',
                              'tools/framework_stubs.c', '-o', str(library)], cwd=ROOT, check=True)
    unittest.main(argv=['test_framework.py'])


if __name__ == '__main__':
    main()
