# MMX/SSE1 Architectural Test

A bootable 1.44 MB FAT12 floppy image for checking original MMX and Pentium
III-era SSE1 behavior on legacy BIOS machines, including 86Box/PCBox emulators.
The freestanding 32-bit C harness controls the run, computes reference results,
and records outcomes. Assembly handles exact instruction encodings, register
selection, expected exceptions, and CPU mode transitions.

This is a broad regression suite, not complete ISA conformance coverage. The
[coverage review](docs/COVERAGE.md) describes the checks and their limits.
The suite plans 10,698 outcomes per pass, including integer reference matrices,
guarded stores, comparison/conversion boundaries and saved-state checks.

## Quick start

With a GNU toolchain targeting 32-bit ELF, GNU make, and Python 3 installed:

```sh
make verify
```

Run the nonprivileged subset on the host CPU with `make verify-hosted`.
It checks 6,311 assertions per pass, twice, independently of the boot image.

Mount `dist/sse_mmx_archtest.img` as a writable 1.44 MB floppy and boot a
Pentium III-class machine exposing MMX, FXSR, and SSE. The program asks for:

1. A pass count from 1 to 10,000.
2. `F` for FAIL/SKIP records or `A` for all PASS/FAIL/EXEC/SKIP records.
3. Floppy or a detected FAT16/FAT32 hard-drive volume as the save destination.
4. `Y` to save the captured log, or `N` to discard it.

Tests buffer their log in RAM. Read-only disk discovery happens before the tests;
file writes happen after them. Hard-drive saves create `RESULTS.TXT` or a numbered
file without replacing earlier results. N leaves the hard drive unchanged.
On the floppy, Y replaces its preallocated log and N clears its size, including
any previous saved log. Use a copy of the floppy to preserve earlier results.

The floppy fits about two clean ALL-results passes. Hard-drive capture can use
up to 64 MiB of BIOS-verified RAM. See [hard-drive saving](docs/STORAGE.md) for
supported layouts, capacity limits, and the remaining BIOS validation step.

Extract a saved log from the image used by the emulator:

```sh
make extract IMAGE=/path/to/run.img
```

## Documentation

Each guide owns one topic so instructions and explanations stay consistent:

| Guide | Contents |
| --- | --- |
| [Building and running](docs/BUILD.md) | Dependencies, build targets, Docker, image verification, and log extraction. |
| [Hard-drive saving](docs/STORAGE.md) | FAT16/FAT32 detection, destination selection, RAM limits and failure handling. |
| [Reading the code](docs/READING_THE_CODE.md) | Architecture, source map, test anatomy, bit patterns, assembly, and exception recovery. |
| [Coverage and limitations](docs/COVERAGE.md) | Case inventory, incomplete checks, and useful additions. |
| [Adding tests](docs/ADDING_TESTS.md) | Reference rules, exact assembly probes, and validation steps. |

## Interpreting results

- **PASS:** the case's implemented assertion matched.
- **FAIL:** it did not match; investigate both the target and the reference rule.
- **EXEC:** execution completed without a semantic result or ordering assertion.
- **SKIP:** the assertion was not evaluated, for example after a prerequisite
  failure or for unsupported DAZ behavior.

Read all four counts together. Repeated passes use the same inputs and can expose
state or emulator recompilation bugs. `make verify` checks the built image and
selected source/object invariants; emulator or hardware runs test CPU behavior.
