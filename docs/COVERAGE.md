# Coverage and limitations

This suite targets original MMX and Pentium III-era SSE1. It now plans **10,698
framework outcomes per pass**, up from 4,069. An outcome is an assertion, an
execution-only record, or a skip; it is not a unique instruction or a measure of
ISA completeness. Five baseline PREFETCH/SFENCE cases remain execution-only.

```sh
python3 tools/coverage_report.py
python3 tools/coverage_report.py --check-log RESULTS.TXT
make verify-hosted
```

The inventory lists each owning source file. Counts are maintained with the
suites, rather than inferred from C syntax. The hosted runner checks its actual
count against that inventory: **6,311 cases per pass**, run twice. The optional
log check validates the full guest count, including skipped prerequisites. It
needs an untruncated log containing the final AGGREGATE record.

The old 92/100 category score has been removed. It was manually assigned and did
not measure conformance, instruction coverage, or the numeric input space.

## What was added

| Area | Assertions now covered |
| --- | --- |
| MMX integer matrix | 44 operations, each with 24 boundary/seeded input pairs in register, memory and actual same-register forms. Integer references cover wrapping and saturating arithmetic, signed/unsigned comparisons and min/max, multiply halves, multiply-add overflow, averages, absolute differences, packing, unpacking and bitwise operations. |
| MMX byte masks | All 256 PMOVMSKB sign patterns and MASKMOVQ write masks. Low mask bits vary independently. Guard bytes catch stores before, after, or outside the selected bytes. |
| SSE moves and logical operations | Register and self forms of MOVAPS, MOVUPS, MOVSS, MOVHLPS, MOVLHPS, UNPCKLPS/HPS and ANDPS/ANDNPS/ORPS/XORPS. Checks include source preservation, raw NaN bits, and unchanged clear/set MXCSR status. All 16 MOVMSKPS masks are checked. |
| Memory transfer widths | Guarded MOVUPS/MOVAPS/MOVSS/MOVLPS/MOVHPS/MOVNTPS and MMX MOVD/MOVQ/MOVNTQ stores. Legal unaligned forms span offsets 0..15. MOVSS memory loads must clear the upper 96 bits; MOVD loads must clear the upper 32 bits. |
| Comparisons | All eight CMPSS/CMPPS predicates, 16 operand pairs, and register/memory forms. Covers both NaN positions, QNaN/SNaN combinations, signed zero, infinities, subnormals, scalar upper lanes, and packed lane masks. COMISS/UCOMISS check EFLAGS and exact MXCSR separately. |
| Conversion boundaries | CVTSS2SI/CVTTSS2SI cover ties, both signs, valid INT_MIN, the largest convertible positive float, overflow, NaN and infinity. CVTSI2SS/CVTPI2PS use an integer-only rounding reference around 2^24 and signed integer limits. All four rounding modes and register/memory forms are checked. |
| Tiny and approximate results | Exact normal/subnormal transitions reject spurious underflow/precision flags. RCPPS/RSQRTPS use an integer relative-error bound for references 1/3, 1/5, 1/7 and 1/10, in every rounding mode. |
| MXCSR | Validate the raw mask's reserved bits and the effective mask's baseline bits, while retaining the valid zero-mask fallback. Each reserved high bit is tested independently through LDMXCSR and FXRSTOR. LDMXCSR must preserve the old state on #GP. |
| Faults | Misaligned MOVAPS loads/stores, ADDPS and MOVNTPS check vector, zero error code, exact saved EIP, unchanged XMM state and unchanged guarded memory. These use assembler-local fault/resume labels. |
| Saved state | All eight MMX payloads round-trip through FXSAVE/FXRSTOR. x87 checks cover ten-byte payloads, nondefault control, rotated TOP, full/partial tags and logical-versus-physical register ordering. EMMS tag clearing, save-area guards and software-owned bytes 464..511 are checked. |
| Existing FP checks | Invalid arithmetic now requires quiet NaNs. Result, upper-lane and exact-status checks are separated so an extra exception flag cannot hide behind a correct result. |

The integer matrix repeats its seed each pass. It expands the tested input set
but is not exhaustive enumeration of all 64-bit operand pairs. The existing
3,072 immediate cases and 256 register-pair cases remain in place.

## Reading matrix failures

`case=0x...` identifies a deterministic input row. MMX names include `reg`, `mem`
or `self`. Comparison indices use `(predicate << 8) | (row << 1) | memory`;
conversion indices use `(rounding_mode << 8) | (row << 1) | memory`. Memory is
zero for a register source and one for a memory source. Other sweeps use a
register number, byte mask, bit number or offset, as described next to the loop.

`tf_check_bytes()` reports the first mismatching byte's offset and values.
It checks the entire guarded buffer, not just the intended payload.

## What is still missing

| Gap | Why the current tests do not establish it |
| --- | --- |
| Paging and memory permissions | Paging is disabled and there is no #PF handler. Cross-page accesses, permission faults, read-width overfetch and guard-page behavior need new harness support. |
| Complete privilege/descriptor matrix | Tests run at ring 0. Ring-3 alignment checks, more segment types/limits, and every CR0/CR4 interaction are not covered. |
| Every encoding and register pair | The full register-pair sweeps still cover four representative operations. New self forms broaden alias coverage, but not all register combinations. Undefined/reserved encodings and later SSE generations remain outside scope. |
| Exhaustive floating-point behavior | These are selected values, not a complete binary32 reference implementation. More finite rounding boundaries, NaN payload combinations, DAZ/FZ matrices, unmasked destination preservation, and simultaneous exception priorities remain useful additions. |
| Complete saved-state format | Saved instruction/data pointers, x87 opcode fields and every x87 status/tag classification are not validated. Reserved FXSAVE bytes are deliberately not compared as defined data. |
| Fault recovery for all mismatches | New context checks concentrate on #GP. Existing #NM/#UD/#XM tests mostly check the vector. A different handled vector still stops the run rather than recovering as a normal failed assertion. |
| Observable cache and ordering behavior | PREFETCH/cache timing and multiprocessor store-order observations are not tested. A completed SFENCE or correct local store value does not prove those properties. |
| Hardware/emulator diversity | Hosted checks exercise the current host and mode. They do not replace boot runs on a Pentium III-class CPU or validate an emulator's protected-mode exception handling. |

For the architectural rules, see Intel's [instruction reference, Volume 2A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf)
(CMP, conversions and saved state) and [Volume 1, section 11.6.6](https://cdrdv2-public.intel.com/671436/253665-sdm-vol-1.pdf)
(MXCSR mask handling). The reference comments identify the assumptions that
matter to each probe.
