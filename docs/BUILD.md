# Building and running

Build instructions for the [MMX/SSE1 Architectural Test](../README.md).

## Dependencies

Use GCC and GNU binutils capable of producing 32-bit ELF, GNU make, and Python 3.
On Debian/Ubuntu-like systems:

```sh
sudo apt-get install gcc-multilib binutils make python3
make verify
```

The output is `dist/sse_mmx_archtest.img`. A native MinGW compiler/linker normally
targets Windows PE, so it is not a direct substitute for the documented ELF
toolchain. On Windows, use a Linux environment such as WSL, the Docker workflow
below, an appropriately configured cross toolchain, or the checked MSYS2 build
script described below.

The C compiler flags disable generated x87/MMX/SSE instructions so ordinary
harness code does not disturb the state under test. Keep those flags enabled.

## Native Windows build with MSYS2

With MSYS2 Clang/LLD in `clang64/bin` and GNU binutils in `mingw64/bin`:

```powershell
C:/msys64/clang64/bin/python.exe tools/build_windows.py
```

Use `--msys2 D:/msys64` for a different installation root. The script rebuilds
all objects, creates `dist/sse_mmx_archtest.img`, and runs image and regression
checks. C targets `i386-none-elf`; GNU `as --32` handles the GNU assembly syntax.
Use `--output dist/sse_mmx_archtest-audited.img` to build a separate image when
an emulator has the default image mounted. Mount the new output to test it.

Do not convert MinGW assembly objects with plain `objcopy -O elf32-i386` and
link them directly. COFF relative calls use a different displacement origin
from ELF, and this conversion leaves calls targeting **four bytes past** their
function entry. The script normalizes those call addends, rejects unexpected
relocation forms, and verifies the final destinations using retained ELF
relocations. Linux ELF builds do not need this conversion.

## Build targets

| Command | Result |
| --- | --- |
| `make` or `make verify` | Build the image and run structural/source regression checks. |
| `make verify-hosted` | Run the nonprivileged reference suites twice on the host CPU. Requires a native x86 GCC/Clang compiler; override with `HOSTCC=clang`. |
| `make verify-storage` | Test FAT16/FAT32 saving and RAM/log bounds on disposable host fixtures. Override the native compiler with `HOSTCC=clang`. |
| `make image` | Build the floppy image. |
| `make regen` | Regenerate opcode assembly when its Python generator is newer. |
| `make info` | Show kernel and image sizes. |
| `make extract IMAGE=/path/to/run.img` | Extract committed results, defaulting to `RESULTS.TXT`. |
| `make clean` | Remove intermediate build files. |
| `make distclean` | Also remove images and the default extracted result file. |

`build.sh` runs `make verify`. C header dependencies are tracked automatically;
after changing toolchain options, use `make clean verify`. To force regeneration regardless of
timestamps, run `python3 tools/gen_cases.py` and
`python3 tools/gen_operand_forms.py` directly.

Edit the generators together with their generated output. `pavgw_probe.S` stays
handwritten because its address-size encodings are part of the diagnostic.

## Docker

From a POSIX shell in the project directory:

```sh
docker build -t sse-mmx-archtest .
docker run --rm -v "$PWD:/project" -w /project sse-mmx-archtest
```

## Running and saving results

Boot `dist/sse_mmx_archtest.img` as a writable 1.44 MB floppy on a Pentium
III-class machine with MMX, FXSR, and SSE. Choose the pass count and log detail,
select the floppy or a detected FAT16/FAT32 hard-drive volume, then choose whether
to save after testing finishes. See [STORAGE.md](STORAGE.md) for the hard-drive
requirements and supported layouts. Retain the floppy image's FAT12 structure.

The BIOS memory map bounds RAM capture; floppy mode also caps it at the floppy's
available capacity. A truncated log keeps its captured prefix, an explicit marker,
and final counters describing the full run. Writes happen after the test window.
Floppy discard sets its file size to zero, including on a reused image. Hard-drive
discard performs no writes, and hard-drive saves preserve existing files.

The expanded kernel reserves 384 sectors (192 KiB), leaving 1,261,056 bytes for
results in the same 1.44 MB image. Rebuild the boot sector, kernel and image
together; the builder rejects a boot sector with a different reservation. The
extractor reads the BPB and can still extract logs from older 256-sector images.

Extract the committed log from the image the emulator actually modified:

```sh
make extract IMAGE=/path/to/run.img OUT=run-results.txt
```

Equivalent direct command:

```sh
python3 tools/extract_results.py /path/to/run.img run-results.txt
```

## Verification and reproducibility

CI regenerates both assembly files and compares them with the checked-in copies
before running `make verify`, the coverage inventory, hosted checks and storage tests.

The hosted checks also run directly on Windows with a native MinGW GCC/Clang:

```sh
python tools/run_hosted.py --cc /path/to/clang.exe
```

They verify 6,311 assertions per pass and check that two complete passes were
recorded. They exercise actual host instructions, but do not run ring-0 fault,
segmentation, BIOS or boot tests. A successful hosted run is not a guest run.

`tools/verify_image.py` checks FAT12 layout and selected harness invariants.
`--kernel` additionally checks the embedded kernel and compiled CR0 mask;
`--source` additionally checks the mode transitions, logging, fault continuations,
register guards, and reference rules. `--elf build/kernel.elf` checks named
direct CALL/JMP destinations against the linked symbol table; the link must
retain relocations with `--emit-relocs`. Both build paths run this check.
An available MMX object is also checked
for the MASKMOVQ encoding. These checks do not execute the CPU tests.

The root `SHA256SUMS` identifies the distributed image. After intentionally
rebuilding the distributed image, refresh it with:

```sh
sha256sum dist/sse_mmx_archtest.img > SHA256SUMS
```

Keep the machine/emulator configuration with saved run results. See
[Coverage and limitations](COVERAGE.md) for what a successful run establishes.

## Validation of the coverage expansion (2026-09-30)

The original image validation below missed a COFF relocation defect. Its
structural checks and hosted results did not establish that the distributed
image could execute correctly. See the correction and guest run below.

- Windows x86-64 hosted runs passed all 12,622 assertions across two passes with
  Clang 20.1.8 and GCC 15.2.0, separately. Neither run reported failures or skips.
- All C files compiled in 32-bit mode with both compilers and warnings treated
  as errors. Clang produced the ELF objects used for the verification image.
- The image passed `make verify`, including source/object checks and six Python
  layout/accounting regression tests. The coverage-only kernel was 162,224 bytes, within the
  196,608-byte reservation. The distributed image's checksum is in `SHA256SUMS`.
- Both generators reproduced their existing source output in an isolated build
  directory. Generated assembly files were left unchanged.

The local image build used Clang's `i386-none-elf` target, native GNU `as --32`
with `objcopy` conversion of its COFF assembly objects to ELF, and LLD. The
unchanged boot sector matched the previously distributed sector byte for byte
before the reservation increase. The documented Linux GNU build remains the
CI build path; CI itself was not executed in this local session.

Behavioral execution was limited to the hosted subset. The rebuilt image still
needs a BIOS boot run to validate protected-mode faults, segment limits and
disk I/O on the target hardware or emulator.

## Validation of hard-drive saving (2026-09-30)

- All 22 storage and platform tests passed with native GCC 15.2.0 and
  Clang 20.1.8. The tests include file preservation, partition bounds,
  fragmented allocation, full disks, metadata write errors, E820 ranges and
  truncation summaries. They access disposable image fixtures only.
- The 12,622 hosted instruction assertions passed again with both compilers.
- The complete C surface compiled with warnings treated as errors using
  Clang's 32-bit ELF target and, separately, 32-bit MinGW GCC as a COFF compile
  check. The image uses the Clang/GNU assembler/LLD path described above.
- `make verify` passed, including the six layout/accounting tests. The kernel
  is 184,096 bytes within the unchanged 196,608-byte reservation. The image
  remains 1,474,560 bytes with 1,261,056 bytes allocated to the floppy log.
- Linked real-mode addresses were inspected, and linker assertions enforce
  the BIOS segment and stack limits. These checks do not replace executing
  EDD/E820 on a BIOS. That guest smoke test remains outstanding.

## Correction of the Windows image build (2026-09-30)

The previous COFF-to-ELF conversion left 608 assembly call relocations with an
incorrect zero addend. For example, the first baseline case called `tf_begin+4`,
inside an instruction and past the function prologue. Startup and panic calls
were affected too, so the fault-reporting path could itself fail. This accounts
for a crash/reset independently of the selected pass count or log destination.

The corrected build normalizes these addends before linking. The new verifier
checked 2,012 linked direct calls/jumps. Ten tool regression tests passed,
including rejection of a four-byte call overshoot and double normalization.
The rebuilt kernel is 184,112 bytes, within its 196,608-byte reservation.

A QEMU 11.1 development build with `-cpu pentium3`, 64 MiB RAM and SeaBIOS
completed one ALL-results pass: 10,698 outcomes (10,552 PASS, 82 FAIL, 5 EXEC,
59 SKIP). It then saved and extracted a 573,047-byte `RESULTS.TXT` from a
disposable floppy copy; the coverage checker confirmed the complete count.
This validates execution through the test and floppy-save paths, not a clean
architectural result. The old image shut down before reaching its first prompt
under the same QEMU setup. PCBox execution and hard-drive saving remain to be
validated on the user's configuration.
