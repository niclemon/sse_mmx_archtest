# Adding tests

This is the contribution guide for the [MMX/SSE1 Architectural Test](../README.md).
See [Building and running](BUILD.md) for the toolchain and build targets, and
[Reading the code](READING_THE_CODE.md) for the framework and assembly conventions.

## Prefer C when encoding is not the subject of the test

Use the existing C test framework for reference calculations, loops, data matrices, grouping, and logging:

```c
tf_begin("my semantic case");
/* execute a very small inline-assembly probe */
tf_check_u32(actual, expected);
```

Keep the actual SSE/MMX instruction inside a small, auditable inline-assembly helper.

## Prefer `.S` when encoding/state boundaries are part of the test

Use exact assembly for:

- all `imm8` encodings,
- explicit MM/XMM register indices,
- source=destination alias encodings,
- unusual address-size or memory forms,
- exception probes whose exact continuation instruction matters,
- mode/descriptor/IDT mechanics.

Do not use optimizer-visible C labels as exception resume addresses. Store an assembler-local continuation such as `1f` into `fault_resume_eip` immediately before the intentional faulting instruction.

## Oracle rules

A new FAIL should identify one architectural assertion. Avoid compound checks that mix result bits, EFLAGS, MXCSR status, and register preservation unless the failure output identifies which component mismatched.

If one capability is a prerequisite for many derivative cases, probe it once and use `tf_skip_many()` to quarantine the derivatives after a root failure.

For a matrix, use `tf_begin_indexed(name, index)` and document how to decode the
index next to the loop. Use `tf_check_bytes()` for guarded buffers; checking only
the payload misses stores that also overwrite adjacent bytes. Keep fixed seeds
so a reported input row can be reproduced on another target.

Keep status snapshots next to the probe, before calling diagnostic helpers.
Check reserved bits and unexpected flags too, unless the architecture leaves
them undefined. Reserved FXSAVE padding is not a portable expected value.

When a test uses inline GAS AT&T syntax, verify operand order in the compiled object with Intel-syntax `objdump`, especially for irregular instructions such as `MASKMOVQ`.

When a flag-producing instruction is followed by harness guards, snapshot the flags **before** any instruction that modifies EFLAGS. The generated COMISS/UCOMISS probes use `SETO` + `LAHF` before their register guard for this reason.

## After editing

```sh
make clean verify
make verify-hosted
```

If a generator changed:

```sh
make regen
make clean verify
```

Keep the harness flags `-mno-sse -mno-mmx -mno-80387` so ordinary C code cannot consume test architectural state unexpectedly.

Update `tools/coverage_report.py` when the number of recorded outcomes changes,
including skip paths. The hosted runner checks the count for its subset. Check
the full guest count with `python3 tools/coverage_report.py --check-log RESULTS.TXT`
after booting and saving an untruncated log.
