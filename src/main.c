#include <stdint.h>
#include "archtest.h"
#include "console.h"
#include "cpu.h"
#include "input.h"
#include "log.h"
#include "testfw.h"
#include "tests.h"
#include "io.h"
#include "disk.h"
#include "storage.h"

/* Probe before resetting CPU state for the first test. BIOS disk calls use
 * interrupts and real mode; they must stay outside the architectural probes. */
static int choose_log_target(void) {
    uint32_t ram = disk_log_memory_capacity(64u * 1024u * 1024u);
    if (ram < 4096) {
        console_puts("Cannot find a usable log buffer in the BIOS memory map.\n");
        for (;;) { cpu_cli(); cpu_halt(); }
    }
    console_puts("\nLooking for FAT16/FAT32 hard-drive volumes...\n");
    unsigned count = hd_scan();
    for (;;) {
        console_puts("Save destination:\n"
                     "  [1] Floppy - about 2 clean ALL-results passes; failures use more space\n");
        if (count) console_puts("  [2] Hard drive\n");
        else console_puts("  No supported FAT16/FAT32 hard-drive volume found.\n");
        char choice = input_read_choice(count ? "12" : "1");
        if (choice == '1') {
            log_configure(ram, 0, 0);
            return 0;
        }
        for (unsigned i = 0; i < count; ++i) {
            const hd_volume *v = hd_get_volume(i);
            console_printf("  %u: hard disk %u, partition %u, FAT%u, %u MiB [%s]\n",
                           i + 1, v->drive - 0x7fu, v->partition, v->fat_bits,
                           v->sectors / 2048u, v->label);
        }
        console_puts("Choose volume number: ");
        unsigned volume = input_read_decimal(1, count) - 1;
        if (hd_select(volume)) {
            console_printf("Cannot use that volume: %s\n", hd_error());
            continue;
        }
        console_puts("Checking free space and FAT copies...\n");
        uint32_t available = hd_capture_capacity(ram);
        if (!available) {
            console_printf("Cannot use that volume: %s\n", hd_error());
            continue;
        }
        log_configure(available, 1, volume);
        console_puts("Existing files will be kept; name collisions get RES00001.TXT, etc.\n");
        return 1;
    }
}

/* Establish the same control state at both ends of each pass. This does not
 * clear every MM/XMM register; each probe must load the operands it needs.
 * Ordinary C is built without x87/MMX/SSE so it cannot disturb probe state. */
static void reset_arch_state(void) {
    uint32_t cr0 = cpu_get_cr0();
    /* EM=emulation, TS=task switched, NW=not write-through, CD=cache disable.
     * Clear these gates and set MP (monitor coprocessor). */
    cr0 &= ~((1u << 2) | (1u << 3) | (1u << 29) | (1u << 30));
    cr0 |= (1u << 1);
    cpu_set_cr0(cr0);

    uint32_t cr4 = cpu_get_cr4();
    cr4 |= (1u << 9) | (1u << 10); /* OSFXSR | OSXMMEXCPT */
    cpu_set_cr4(cr4);

    cpu_fninit();
    cpu_emms();
    cpu_set_mxcsr(MXCSR_DEFAULT);
}

/* Start with representative instruction forms, then exercise boundaries.
 * MXCSR must run before fault_gating: it decides whether the latter can
 * meaningfully check unmasked floating-point exceptions. */
static void run_one_pass(void) {
    reset_arch_state();

    run_baseline_suite();
    tf_group("operand forms: memory-source/register-form cross-checks");
    run_operand_form_suite();
    run_edge_mmx();
    run_mmx_matrix();
    run_sse_moves();
    run_edge_sse_fp();
    run_edge_sse_packed();
    run_sse_compare_matrix();
    run_sse_numeric_boundaries();
    run_edge_mxcsr();
    run_edge_memory();
    run_memory_faults();
    run_edge_immediates();
    run_edge_registers();
    run_edge_state();
    run_state_payloads();
    run_edge_fault_gating();
    tf_end_group();

    reset_arch_state();
}

void kernel_main(void) {
    console_init();
    console_puts("\n" ARCHTEST_NAME "\n");
    console_puts("Hybrid freestanding C harness + exact assembly opcode probes\nRAM-buffered "
                 "logging; disk writes only after tests finish\n\n");

    uint32_t edx = cpu_cpuid1_edx();
    uint32_t has_mmx = (edx >> 23) & 1u;
    uint32_t has_fxsr = (edx >> 24) & 1u;
    uint32_t has_sse = (edx >> 25) & 1u;
    console_printf("CPUID.1 EDX: MMX=%u FXSR=%u SSE=%u\n", has_mmx, has_fxsr, has_sse);
    if (!(has_mmx && has_fxsr && has_sse)) {
        console_puts("This image requires a Pentium III-class CPU exposing MMX+FXSR+SSE.\n");
        for (;;) {
            cpu_cli();
            cpu_halt();
        }
    }

    console_printf("\nNumber of complete architectural passes (decimal, 1-%u): ", MAX_PASSES);
    g_configured_passes = input_read_decimal(1, MAX_PASSES);

    console_puts("Log detail: [F] FAIL/SKIP only, [A] ALL PASS/FAIL/EXEC/SKIP: ");
    char choice = input_read_choice("FfAa");
    g_log_mode = (choice == 'A' || choice == 'a') ? LOG_ALL : LOG_FAIL_ONLY;

    int hard_drive = choose_log_target();
    console_printf("Log capture capacity: %u bytes (512 bytes reserved for final totals).\n", log_capacity());
    if (g_log_mode == LOG_ALL && g_configured_passes > (log_capacity() - 512u) / 572000u)
        console_puts("WARNING: the requested ALL-results log is likely to exceed capture capacity.\n");

    log_prepare(g_configured_passes, g_log_mode);

    /* Counts accumulate across passes. Repetition uses the same vectors; it
     * can expose emulator state/tiering bugs but adds no new input values. */
    tf_reset_counts();
    for (g_current_pass = 1; g_current_pass <= g_configured_passes; ++g_current_pass) {
        console_printf("\n=== ARCHITECTURAL PASS %u / %u ===\n", g_current_pass,
                       g_configured_passes);
        log_printf("\r\n=== ARCHITECTURAL PASS %08x / %08x ===\r\n", g_current_pass,
                   g_configured_passes);
        run_one_pass();
    }
    /* Keep the last valid pass number in summaries instead of passes+1. */
    g_current_pass = g_configured_passes;
    log_begin_summary();
    tf_print_summary();

    console_printf("\nAggregate: total=%u pass=%u fail=%u exec=%u skip=%u\n", g_counts.total,
                   g_counts.passed, g_counts.failed, g_counts.exec_only, g_counts.skipped);
    log_printf(
        "AGGREGATE configured-passes=%08x total=%08x pass=%08x fail=%08x exec=%08x skip=%08x\r\n",
        g_configured_passes, g_counts.total, g_counts.passed, g_counts.failed, g_counts.exec_only,
        g_counts.skipped);

    /* Zero failures says nothing about skipped or execution-only cases.
     * Read those counts alongside this message when judging coverage. */
    if (g_counts.failed == 0)
        console_puts("SELF-CHECK PASSED: no semantic mismatches in the enabled coverage matrix.\n");
    else
        console_puts("SELF-CHECK FAILED: one or more architectural checks mismatched.\n");

    if (log_is_truncated())
        console_puts("WARNING: log capture filled. Some records were dropped; final totals were kept.\n");

    console_printf("\nSave captured log on the %s? [Y/N]: ", hard_drive ? "selected hard drive" : "floppy");
    choice = input_read_choice("YyNn");
    if (choice == 'Y' || choice == 'y') {
        if (log_commit() == 0)
            console_printf("Saved %s (%u bytes).\n", log_filename(), log_size());
        else if (hard_drive)
            console_printf("Could not save log: %s\n", hd_error());
        else
            console_puts("Could not commit RESULTS.TXT. Check that the floppy is writable.\n");
    } else {
        log_discard();
        console_puts(hard_drive ? "Log discarded; hard drive unchanged.\n" :
                                 "Log discarded (RESULTS.TXT file size left at zero).\n");
    }

    console_puts("\nDone. Power off or reset the machine.\n");
    for (;;) {
        cpu_cli();
        cpu_halt();
    }
}
