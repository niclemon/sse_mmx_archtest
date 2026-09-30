#!/usr/bin/env python3
"""Check image layout and selected source/object regression patterns.

This does not boot the image or prove instruction semantics. Some checks look
for exact source text (including comments); preserve those markers when editing.
Optional --kernel/--source arguments enable the corresponding extra checks.
"""
from pathlib import Path
import argparse, struct, re, hashlib
from elf_calls import verify_linked_calls

SECTOR=512; TOTAL=2880; KERNEL_SECTORS=384; RESERVED=385; SPF=9; ROOT=403; DATA=417

def fat12_get(fat:bytes,c:int)->int:
    off=c+c//2
    w=fat[off]|(fat[off+1]<<8)
    return (w>>4)&0xfff if c&1 else w&0xfff

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('image',type=Path); ap.add_argument('--kernel',type=Path); ap.add_argument('--source',type=Path); ap.add_argument('--elf',type=Path); a=ap.parse_args()
    b=a.image.read_bytes(); errs=[]
    if a.elf:
        try:
            call_count = verify_linked_calls(a.elf)
        except ValueError as error:
            errs.append(str(error))
    if len(b)!=TOTAL*SECTOR: errs.append(f'image size {len(b)} != {TOTAL*SECTOR}')
    if b[510:512]!=b'\x55\xaa': errs.append('bad boot signature')
    if b[54:62]!=b'FAT12   ': errs.append('BPB filesystem type is not FAT12')
    if struct.unpack_from('<H',b,14)[0]!=RESERVED: errs.append('unexpected reserved-sector count')
    f1=b[RESERVED*SECTOR:(RESERVED+SPF)*SECTOR]; f2=b[(RESERVED+SPF)*SECTOR:(RESERVED+2*SPF)*SECTOR]
    if f1!=f2: errs.append('FAT copies differ')
    ent=b[ROOT*SECTOR:ROOT*SECTOR+32]
    if ent[:11]!=b'RESULTS TXT': errs.append('first root entry is not RESULTS.TXT')
    if struct.unpack_from('<H',ent,26)[0]!=2: errs.append('RESULTS.TXT does not start at cluster 2')
    # Walk preallocated chain and ensure it covers the entire data area.
    c=2; seen=[]
    while 2<=c<0xff8 and len(seen)<3000:
        seen.append(c); c=fat12_get(f1,c)
    expected=TOTAL-DATA
    if len(seen)!=expected or c<0xff8: errs.append(f'RESULTS FAT chain length={len(seen)}, expected={expected}, terminator={c:03x}')
    if a.kernel:
        k=a.kernel.read_bytes(); embedded=b[SECTOR:SECTOR+len(k)]
        if embedded!=k: errs.append('embedded kernel does not match kernel.bin')
        if len(k)>KERNEL_SECTORS*SECTOR: errs.append('kernel exceeds reserved sectors')
        # Verify requested CR0 AND immediate 0x9ffffff3 appears in machine code.
        if b'\x25\xf3\xff\xff\x9f' not in k: errs.append('compiled CR0 mask immediate 0x9ffffff3 not found')
    if a.source:
        s=a.source.read_text(errors='replace')
        requested='and eax, ~((1 << 2) | (1 << 3) | (1 << 29) | (1 << 30))'
        if requested not in s: errs.append('requested CR0 source mask not found')
        # Regression checks for the protected->real transition.
        # 1) SS must have D/B=0 before PE is cleared.
        m = re.search(r'pm16_to_rm:.*?mov ax, DATA16_SEL(?P<body>.*?)mov eax, cr0\s*\n\s*and eax, 0xfffffffe', s, re.S)
        if not m or 'mov ss, ax' not in m.group('body'):
            errs.append('pm16_to_rm does not load SS with DATA16_SEL before clearing CR0.PE')
        # 2) CODE16 must be based at KERNEL_BASE and the far jump must use a
        #    <64 KiB offset, not an absolute 0x100xx offset in a 16-bit CS.
        if '.long pm16_to_rm - KERNEL_BASE' not in s:
            errs.append('protected->16-bit far jump does not use KERNEL_BASE-relative offset')
        code16_marker = '16-bit transition code.  Give it base KERNEL_BASE'
        if code16_marker not in s:
            errs.append('CODE16 descriptor is not documented/configured as KERNEL_BASE-based')

        root = a.source.parent.parent
        header = (root / 'include' / 'archtest.h').read_text()
        boot_source = (root / 'src' / 'boot.S').read_text()
        if not re.search(rf'#define\s+KERNEL_SECTORS\s+{KERNEL_SECTORS}u\b', header):
            errs.append('C kernel reservation disagrees with image layout')
        if not re.search(rf'\.equ\s+KERNEL_SECTORS,\s*{KERNEL_SECTORS}\b', boot_source):
            errs.append('boot loader kernel reservation disagrees with image layout')
        log_src = (root / 'src' / 'log.c').read_text(errors='replace')
        baseline_src = (root / 'src' / 'tests' / 'baseline.S').read_text(errors='replace')
        operand_src = (root / 'src' / 'tests' / 'operand_forms.S').read_text(errors='replace')
        operand_gen = (root / 'tools' / 'gen_operand_forms.py').read_text(errors='replace')
        if 'No disk access here.  Logging during tests is RAM-only.' not in log_src:
            errs.append('RAM-only active logging invariant missing')
        if 'log_ram()[size_bytes++]' not in log_src:
            errs.append('log_putc is not writing the active log to RAM')
        if 'push edi' not in baseline_src or 'pop edi' not in baseline_src:
            errs.append('baseline_run_sse does not preserve cdecl callee-saved EDI')
        if 'mov DWORD PTR [of_saved_esp],esp' not in operand_src or 'tf_fail_guest_reg' not in operand_src:
            errs.append('operand-form guest-register guard missing from generated assembly')
        if 'mov esp,DWORD PTR [of_entry_esp]' not in operand_src:
            errs.append('operand-form outer ESP recovery missing')
        if 'Verify that a SIMD probe did not corrupt guest ABI/stack registers.' not in operand_gen:
            errs.append('operand-form guard is not preserved in the generator')
        if 'exp_pavgw:    .quad 0x089119a22ab33bc4' not in baseline_src:
            errs.append('PAVGW reference vector changed unexpectedly')
        mxcsr_src = (root / 'src' / 'tests' / 'mxcsr_edges.c').read_text(errors='replace')
        testfw_src = (root / 'src' / 'testfw.c').read_text(errors='replace')
        if 'static uint32_t mxcsr_quarantined;' not in mxcsr_src:
            errs.append('MXCSR quarantine state is missing')
        if 'guest MXCSR control/status/exception semantics prerequisite failed' not in mxcsr_src:
            errs.append('MXCSR prerequisite/quarantine path is missing')
        if '55u' not in mxcsr_src or 'MXCSR-dependent rounding/flag/#XM/DAZ/FZ checks' not in mxcsr_src:
            errs.append('MXCSR derivative-case quarantine count is missing')
        if 'void tf_skip_many(' not in testfw_src:
            errs.append('compact skip accounting helper is missing')
        fault_gate_src = (root / 'src' / 'tests' / 'fault_gating.c').read_text(errors='replace')
        state_src = (root / 'src' / 'tests' / 'state_edges.c').read_text(errors='replace')
        mem_src = (root / 'src' / 'tests' / 'memory_edges.c').read_text(errors='replace')
        combined_fault_sources = fault_gate_src + mxcsr_src + state_src + mem_src
        # Optimized C &&resume labels can be coalesced to the beginning
        # of run_edge_fault_gating(), making an expected #NM restart the group.
        if '(uintptr_t)&&resume' in combined_fault_sources:
            errs.append('unsafe optimized-C &&resume exception target remains')
        if combined_fault_sources.count('movl $1f, fault_resume_eip') < 17:
            errs.append('assembler-local exception resume labels are missing')
        if 'mxcsr_guest_semantics_quarantined' not in fault_gate_src:
            errs.append('MXCSR-dependent CR4/#XM priority probes are not quarantined')
        mmx_src = (root / 'src' / 'tests' / 'mmx_edges.c').read_text(errors='replace')
        if 'PAVGW.diag memory-source discriminator' not in mmx_src:
            errs.append('PAVGW memory-source discriminator diagnostics missing')
        if '2AC4512A2F7113B0' not in mmx_src or '2B44512A2FF113B0' not in mmx_src or '3AB454A7732D84BE' not in mmx_src:
            errs.append('PAVGW discriminator expected classes missing')

        # Reference-rule regression checks.
        if 'maskmovq %%mm1,%%mm0' not in mmx_src:
            errs.append('MASKMOVQ oracle does not encode Intel data=mm0, mask=mm1 ordering')
        mmx_obj = root / 'build' / 'tests' / 'mmx_edges.c.o'
        if mmx_obj.exists():
            ob = mmx_obj.read_bytes()
            if b'\x0f\xf7\xc1' not in ob:
                errs.append('compiled MMX edge object lacks MASKMOVQ mm0,mm1 encoding 0F F7 C1')
            if b'\x0f\xf7\xc8' in ob:
                errs.append('compiled MMX edge object contains reversed MASKMOVQ mm1,mm0 encoding')
        cf = re.search(r'\.macro CF val(?P<body>.*?)\.endm', operand_src, re.S)
        if not cf or 'seto BYTE PTR [of_saved_of]' not in cf.group('body') or 'lahf' not in cf.group('body'):
            errs.append('COMISS/UCOMISS EFLAGS capture macro missing SETO+LAHF snapshot')
        elif cf.group('body').find('GUARD') < cf.group('body').find('lahf'):
            errs.append('COMISS/UCOMISS flags are clobbered by GUARD before capture')
        if 'seto BYTE PTR [of_saved_of]' not in operand_gen or 'lahf' not in operand_gen:
            errs.append('COMISS/UCOMISS flag snapshot is not preserved in generator')

        sse_fp_src = (root / 'src' / 'tests' / 'sse_fp_edges.c').read_text(errors='replace')
        packed_src = (root / 'src' / 'tests' / 'sse_packed_edges.c').read_text(errors='replace')
        if 'SQRTSS -1 invalid result class' not in sse_fp_src or 'SQRTSS -1 invalid MXCSR.IE' not in sse_fp_src or 'SQRTSS -1 preserves upper lanes' not in sse_fp_src:
            errs.append('SQRTSS oracle is not split into result/status/upper-lane checks')
        if 'SQRTPS negative/-0/+inf/exact lane results' not in packed_src or 'SQRTPS negative lane sets MXCSR.IE' not in packed_src:
            errs.append('SQRTPS oracle is not split into lane-result and status checks')
        if 'imm=0x00 EQ' not in packed_src or 'imm=0x07 ORD' not in packed_src or 'CMPPS QNaN predicate result' not in packed_src:
            errs.append('CMPPS NaN predicate diagnostics are missing imm8 labels/result checks')
        if 'FS oracle LSL effective limit' not in mem_src or 'FS scalar dword crosses limit +13 -> #GP' not in mem_src:
            errs.append('FS segment-limit prerequisite oracle is missing')
        if 'MOVUPS segment-limit exact-fit/cross checks' not in mem_src or 'FS selector/base/limit scalar prerequisite failed' not in mem_src:
            errs.append('MOVUPS segment-limit checks are not quarantined behind scalar FS oracle')
    if errs:
        print('verification FAILED')
        for e in errs: print('  -',e)
        raise SystemExit(1)
    print('verification OK')
    if a.elf: print(f'  linked calls: {call_count} direct calls/jumps land at symbol entry points')
    print(f'  image: {len(b)} bytes; sha256={sha(a.image)}')
    if a.kernel: print(f'  kernel: {a.kernel.stat().st_size} bytes; embedded copy matches')
    print(f'  FAT12: reserved={RESERVED}, root={ROOT}, data={DATA}, RESULTS capacity={(TOTAL-DATA)*SECTOR} bytes')
    print(f'  RESULTS chain: {len(seen)} clusters, first=2 last={seen[-1]}')
    print('  CR0 mask: EM/TS/NW/CD clear mask 0x9ffffff3 present')
    if a.source:
        print('  BIOS thunk: CODE16 base=0x10000, small IP target, SS D/B=0 before real mode')
        print('  active logging: extended-RAM only during tests; disk commit deferred until Y')
        print('  ABI: baseline SSE preserves EDI across C call boundary')
        print('  diagnostics: every generated operand probe guards ESP/EBX/EBP/ESI/EDI before C calls')
        print('  MXCSR quarantine: prerequisite failure suppresses 55 derivative checks on later execution')
        print('  fault recovery: assembler-local resume EIPs; no optimized-C &&resume targets')
        print('  PAVGW diagnostics: distinct correct/PAVGB/word-byte-swapped result classes')
        print('  reference rules: COMISS flags snapshot precedes guard; MASKMOVQ operand order audited')
        print('  FP diagnostics: SQRT result/status split; CMPPS NaN predicates labeled by imm8')
        print('  segment oracle: FS selector/LSL/base/scalar limit prerequisite before MOVUPS')

if __name__=='__main__': main()
