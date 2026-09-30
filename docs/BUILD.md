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
below, or an appropriately configured cross toolchain.

The C compiler flags disable generated x87/MMX/SSE instructions so ordinary
harness code does not disturb the state under test. Keep those flags enabled.

## Build targets

| Command | Result |
| --- | --- |
| `make` or `make verify` | Build the image and run structural/source regression checks. |
| `make image` | Build the floppy image. |
| `make regen` | Regenerate opcode assembly when its Python generator is newer. |
| `make info` | Show kernel and image sizes. |
| `make extract IMAGE=/path/to/run.img` | Extract committed results, defaulting to `RESULTS.TXT`. |
| `make clean` | Remove intermediate build files. |
| `make distclean` | Also remove images and the default extracted result file. |

`build.sh` runs `make verify`. After changing headers or toolchain options, use
`make clean verify` to avoid stale objects. To force regeneration regardless of
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
then choose whether to save after testing finishes. The logger uses a fixed RAM
buffer and the image builder's preallocated FAT12 layout; retain the image's
filesystem structure.

The log is capped at the floppy's available capacity. A truncation warning means
only the captured prefix can be saved; counters still describe the full run.
Both save and discard perform disk I/O after the test window. Discard sets the
file size to zero, including on an image that contained an older log.

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
before running `make verify` and the coverage inventory.

`tools/verify_image.py` checks FAT12 layout and selected harness invariants.
`--kernel` additionally checks the embedded kernel and compiled CR0 mask;
`--source` additionally checks the mode transitions, logging, fault continuations,
register guards, and reference rules. An available MMX object is also checked
for the MASKMOVQ encoding. These checks do not execute the CPU tests.

The root `SHA256SUMS` identifies the distributed image. After intentionally
rebuilding the distributed image, refresh it with:

```sh
sha256sum dist/sse_mmx_archtest.img > SHA256SUMS
```

Keep the machine/emulator configuration with saved run results. See
[Coverage and limitations](COVERAGE.md) for what a successful run establishes.
