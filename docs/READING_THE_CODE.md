# Reading the code

The [MMX/SSE1 Architectural Test](../README.md) is a small bare-metal program,
including its own boot loader and console. It runs directly on a BIOS machine
or an emulator, without an operating
system. Its intended instruction set is original MMX plus Pentium III-era SSE1.
It does not test all later SSE generations.

The suite has broad regression coverage, but it is not a complete conformance
test. Read [COVERAGE.md](COVERAGE.md) for the specific limits and next additions.

## Start here

| File | What to look for |
| --- | --- |
| `src/main.c` | The prompts, suite order, repeated passes, and final save decision. |
| `src/testfw.c` | How a named assertion becomes PASS, FAIL, EXEC, or SKIP. |
| `include/archtest.h` | Register bits, vector layout, counters, and fixed memory/disk layout. |
| `src/tests/immediates.c` | A readable example of a hardware result checked against integer C reference calculations. |
| `src/tests/mmx_edges.c` | Saturation, shifts, packing, and diagnostic input patterns. |
| `src/tests/sse_fp_edges.c` / `sse_packed_edges.c` | Floating-point classes, scalar lanes, comparisons, and status flags. |
| `src/tests/mxcsr_edges.c` | Rounding, trap masks, and prerequisite failures. |
| `src/faults.c` / `faults.S` | How an intentional CPU exception returns to the test. |
| `src/tests/baseline.S` | Fixed operands, exact instruction forms, and expected vectors. |
| `tools/gen_cases.py` / `gen_operand_forms.py` | The source of the two generated assembly files. |

The code comments explain the local assumptions. The rest of this guide connects
the pieces so you do not have to start by reading thousands of generated probes.

## From boot to results

1. **Load the kernel.** The BIOS loads `boot.S` at physical address `0x7c00`.
   This loader copies 256 reserved sectors to `0x10000`. It uses fixed sectors,
   not a FAT file lookup.
2. **Enter protected mode.** `entry.S` enables A20, configures CR0/CR4, loads a
   global descriptor table (GDT), and switches to 32-bit code. It zeroes BSS and
   installs the exception table (IDT) before calling `kernel_main()`.
3. **Choose a run.** `main.c` checks advertised CPU features and reads the pass
   count and logging mode. The boot environment already assumes a suitable CPU;
   this is not a universal loader for machines without SSE support.
4. **Run the suites.** Each pass resets control state, executes the baseline and
   edge suites, and restores control state. Ordinary C cannot use floating-point
   or SIMD instructions because the build disables compiler-generated x87/MMX/SSE.
5. **Record outcomes.** VGA/COM1 display progress. Log text accumulates in physical
   RAM from `0x100000`, capped at 1,326,592 bytes. This region is assumed available;
   there is no BIOS memory-map allocation.
6. **Finish disk work.** After testing, `log_commit()` writes the preallocated
   `RESULTS.TXT` data and then its file size. The BIOS bridge temporarily returns
   to real mode for sector I/O. Choosing N also performs a directory write to hide
   any older log by setting its size to zero.

Repeated passes use the same inputs. They may expose an emulator bug that appears
after recompilation or state reuse, but do not explore additional numeric values.

## Mode transitions and reference safeguards

The kernel runs at ring 0 in flat 32-bit protected mode. BIOS sector I/O goes
through a 16-bit protected-mode code segment before returning to real mode. That
transition segment is based at `KERNEL_BASE`, so its instruction offsets fit in
16 bits. The bridge also loads a 16-bit stack descriptor before clearing CR0.PE;
otherwise cached stack attributes could make BIOS calls use the wrong stack width.

The code separates hardware behavior from bookkeeping at sensitive points:

- Generated operand-form probes guard ESP/EBX/EBP/ESI/EDI before calling C.
- COMISS/UCOMISS flags are captured with SETO/LAHF before guard comparisons can
  overwrite them, without depending on the guest stack being intact.
- MASKMOVQ data/mask operand order is checked in source and, when available,
  the compiled object.
- SQRT and packed NaN comparison cases separate result and status assertions.
- Segment-limit SIMD probes require independent scalar FS checks to pass first.

The expected-exception and logging paths below explain the remaining safeguards.

## Anatomy of a test

Most cases follow four steps:

```c
tf_begin("instruction and condition");
/* Set control state and load known input bits. */
/* Run a small assembly probe; save its result to ordinary memory. */
tf_check_u32(actual, expected);
```

An **oracle** is the rule that decides what the answer should be. Here it may be
a fixed bit pattern, an integer reference calculation, a floating-point class,
an error tolerance, or an expected exception vector. A trustworthy oracle must
not merely repeat the same instruction being tested.

For example, `SHUFPS.imm8` gives every lane a distinct value. Four two-bit fields
select output lanes: the first two come from A and the last two from B. C computes
those selections, while assembly executes the encoded instruction. The comparison
then checks all 128 result bits.

`tf_begin()` only selects a name; it does not increment counters. A checker
records one outcome. The same probe can have separate cases for result bits,
MXCSR status, and upper-lane preservation. Conversely, some helpers combine
several assertions into one case, so a case count is not an instruction count.

| Outcome | Meaning |
| --- | --- |
| PASS | The assertion implemented by this case matched. |
| FAIL | The implemented assertion did not match. This can be a target defect or an incorrect oracle. |
| EXEC | Execution completed; no deterministic result/ordering assertion was checked. |
| SKIP | The assertion was not evaluated, for example after a prerequisite failed or for unsupported DAZ. |

Zero failures must be interpreted together with EXEC and SKIP counts.

## Reading the bit patterns and assembly

MMX registers contain 64 bits, used as eight bytes, four 16-bit words, or two
32-bit dwords. An SSE1 XMM register contains four 32-bit lanes. `u128.lane[0]`
is the lowest lane and the first one in memory. The diagnostic formatter prints
the highest dword first, so lane 0 appears at the right of a 128-bit hex value.

For binary32 inputs, `0x3f800000` is `1.0`, `0x80000000` is negative zero, and
`0x7f800000` is positive infinity. NaNs have an all-ones exponent and a nonzero
fraction. The quiet/signaling distinction is also encoded in the fraction.
Keeping these as integers preserves their exact bits and avoids using C floating
point to compute the expected answer.

Packed `PS` operations act on four single-precision lanes. Scalar `SS` arithmetic
acts on lane 0 and preserves upper destination lanes. Check each instruction's
rules: for example, the memory-load form of `MOVSS` clears the upper lanes.

There are two assembly syntaxes in this project:

```asm
# Intel syntax in .S: destination, source
addps xmm0, xmm1

# AT&T syntax inside C strings: source, destination
addps %xmm1, %xmm0
```

In GCC extended-assembly strings, register percent signs are doubled (`%%xmm0`)
because `%0`, `%1`, and so on substitute C operands. `"r"` requests a general
register, `"m"` a memory operand, and the `"memory"` clobber tells the compiler
the assembly accesses memory. It is a compiler constraint, not a CPU fence.

`EMMS` marks the shared x87/MMX register file empty; it does not zero its data.
`MXCSR_DEFAULT` masks all SSE floating-point traps, clears sticky status, selects
nearest/even rounding, and disables DAZ/FZ. A trap mask bit of **one** suppresses
the trap; zero enables it. Status can still be set while a trap is masked.

For the authoritative per-instruction rules, use Intel's
[Software Developer's Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html):
Volume 2 describes instructions; Volume 3 describes the system environment.
Use the legacy MMX/SSE forms when reading entries that also describe AVX variants.

## How expected exceptions return

Fault probes deliberately trigger such exceptions as `#GP` (general protection),
`#NM` (device not available), `#UD` (invalid opcode), or `#XM` (SIMD floating point).

1. `fault_arm()` sets the expected vector and clears the previous observation.
2. Inline assembly writes the address of its next local `1:` label to
   `fault_resume_eip`, then executes the potentially faulting instruction.
   `1f` means the next forward occurrence of label `1`.
3. The CPU enters `faults.S`. If the vector matches, the handler records the
   exception and replaces saved EIP with that continuation address.
4. `IRETD` returns after the tested instruction. C disarms the probe, restores
   control state, and compares the observed vector with the expected vector.

If no exception occurs, the observation remains `0xffffffff` and the comparison
fails. A different **handled** vector causes a diagnostic halt, rather than a
recoverable FAIL. Only selected vectors have handlers. The framework is not a
general exception recovery service and does not support nested probes.

Do not replace the assembly continuation with a C label. Optimized C does not
describe this external control transfer, so the compiler may place that label
somewhere unsuitable. The vector checker also does not validate saved EIP or
error code merely because the handler captured them.

## Why some tests are skipped

If MXCSR fails its rounding/status/trap prerequisites, `mxcsr_edges.c` records the
failure and skips 55 dependent checks. The quarantine persists across later
passes; two dependent fault-gating cases also skip. Earlier scalar/packed suites
still run, so this is not a global suppression of all MXCSR-related failures.

Similarly, memory tests prove the FS selector, base, limit, and scalar boundary
behavior before attributing a segment-limit failure to `MOVUPS`. A failed
prerequisite skips those two SIMD segment cases. These skips reduce misleading
secondary failures; they do not establish that the skipped behavior is correct.

## Generated code and verification

Edit the Python generators and regenerate their `.S` output together. Immediate
values are encoded instruction bytes, so a runtime loop cannot directly supply
them to one assembly instruction. The generator emits 256 instruction targets
and a dispatch table instead. Register matrices similarly spell out register
indices to test the actual encoding fields and source/destination aliases.

`make verify` builds the image and runs structural/source regression checks.
It does not execute the CPU tests. Use emulator or hardware runs for behavioral
validation, and preserve the complete result counts and target configuration.

