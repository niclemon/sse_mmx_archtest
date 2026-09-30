# Coverage and limitations

The [MMX/SSE1 Architectural Test](../README.md) targets original MMX and
Pentium III-era SSE1 in a single-CPU, BIOS-booted environment. It is a regression
suite, not a complete conformance test or a test of later SSE generations.

## Inventory

The manually maintained inventory plans **4,069 framework cases per pass**,
including five execution-only PREFETCH/SFENCE cases. Cases count recorded
outcomes, not unique instructions. Prerequisite failures or unsupported features
can produce SKIP outcomes; see [the reading guide](READING_THE_CODE.md#why-some-tests-are-skipped)
for the dependency rules.

Generate the category inventory directly from its maintained source:

```sh
python3 tools/coverage_report.py
```

The tool also prints a manually assigned **92/100 category score**. This is not
a measured percentage of the ISA, numeric input space, or conformance
requirements. The remaining eight points do not measure the missing behavior.
The script does not discover tests or read execution results.

## Covered areas

- Representative original MMX/SSE1 instruction and operand forms.
- All 256 immediate bytes for twelve selected forms, and all register pairs for
  four representative operations.
- MMX saturation, packing, averaging, shift, and overflow boundaries.
- Scalar/packed floating-point specials, upper-lane preservation, comparison
  flags, and all eight legacy packed comparison predicates.
- MXCSR rounding, status, trap masks, and optional DAZ/FZ behavior.
- Alignment faults, legal unaligned transfers, and an FS segment boundary.
- XMM/MXCSR save/restore, selected x87 metadata, and CR0/CR4 exception gates.
- Repeated passes to expose state reuse or emulator recompilation defects.

The table below qualifies these categories; a category does not imply exhaustive
coverage of every instruction, operand, or architectural field in it.

## Major gaps

- Paging, page-crossing faults, and page permission combinations.
- Multiprocessor observation of store ordering.
- Deterministic cache/PREFETCH timing or effect measurements.
- Exhaustive privilege-level and descriptor permutations.
- Undefined/reserved encodings and exhaustive numeric input enumeration.

## Concrete coverage review

The source supports calling this a broad regression suite for original MMX and
SSE1. It does not support calling it complete, even within that target. These
observations come from source inspection, not a new emulator or hardware run.

| Area | What the current code checks | Limit or useful next addition |
| --- | --- | --- |
| Inventory | `tools/coverage_report.py` prints literal scores and counts. | It does not discover tests, compare an ISA catalogue, or consume run results. Build an instruction/form-to-assertion inventory before claiming exhaustive coverage. |
| Immediate fields | `immediates.c` checks 256 values for SHUFPS, PSHUFW, PEXTRW, PINSRW, and eight MMX shifts: 3,072 cases. | Each form uses one fixed source pattern. CMPPS/CMPSS use eight defined legacy predicates separately. This is not every immediate-bearing instruction with every input. |
| Register fields | `register_edges.c` checks all 64 register pairs for XORPS, ADDPS, PXOR, and PADDB. | The other operations do not have their own full register matrices. |
| Move forms | The baseline checks MOVAPS/MOVUPS/MOVSS memory loads and stores. | Add dedicated register-to-register cases, particularly MOVSS upper-lane preservation with distinct source/destination sentinels. Arithmetic lane tests do not test that move form. |
| Floating point | Fixed exact results, special classes, rounding ties, and selected status/trap cases. | Add broader finite/subnormal boundaries, both NaN operand positions and payloads where specified, and a scalar CMPSS QNaN/SNaN matrix comparable to the packed one. RCP/RSQRT finite accuracy is checked only on selected power-of-two references. |
| MXCSR mask | `get_mxcsr_mask()` replaces a raw zero mask with `0x0000ffbf`. | The subsequent nonzero check always succeeds after that fallback. It does not validate supported/reserved bits despite its `nonzero/architectural` name. Preserve the valid zero-mask fallback but add substantive assertions against the raw/effective fields. |
| Saved state | `state_edges.c` checks all eight XMM registers, MXCSR, default x87 control word, empty tag byte, and misalignment faults. | It does not round-trip all x87/MMX payloads, nonempty tags, status, or saved instruction/data pointers. The group label's word “complete” should not be read as full FXSAVE-format coverage. |
| Faults | Alignment, one FS segment boundary, selected CR0/CR4 gates, and six SIMD exception classes. | `tf_check_fault()` checks the vector only. Add error-code, saved-EIP, destination-preservation, and faulting-store side-effect assertions where specified. A different handled vector currently halts the run. |
| Store boundaries | Store tests compare the bytes expected at the destination. | Add canaries around the destination to detect unintended adjacent writes, including on fault paths. |
| Reference strength | Most cases compare exact bits; some use class/status helpers. | `check_scalar_nan()` and `cmp_lane_class()` accept any NaN and require selected flags without rejecting extra flags. Their combined failures are less diagnostic than the separated SQRT/compare checks. |

The immediate and register matrices account for 3,328 of the planned 4,069 cases.
Large case counts therefore mainly reflect exhaustive encoding fields on a
limited set of operations, rather than an equally deep check of every operation.

The most useful next steps are to strengthen the MXCSR mask assertion, cover the
missing move and saved-state cases, and map each intended instruction form to
explicit result/status/fault checks. Paging, additional privilege combinations,
and externally observed ordering require further harness support. Validation on
known hardware and multiple emulators would help distinguish oracle mistakes
from implementation defects; no such new results are claimed by this review.
