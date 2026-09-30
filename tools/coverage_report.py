#!/usr/bin/env python3
"""Human-readable MMX/SSE1 Architectural Test coverage inventory.
The score is a project-defined matrix, not an Intel certification metric.
This is a manually maintained inventory: it does not inspect the source,
compare against an ISA catalogue, or read execution logs.
"""
# (category, maximum weight, assigned score, planned framework cases/pass).
# A zero case count avoids double-counting checks already in other rows.
# Cases include PASS/FAIL/EXEC/SKIP; the score is not a measured pass percentage.
rows = [
    ("Mnemonic/form baseline", 15, 15, 146),
    ("Memory-source operand forms", 4, 4, 103),
    ("Register fields + aliasing", 8, 8, 256),
    ("All relevant imm8 values", 8, 8, 3072),
    ("MMX integer boundaries", 10, 9, 163),
    ("SSE FP classes/specials", 12, 11, 94),
    ("MXCSR rounding/flags/masks", 12, 11, 60),
    ("Fault gating/priority", 8, 7, 7),
    ("Alignment/segment memory edges", 8, 7, 118),
    ("FXSAVE/FXRSTOR state", 7, 6, 50),
    ("NT stores/prefetch/fence", 5, 3, 0),
    ("Repeated-pass/dynarec transitions", 3, 3, 0),
]
score=sum(r[2] for r in rows); maxscore=sum(r[1] for r in rows); checks=sum(r[3] for r in rows)
print("MMX/SSE1 Architectural Test coverage inventory")
print("(project-defined architectural category score; not an Intel certification metric)\n")
for name,w,s,n in rows:
    print(f"{name:34} {s:2}/{w:2}  direct checks/pass: {n if n else '-'}")
print(f"\nscore: {score}/{maxscore} = {100*score/maxscore:.1f}%")
print(f"framework cases per pass: {checks}")
print("Note: baseline already contains MOVNTQ/MOVNTPS/PREFETCH*/SFENCE checks; their")
print("architectural observability is intentionally scored conservatively.")
