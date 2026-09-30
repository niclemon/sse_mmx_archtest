#!/usr/bin/env python3
"""Build with MSYS2 Clang/LLD and MinGW GNU as/objcopy, without WSL.

The COFF assembly conversion MUST normalize call addends before linking.
The final ELF keeps relocations so verification checks actual call destinations.
"""
import argparse
from pathlib import Path
import subprocess
import shutil
import sys

from elf_calls import normalize_coff_calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--msys2', type=Path, default=Path('C:/msys64'))
    parser.add_argument('--output', type=Path, default=Path('dist/sse_mmx_archtest.img'))
    cleanup = parser.add_mutually_exclusive_group()
    cleanup.add_argument('--clean', action='store_true', help='remove build artifacts only')
    cleanup.add_argument('--distclean', action='store_true', help='also remove dist and RESULTS.TXT')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if args.clean or args.distclean:
        names = ('build', 'dist', 'RESULTS.TXT') if args.distclean else ('build',)
        targets = [root / name for name in names]
        # Verify every resolved target before removing anything. Never follow
        # an artifact directory that was redirected outside this checkout.
        for target in targets:
            if target.resolve().parent != root or target.is_symlink():
                parser.error(f'refusing cleanup outside the project: {target}')
        for target in targets:
            if target.is_dir():
                shutil.rmtree(target)
            elif target.exists():
                target.unlink()
        return
    clang = args.msys2 / 'clang64/bin/clang.exe'
    linker = args.msys2 / 'clang64/bin/ld.lld.exe'
    assembler = args.msys2 / 'mingw64/bin/as.exe'
    objcopy = args.msys2 / 'mingw64/bin/objcopy.exe'
    for tool in (clang, linker, assembler, objcopy):
        if not tool.is_file():
            parser.error(f'missing tool: {tool}')

    def run(command):
        subprocess.run([str(x) for x in command], cwd=root, check=True)

    # Recompile everything: never reuse objects from another toolchain.
    cflags = ['--target=i386-none-elf', '-m32', '-std=gnu11', '-O2',
              '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
              '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
              '-mno-sse', '-mno-mmx', '-mno-80387', '-Wall', '-Wextra', '-Werror',
              '-MMD', '-MP', '-Iinclude', '-Iinclude/libc', '-Ithird_party/fatfs']
    objects = []
    for source in sorted(root.glob('src/**/*.c')) + [root / 'third_party/fatfs/ff.c']:
        obj = (root / 'build' / source.relative_to(root / 'src')).with_suffix('.c.o') \
            if source.is_relative_to(root / 'src') else root / 'build/fatfs.o'
        obj.parent.mkdir(parents=True, exist_ok=True)
        run([clang, *cflags, '-c', source, '-o', obj])
        objects.append(obj)

    fixed = 0
    assembly = ['entry', 'faults', 'tests/baseline', 'tests/generated_ops',
                'tests/operand_forms', 'tests/pavgw_probe', 'boot']
    for name in assembly:
        obj = root / f'build/{name}.S.o'
        obj.parent.mkdir(parents=True, exist_ok=True)
        preprocessed = obj.with_suffix('.i')
        coff = obj.with_suffix('.coff')
        run([clang, '--target=i386-none-elf', '-E', '-Iinclude',
             root / f'src/{name}.S', '-o', preprocessed])
        run([assembler, '--32', preprocessed, '-o', coff])
        run([objcopy, '-I', 'pe-i386', '-O', 'elf32-i386', coff, obj])
        fixed += normalize_coff_calls(obj)
        if name != 'boot':
            objects.append(obj)

    run([linker, '-m', 'elf_i386', '-nostdlib', '--emit-relocs',
         '-T', 'linker/kernel.ld', '-Map=build/kernel.map',
         '-o', 'build/kernel.elf', *objects])
    run([linker, '-m', 'elf_i386', '-nostdlib', '-T', 'linker/boot.ld',
         '-o', 'build/boot.elf', 'build/boot.S.o'])
    for name in ('boot', 'kernel'):
        run([objcopy, '-O', 'binary', f'build/{name}.elf', f'build/{name}.bin'])
    (root / 'dist').mkdir(exist_ok=True)
    run([sys.executable, 'tools/build_image.py', '--boot', 'build/boot.bin',
         '--kernel', 'build/kernel.bin', '--output', args.output])
    run([sys.executable, 'tools/verify_image.py', args.output,
         '--kernel', 'build/kernel.bin', '--source', 'src/entry.S',
         '--elf', 'build/kernel.elf'])
    run([sys.executable, 'tools/test_tools.py'])
    print(f'Windows build OK; normalized {fixed} COFF call relocations')


if __name__ == '__main__':
    main()
