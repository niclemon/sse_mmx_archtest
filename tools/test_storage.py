#!/usr/bin/env python3
"""Exercise the actual C FAT adapter on disposable, independently built images."""
import argparse
import ctypes as C
import os
from pathlib import Path
import shutil
import struct as S
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build' / 'storage-tests'


class Volume(C.Structure):
    _fields_ = [('lba', C.c_uint32), ('sectors', C.c_uint32), ('partition', C.c_uint16),
                ('drive', C.c_uint8), ('fat_bits', C.c_uint8), ('label', C.c_char * 12)]


def make_volume(bits, *, fragmented=False, full=False, root_full=False):
    # Deliberately different cluster sizes exercise both sector and cluster
    # boundaries. The FAT32 fixture exceeds the FAT16 cluster-count cutoff.
    spc = 4 if bits == 16 else 1
    clusters = 5000 if bits == 16 else 66000
    reserved, root_entries = (1, 512) if bits == 16 else (32, 0)
    spf = ((clusters + 2) * (bits // 8) + 511) // 512
    data_lba = reserved + 2 * spf + root_entries * 32 // 512
    total = data_lba + clusters * spc
    image = bytearray(total * 512)
    image[:3] = b'\xeb\x3c\x90'
    image[3:11] = b'FATTEST '
    S.pack_into('<HBHBHH', image, 11, 512, spc, reserved, 2, root_entries, total if total < 65536 else 0)
    image[21] = 0xf8
    S.pack_into('<H', image, 22, spf if bits == 16 else 0)
    if total >= 65536: S.pack_into('<I', image, 32, total)
    if bits == 32:
        S.pack_into('<I', image, 36, spf)
        S.pack_into('<I', image, 44, 2)
        S.pack_into('<HH', image, 48, 1, 6)
        image[66] = 0x29
        image[71:82] = b'LOGTEST    '
        image[82:90] = b'FAT32   '
        S.pack_into('<I', image, 512, 0x41615252)
        S.pack_into('<I', image, 512 + 484, 0x61417272)
        # Intentionally wrong hints: the adapter must scan instead of trusting them.
        S.pack_into('<II', image, 512 + 488, 1, 0xffffffff)
        S.pack_into('<I', image, 512 + 508, 0xaa550000)
    else:
        image[38] = 0x29
        image[43:54] = b'LOGTEST    '
        image[54:62] = b'FAT16   '
    image[510:512] = b'\x55\xaa'
    if bits == 32: image[6*512:7*512] = image[:512]
    fat = bytearray(spf * 512)
    code = '<H' if bits == 16 else '<I'
    eof = 0xffff if bits == 16 else 0x0fffffff
    def entry(cluster, value): S.pack_into(code, fat, cluster * (bits // 8), value)
    entry(0, eof & ~7); entry(1, eof)
    if bits == 32: entry(2, eof)
    # Existing RESULTS.TXT deliberately forces the non-destructive filename path.
    entry(3, eof)
    root = (reserved + 2 * spf) * 512 if bits == 16 else data_lba * 512
    image[root:root+11] = b'RESULTS TXT'
    image[root+11] = 0x20
    S.pack_into('<H', image, root+26, 3)
    S.pack_into('<I', image, root+28, 12)
    old_data = (data_lba + spc) * 512
    image[old_data:old_data+12] = b'KEEP ME SAFE'
    if root_full:
        entries = root_entries if bits == 16 else spc * 16
        for i in range(1, entries):
            offset = root + i * 32
            image[offset:offset+11] = f'F{i:07d}TXT'.encode()
            image[offset+11] = 0x20
    for cluster in range(4, clusters + 2):
        if full or (fragmented and cluster % 2 == 0): entry(cluster, eof)
    for i in range(2): image[(reserved+i*spf)*512:(reserved+(i+1)*spf)*512] = fat
    return image


def partition(volume, *, logical=False):
    base = 4096 if logical else 2048
    result = bytearray(b'\xa5' * (base * 512)) + volume + bytearray(b'\x5a' * 8192)
    result[:512] = bytes(512)
    result[510:512] = b'\x55\xaa'
    fat_type = 6 if volume[54:62] == b'FAT16   ' else 0x0c
    if logical:
        result[450] = 0x0f
        S.pack_into('<II', result, 454, 2048, len(volume)//512+2048)
        ebr = 2048*512
        result[ebr:ebr+512] = bytes(512)
        result[ebr+510:ebr+512] = b'\x55\xaa'
        result[ebr+450] = fat_type
        S.pack_into('<II', result, ebr+454, 2048, len(volume)//512)
    else:
        result[450] = fat_type
        S.pack_into('<II', result, 454, base, len(volume)//512)
    return result, base


def files(image, base):
    """Independent FAT reader; also verify every recorded file chain is in bounds."""
    volume = memoryview(image)[base*512:]
    bits = 16 if S.unpack_from('<H', volume, 22)[0] else 32
    spc, reserved, nfats, entries = volume[13], S.unpack_from('<H', volume, 14)[0], volume[16], S.unpack_from('<H', volume, 17)[0]
    spf = S.unpack_from('<H' if bits == 16 else '<I', volume, 22 if bits == 16 else 36)[0]
    fat = volume[reserved*512:(reserved+spf)*512]
    assert bytes(fat) == bytes(volume[(reserved+spf)*512:(reserved+2*spf)*512]), 'FAT mirrors differ'
    data = reserved + nfats*spf + (entries*32+511)//512
    def chain(c):
        seen = set(); out = bytearray()
        while 2 <= c < (0xfff8 if bits == 16 else 0x0ffffff8):
            assert c not in seen, 'cluster cycle'
            seen.add(c)
            offset = (data + (c-2)*spc)*512
            assert offset + spc*512 <= len(volume), 'cluster outside image'
            out += volume[offset:offset+spc*512]
            c = S.unpack_from('<H' if bits == 16 else '<I', fat, c*(bits//8))[0]
            if bits == 32: c &= 0x0fffffff
        return out
    directory = volume[(reserved+nfats*spf)*512:data*512] if bits == 16 else chain(S.unpack_from('<I', volume, 44)[0])
    result = {}
    for i in range(0, len(directory), 32):
        e = directory[i:i+32]
        if e[0] == 0: break
        if e[0] == 0xe5 or e[11] & 0x18 or e[11] == 0x0f: continue
        name = bytes(e[:8]).decode().rstrip() + '.' + bytes(e[8:11]).decode().rstrip()
        cluster = S.unpack_from('<H', e, 26)[0]
        if bits == 32: cluster |= S.unpack_from('<H', e, 20)[0] << 16
        size = S.unpack_from('<I', e, 28)[0]
        result[name] = bytes(chain(cluster)[:size]) if size else b''
        assert len(result[name]) == size, 'short file chain'
    return result


class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(BUILD / ('storage.dll' if os.name == 'nt' else 'storage.so')))
        cls.lib.test_attach.argtypes = [C.c_char_p, C.c_uint]
        cls.lib.hd_get_volume.restype = C.POINTER(Volume)
        cls.lib.hd_save.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p]
        cls.lib.hd_error.restype = C.c_char_p

    def setUp(self):
        self.fixture = BUILD / uuid.uuid4().hex
        self.fixture.mkdir()
        self.addCleanup(shutil.rmtree, self.fixture)
        self.addCleanup(self.lib.test_close)

    def attach(self, image, index=0):
        path = self.fixture / f'disk{index}.img'
        path.write_bytes(image)
        self.assertEqual(self.lib.test_attach(os.fsencode(path), index), 0)
        return path

    def save(self, payload):
        name = C.create_string_buffer(13)
        data = C.create_string_buffer(payload)
        rc = self.lib.hd_save(data, len(payload), name)
        return rc, name.value.decode()

    def test_create_fragmented_and_preserve_files(self):
        for bits in (16, 32):
            for logical in (False, True):
                with self.subTest(bits=bits, logical=logical):
                    self.lib.test_close()
                    image, base = partition(make_volume(bits, fragmented=True), logical=logical)
                    path = self.attach(image)
                    self.assertEqual(self.lib.hd_scan(), 1)
                    self.assertEqual(self.lib.hd_get_volume(0).contents.fat_bits, bits)
                    self.assertEqual(self.lib.hd_select(0), 0)
                    self.assertEqual(self.lib.test_writes(), 0)
                    payload = bytes(range(256))*39 + b'final partial sector'
                    rc, name = self.save(payload)
                    self.assertEqual(rc, 0, self.lib.hd_error())
                    self.assertEqual(name, 'RES00001.TXT')
                    rc, name2 = self.save(b'second save')
                    self.assertEqual((rc, name2), (0, 'RES00002.TXT'))
                    after = path.read_bytes()
                    found = files(after, base)
                    self.assertEqual(found['RESULTS.TXT'], b'KEEP ME SAFE')
                    self.assertEqual(found[name], payload)
                    self.assertEqual(found[name2], b'second save')
                    self.assertEqual(after[:base*512], image[:base*512])
                    self.assertEqual(after[-8192:], image[-8192:])
                    self.assertEqual(self.lib.test_bad_access(), 0)

    def test_raw_volume_and_empty_file(self):
        image = make_volume(16)
        path = self.attach(image)
        self.assertEqual(self.lib.hd_scan(), 1)
        self.assertEqual(self.lib.hd_select(0), 0)
        self.assertEqual(self.save(b''), (0, 'RES00001.TXT'))
        self.assertEqual(files(path.read_bytes(), 0)['RES00001.TXT'], b'')

    def test_results_name_when_no_previous_log_exists(self):
        image = make_volume(16)
        root = (1 + 2*S.unpack_from('<H', image, 22)[0])*512
        image[root] = 0
        path = self.attach(image)
        self.assertEqual(self.lib.hd_scan(), 1)
        self.assertEqual(self.lib.hd_select(0), 0)
        self.assertEqual(self.save(b'first run'), (0, 'RESULTS.TXT'))
        self.assertEqual(files(path.read_bytes(), 0)['RESULTS.TXT'], b'first run')

    def test_fat32_root_extension(self):
        path = self.attach(make_volume(32, root_full=True))
        self.assertEqual(self.lib.hd_scan(), 1)
        self.assertEqual(self.lib.hd_select(0), 0)
        self.assertEqual(self.save(b'extended root'), (0, 'RES00001.TXT'))
        self.assertEqual(files(path.read_bytes(), 0)['RES00001.TXT'], b'extended root')

    def test_full_disk_and_fat16_full_root(self):
        for bits, full, root_full in [(16, True, False), (32, True, False), (16, False, True)]:
            with self.subTest(bits=bits, full=full):
                self.lib.test_close()
                image = make_volume(bits, full=full, root_full=root_full)
                path = self.attach(image)
                self.assertEqual(self.lib.hd_scan(), 1)
                self.assertEqual(self.lib.hd_select(0), 0)
                self.assertNotEqual(self.save(b'x'*10000)[0], 0)
                self.assertEqual(path.read_bytes(), image)
                self.assertEqual(self.lib.test_writes(), 0)

    def test_failed_write_is_reported(self):
        for bits in (16, 32):
            for fail in (1, 2, 3, 4, 8):
                with self.subTest(bits=bits, fail=fail):
                    self.lib.test_close()
                    image = make_volume(bits)
                    path = self.attach(image)
                    self.assertEqual(self.lib.hd_scan(), 1)
                    self.assertEqual(self.lib.hd_select(0), 0)
                    self.lib.test_fail_write(fail)
                    self.assertNotEqual(self.save(b'x'*20000)[0], 0)
                    # The old payload and its directory entry are never changed.
                    after = path.read_bytes()
                    reserved = S.unpack_from('<H', image, 14)[0]
                    spf = S.unpack_from('<H' if bits == 16 else '<I', image, 22 if bits == 16 else 36)[0]
                    root = (reserved+2*spf)*512
                    self.assertEqual(after[root:root+32], image[root:root+32])
                    self.assertEqual(self.lib.test_bad_access(), 0)

    def test_final_metadata_write_failures_are_reported(self):
        for bits in (16, 32):
            image = make_volume(bits)
            self.lib.test_close()
            self.attach(image)
            self.assertEqual(self.lib.hd_scan(), 1)
            self.assertEqual(self.lib.hd_select(0), 0)
            self.assertEqual(self.save(b'x'*2000)[0], 0)
            count = self.lib.test_writes()
            # Cover the last FAT mirror, directory, and FAT32 FSInfo writes.
            for fail in range(max(1, count-2), count+1):
                with self.subTest(bits=bits, fail=fail):
                    self.lib.test_close()
                    self.attach(image)
                    self.assertEqual(self.lib.hd_scan(), 1)
                    self.assertEqual(self.lib.hd_select(0), 0)
                    self.lib.test_fail_write(fail)
                    self.assertNotEqual(self.save(b'x'*2000)[0], 0)

    def test_dirty_volume_and_divergent_fats_do_not_get_written(self):
        for bits in (16, 32):
            for bad in ('dirty', 'mirror'):
                with self.subTest(bits=bits, bad=bad):
                    self.lib.test_close()
                    image = make_volume(bits)
                    reserved = S.unpack_from('<H', image, 14)[0]
                    spf = S.unpack_from('<H' if bits == 16 else '<I', image, 22 if bits == 16 else 36)[0]
                    if bad == 'dirty': image[reserved*512+(3 if bits == 16 else 7)] &= 0x3f if bits == 16 else 0xf3
                    else: image[(reserved+spf)*512+20] ^= 1
                    path = self.attach(image)
                    count = self.lib.hd_scan()
                    if bad == 'dirty': self.assertEqual(count, 0)
                    else:
                        self.assertEqual(count, 1)
                        self.assertEqual(self.lib.hd_select(0), 0)
                        self.assertNotEqual(self.save(b'do not write')[0], 0)
                    self.assertEqual(path.read_bytes(), image)
                    self.assertEqual(self.lib.test_writes(), 0)

    def test_bad_bpb_and_unsupported_volumes_are_read_only(self):
        for field, data in [(11, b'\x00\x10'), (13, b'\x03'), (36, b'\xff'*4), (40, b'\x80\x00'), (44, b'\x01\x00\x00\x00')]:
            with self.subTest(field=field):
                self.lib.test_close()
                image = make_volume(32)
                image[field:field+len(data)] = data
                path = self.attach(image)
                self.assertEqual(self.lib.hd_scan(), 0)
                self.assertEqual(self.lib.test_writes(), 0)
                self.assertEqual(path.read_bytes(), image)

    def test_multiple_disks_and_selected_partition(self):
        first, base = partition(make_volume(16))
        second, base2 = partition(make_volume(32))
        p0 = self.attach(first, 0); p1 = self.attach(second, 1)
        self.assertEqual(self.lib.hd_scan(), 2)
        self.assertEqual(self.lib.hd_select(1), 0)
        self.assertEqual(self.save(b'chosen disk')[0], 0)
        self.assertEqual(p0.read_bytes(), first)
        self.assertEqual(files(p1.read_bytes(), base2)['RES00001.TXT'], b'chosen disk')

    def test_capture_is_bounded_by_free_space_and_ram(self):
        image = make_volume(16)
        path = self.attach(image)
        self.assertEqual(self.lib.hd_scan(), 1)
        self.assertEqual(self.lib.hd_select(0), 0)
        self.assertEqual(self.lib.hd_capture_capacity(65536), 65536)
        self.assertEqual(self.lib.hd_capture_capacity(64*1024*1024), 4999*2048)
        self.assertEqual(path.read_bytes(), image)
        self.assertEqual(self.lib.test_writes(), 0)

    def test_partition_bounds_and_cycles(self):
        for malformed in ('oversize', 'overlap', 'ebr-cycle', 'logical-overlap', 'gpt'):
            with self.subTest(malformed=malformed):
                self.lib.test_close()
                image, base = partition(make_volume(16), logical=malformed in ('ebr-cycle', 'logical-overlap'))
                if malformed == 'oversize': S.pack_into('<I', image, 458, 0xffffffff)
                elif malformed == 'overlap': image[462:478] = image[446:462]
                elif malformed == 'gpt': image[450] = 0xee
                else:
                    e = 2048*512
                    image[e+466] = 0x0f
                    S.pack_into('<II', image, e+470, 1, 10)
                    image[e+512:e+1024] = image[e:e+512]
                    if malformed == 'ebr-cycle':
                        image[e+512+446:e+512+462] = bytes(16)
                    else:
                        # An unsupported filesystem still owns its range.
                        image[e+512+450] = 7
                        S.pack_into('<I', image, e+512+458, len(make_volume(16))//512-1)
                self.attach(image)
                count = self.lib.hd_scan()
                self.assertEqual(count, 0)
                self.assertEqual(self.lib.test_writes(), 0)
                self.assertEqual(self.lib.test_bad_access(), 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = ap.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    dll = BUILD / ('storage.dll' if os.name == 'nt' else 'storage.so')
    command = [args.cc, '-std=gnu11', '-O2', '-shared', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-Ithird_party/fatfs']
    command += ['-Wl,--export-all-symbols'] if os.name == 'nt' else ['-fPIC']
    subprocess.run(command + ['src/storage.c', 'third_party/fatfs/ff.c', 'tools/storage_runner.c', '-o', str(dll)], cwd=ROOT, check=True)
    unittest.main(argv=['test_storage.py'])


if __name__ == '__main__': main()
