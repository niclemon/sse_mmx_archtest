#include <stdint.h>
#include "archtest.h"
#include "cpu.h"
#include "faults.h"
#include "testfw.h"
#include "tests.h"

/*
 * IMPORTANT: do not use &&resume as an exception return target in optimized C.
 * GCC is free to merge/move such labels because it cannot see the asynchronous
 * transfer from our ISR. A misplaced continuation can restart the test group
 * with CR0.TS still set, repeatedly faulting until the machine resets.
 *
 * Each probe below writes the address of an assembler-local label into
 * fault_resume_eip immediately before the faulting instruction.  That gives
 * the ISR an exact architectural continuation address which the compiler
 * cannot coalesce with another C basic block.
 */

/* TS (task switched) asks the OS to restore the task's floating-point state;
 * executing SSE/MMX with TS set must raise #NM before doing the operation.
 * Save and restore each control register so a probe cannot poison the next. */
static uint32_t with_ts_sse(void) {
    uint32_t old = cpu_get_cr0();
    cpu_set_cr0(old | (1u << 3));
    fault_arm(X86_VEC_NM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "xorps %%xmm0,%%xmm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr0(old);
    return v;
}

static uint32_t with_ts_mmx(void) {
    uint32_t old = cpu_get_cr0();
    cpu_set_cr0(old | (1u << 3));
    fault_arm(X86_VEC_NM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "pxor %%mm0,%%mm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr0(old);
    cpu_emms();
    return v;
}

/* EM disables these hardware instructions: expect #UD. Clear TS here to
 * isolate EM rather than accidentally testing two gates at once. */
static uint32_t with_em_sse(void) {
    uint32_t old = cpu_get_cr0();
    cpu_set_cr0((old | (1u << 2)) & ~(1u << 3));
    fault_arm(X86_VEC_UD, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "xorps %%xmm0,%%xmm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr0(old);
    return v;
}

static uint32_t with_em_mmx(void) {
    uint32_t old = cpu_get_cr0();
    cpu_set_cr0((old | (1u << 2)) & ~(1u << 3));
    fault_arm(X86_VEC_UD, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "pxor %%mm0,%%mm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr0(old);
    cpu_emms();
    return v;
}

/* OSFXSR announces OS support for SSE state management. With it cleared,
 * this XMM instruction is unavailable even though CPUID advertises SSE. */
static uint32_t without_osfxsr(void) {
    uint32_t old = cpu_get_cr4();
    cpu_set_cr4(old & ~(1u << 9));
    fault_arm(X86_VEC_UD, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "xorps %%xmm0,%%xmm0\n\t"
                     "1:\n\t" ::
                         : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr4(old);
    return v;
}

/* OSXMMEXCPT controls delivery of unmasked SIMD exceptions. sqrt(-1) with
 * invalid unmasked should become #UD when OS support for #XM is disabled. */
static uint32_t without_osxmmexcpt(void) {
    uint32_t old4 = cpu_get_cr4();
    uint32_t x = 0xbf800000u;
    cpu_set_mxcsr(MXCSR_DEFAULT & ~MXCSR_IM);
    cpu_set_cr4(old4 & ~(1u << 10));
    fault_arm(X86_VEC_UD, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "sqrtss (%0),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&x)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr4(old4);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}

/* Both a state gate and an arithmetic exception are possible. TS must win:
 * the instruction should raise #NM before evaluating the negative operand. */
static uint32_t ts_priority_over_xm(void) {
    uint32_t old0 = cpu_get_cr0();
    uint32_t x = 0xbf800000u;
    cpu_set_mxcsr(MXCSR_DEFAULT & ~MXCSR_IM);
    cpu_set_cr0(old0 | (1u << 3));
    fault_arm(X86_VEC_NM, 0);
    __asm__ volatile("movl $1f, fault_resume_eip\n\t"
                     "sqrtss (%0),%%xmm0\n\t"
                     "1:\n\t" ::"r"(&x)
                     : "memory");
    uint32_t v = fault_disarm();
    cpu_set_cr0(old0);
    cpu_set_mxcsr(MXCSR_DEFAULT);
    return v;
}

/* Exposed by mxcsr_edges.c. */
extern int mxcsr_guest_semantics_quarantined(void);

void run_edge_fault_gating(void) {
    tf_group("CR0/CR4 SSE-MMX availability and exception-priority gates");
    tf_begin("CR0.TS + SSE -> #NM");
    tf_check_fault(with_ts_sse(), X86_VEC_NM);
    tf_begin("CR0.TS + MMX -> #NM");
    tf_check_fault(with_ts_mmx(), X86_VEC_NM);
    tf_begin("CR0.EM + SSE -> #UD");
    tf_check_fault(with_em_sse(), X86_VEC_UD);
    tf_begin("CR0.EM + MMX -> #UD");
    tf_check_fault(with_em_mmx(), X86_VEC_UD);
    tf_begin("CR4.OSFXSR=0 + SSE -> #UD");
    tf_check_fault(without_osfxsr(), X86_VEC_UD);

    /* These two need a functional guest MXCSR exception mask.  If the
       prerequisite is already known broken, testing them cannot distinguish
       CR4/priority behavior from the MXCSR defect. */
    if (mxcsr_guest_semantics_quarantined()) {
        tf_skip_many("CR4.OSXMMEXCPT=0 + unmasked FP exception -> #UD",
                     "guest MXCSR exception mask is quarantined", 1u);
        tf_skip_many("CR0.TS priority over unmasked #XM -> #NM",
                     "guest MXCSR exception mask is quarantined", 1u);
    } else {
        tf_begin("CR4.OSXMMEXCPT=0 + unmasked FP exception -> #UD");
        tf_check_fault(without_osxmmexcpt(), X86_VEC_UD);
        tf_begin("CR0.TS priority over unmasked #XM -> #NM");
        tf_check_fault(ts_priority_over_xm(), X86_VEC_NM);
    }
}
