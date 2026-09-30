/* Run only user-mode probes against the host CPU. Boot, segmentation, control
 * registers and exception delivery still need the bare-metal image. */
#include <stdarg.h>
#include <stdio.h>
#include "archtest.h"
#include "console.h"
#include "cpu.h"
#include "log.h"
#include "testfw.h"
#include "tests.h"

void console_putc(char c) { putchar(c); }
void console_puts(const char *s) { fputs(s, stdout); }
void console_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
void log_puts(const char *s) { (void)s; }
void log_printf(const char *format, ...) { (void)format; }

/* Deliberately supply no CR0/CR4 or fault-handler stubs. Accidentally adding a
 * privileged suite to this runner should fail to link, not silently pass. */
uint32_t cpu_cpuid1_edx(void) {
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return d;
}
uint32_t cpu_get_mxcsr(void) {
    uint32_t value;
    __asm__ volatile("stmxcsr %0" : "=m"(value));
    return value;
}
void cpu_set_mxcsr(uint32_t value) { __asm__ volatile("ldmxcsr %0" ::"m"(value) : "memory"); }
void cpu_fninit(void) { __asm__ volatile("fninit" ::: "memory"); }
void cpu_emms(void) { __asm__ volatile("emms" ::: "memory"); }
void cpu_fxsave(void *area) { __asm__ volatile("fxsave (%0)" ::"r"(area) : "memory"); }
void cpu_fxrstor(const void *area) { __asm__ volatile("fxrstor (%0)" ::"r"(area) : "memory"); }

int main(void) {
    static uint8_t saved[512] __attribute__((aligned(16)));
    uint32_t required = (1u << 23) | (1u << 24) | (1u << 25);
    if ((cpu_cpuid1_edx() & required) != required) {
        fputs("Hosted probes require MMX, FXSR and SSE.\n", stderr);
        return 77;
    }
    cpu_fxsave(saved);
    cpu_fninit();
    cpu_set_mxcsr(MXCSR_DEFAULT);
    tf_reset_counts();
    /* A second pass catches tests that leave behind control or stack state. */
    for (g_current_pass = 1; g_current_pass <= 2; ++g_current_pass) {
        run_mmx_matrix();
        run_sse_moves();
        run_edge_sse_fp();
        run_edge_sse_packed();
        run_sse_compare_matrix();
        run_sse_numeric_boundaries();
        run_state_payloads();
        tf_end_group();
    }
    cpu_fxrstor(saved);
    printf("HOSTED total=%u pass=%u fail=%u exec=%u skip=%u\n", g_counts.total, g_counts.passed,
           g_counts.failed, g_counts.exec_only, g_counts.skipped);
    return g_counts.failed ? 1 : 0;
}
