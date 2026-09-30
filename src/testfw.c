#include <stdint.h>
#include "archtest.h"
#include "console.h"
#include "log.h"
#include "testfw.h"

/* A framework case is one recorded outcome, not one instruction or input.
 * One probe can record several outcomes (for example, result bits and flags).
 * tf_begin() selects a name; only the reporting/checking functions count it. */
volatile uint32_t g_current_pass = 1;
volatile uint32_t g_configured_passes = 1;
volatile uint32_t g_log_mode = LOG_FAIL_ONLY;
volatile test_counts g_counts;

static const char *current_name = "<none>";
static const char *current_detail = 0;
static uint32_t group_start_total, group_start_fail, group_start_skip;
static const char *group_name;

static int memeq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static void log_prefix(const char *status) {
    log_printf("pass=%08x %s %s", g_current_pass, status, current_name);
    if (current_detail && *current_detail)
        log_printf(" [%s]", current_detail);
    log_puts("\r\n");
}

const char *tf_current_name(void) {
    return current_name ? current_name : "<none>";
}
const char *tf_current_group(void) {
    return group_name ? group_name : "<none>";
}

void tf_reset_counts(void) {
    g_counts.total = g_counts.passed = g_counts.failed = g_counts.exec_only = g_counts.skipped = 0;
}

void tf_group(const char *name) {
    if (group_name) {
        console_printf("  -> %u cases, %u fail, %u skip\n", g_counts.total - group_start_total,
                       g_counts.failed - group_start_fail, g_counts.skipped - group_start_skip);
    }
    group_name = name;
    group_start_total = g_counts.total;
    group_start_fail = g_counts.failed;
    group_start_skip = g_counts.skipped;
    console_printf("\n[%s]\n", name);
    log_printf("\r\n[%s]\r\n", name);
}

void tf_end_group(void) {
    if (!group_name)
        return;
    console_printf("  -> %u cases, %u fail, %u skip\n", g_counts.total - group_start_total,
                   g_counts.failed - group_start_fail, g_counts.skipped - group_start_skip);
    group_name = 0;
}

void tf_begin(const char *name) {
    current_name = name ? name : "<none>";
    current_detail = 0;
}

/* Borrow the string until the next tf_begin(); do not retain a dead stack
 * buffer. The tests normally check/report immediately after setting detail. */
void tf_set_detail(const char *detail) {
    current_detail = detail;
}

void tf_pass(void) {
    ++g_counts.total;
    ++g_counts.passed;
    if (g_log_mode == LOG_ALL)
        log_prefix("PASS");
}

/* EXEC means the instruction returned without an unexpected exception.
 * No data-result or externally visible ordering assertion was checked. */
void tf_exec(void) {
    ++g_counts.total;
    ++g_counts.exec_only;
    if (g_log_mode == LOG_ALL)
        log_prefix("EXEC");
}

void tf_skip(const char *reason) {
    ++g_counts.total;
    ++g_counts.skipped;
    log_prefix("SKIP");
    if (reason)
        log_printf("  reason=%s\r\n", reason);
}

/* Preserve the planned case count when a failed prerequisite suppresses a
 * whole family. One log line represents count unexecuted cases. */
void tf_skip_many(const char *name, const char *reason, uint32_t count) {
    if (!count)
        return;
    current_name = name ? name : "<none>";
    current_detail = 0;
    g_counts.total += count;
    g_counts.skipped += count;
    console_printf("  SKIP %u cases: %s", count, current_name);
    if (reason)
        console_printf(" (%s)", reason);
    console_putc('\n');
    log_printf("pass=%08x SKIPx%u %s\r\n", g_current_pass, count, current_name);
    if (reason)
        log_printf("  reason=%s\r\n", reason);
}

void tf_fail_text(const char *reason) {
    ++g_counts.total;
    ++g_counts.failed;
    console_printf("  FAIL %s", current_name);
    if (current_detail)
        console_printf(" [%s]", current_detail);
    if (reason)
        console_printf(": %s", reason);
    console_putc('\n');
    log_prefix("FAIL");
    if (reason)
        log_printf("  reason=%s\r\n", reason);
}

void tf_fail_guest_reg(const char *reg, uint32_t expected, uint32_t actual) {
    ++g_counts.total;
    ++g_counts.failed;
    console_printf("  FAIL %s guest-%s-corruption expected=%08x actual=%08x\n", current_name,
                   reg ? reg : "GPR", expected, actual);
    log_prefix("FAIL");
    log_printf("  guest-register-corruption %s expected=0x%08x actual=0x%08x\r\n",
               reg ? reg : "GPR", expected, actual);
}

void tf_check_u32(uint32_t actual, uint32_t expected) {
    if (actual == expected) {
        tf_pass();
        return;
    }
    ++g_counts.total;
    ++g_counts.failed;
    console_printf("  FAIL %s", current_name);
    if (current_detail)
        console_printf(" [%s]", current_detail);
    console_printf(" expected=%08x actual=%08x\n", expected, actual);
    log_prefix("FAIL");
    log_printf("  expected=0x%08x actual=0x%08x\r\n", expected, actual);
}

void tf_check_mask_u32(uint32_t actual, uint32_t expected, uint32_t mask) {
    tf_check_u32(actual & mask, expected & mask);
}

/* Compare raw bits, including floating-point NaN payloads and signed zero.
 * Hex diagnostics print the most significant dword first; lane[0] and the
 * lowest-addressed bytes appear at the RIGHT of the printed value. */
void tf_check_u64(const void *actual, const void *expected) {
    const uint32_t *a = (const uint32_t *)actual;
    const uint32_t *e = (const uint32_t *)expected;
    if (memeq((const uint8_t *)a, (const uint8_t *)e, 8)) {
        tf_pass();
        return;
    }
    ++g_counts.total;
    ++g_counts.failed;
    console_printf("  FAIL %s expected=%08x%08x actual=%08x%08x\n", current_name, e[1], e[0], a[1],
                   a[0]);
    log_prefix("FAIL");
    log_printf("  expected=0x%08x%08x actual=0x%08x%08x\r\n", e[1], e[0], a[1], a[0]);
}

void tf_check_u128(const void *actual, const void *expected) {
    const uint32_t *a = (const uint32_t *)actual;
    const uint32_t *e = (const uint32_t *)expected;
    if (memeq((const uint8_t *)a, (const uint8_t *)e, 16)) {
        tf_pass();
        return;
    }
    ++g_counts.total;
    ++g_counts.failed;
    console_printf("  FAIL %s\n", current_name);
    console_printf("    expected=%08x%08x%08x%08x\n", e[3], e[2], e[1], e[0]);
    console_printf("    actual  =%08x%08x%08x%08x\n", a[3], a[2], a[1], a[0]);
    log_prefix("FAIL");
    log_printf("  expected=0x%08x%08x%08x%08x\r\n", e[3], e[2], e[1], e[0]);
    log_printf("  actual=0x%08x%08x%08x%08x\r\n", a[3], a[2], a[1], a[0]);
}

/* A deliberately restricted RCP/RSQRT oracle, not a general float comparator.
 * Callers must use the positive power-of-two reference vectors in baseline.S.
 * Adjacent float encodings have different spacing on either side of a power
 * of two, hence the asymmetric integer tolerances. */
static int approx_lane(uint32_t a, uint32_t e) {
    if (a == e)
        return 1;
    /* Baseline uses positive powers of two. Intel's 1.5*2^-12 bound maps to
       <=6144 encodings below or <=3072 above at these exponent boundaries. */
    if (a < e)
        return (e - a) <= 6144u;
    return (a - e) <= 3072u;
}

void tf_check_approx4(const void *actual, const void *expected) {
    const uint32_t *a = actual, *e = expected;
    for (uint32_t i = 0; i < 4; ++i) {
        if (!approx_lane(a[i], e[i])) {
            tf_check_u128(actual, expected);
            return;
        }
    }
    tf_pass();
}

void tf_check_approx_scalar(const void *actual, const void *expected) {
    const uint32_t *a = actual, *e = expected;
    if (approx_lane(a[0], e[0]) && a[1] == e[1] && a[2] == e[2] && a[3] == e[3])
        tf_pass();
    else
        tf_check_u128(actual, expected);
}

/* Check the exception vector only. The handler records EIP/error code for
 * diagnostics, but this function does not validate either of them. */
void tf_check_fault(uint32_t actual_vector, uint32_t expected_vector) {
    tf_check_u32(actual_vector, expected_vector);
}

void tf_print_summary(void) {
    tf_end_group();
    console_printf("\nSUMMARY pass=%u total=%u pass=%u fail=%u exec=%u skip=%u\n", g_current_pass,
                   g_counts.total, g_counts.passed, g_counts.failed, g_counts.exec_only,
                   g_counts.skipped);
    log_printf("\r\nSUMMARY pass=%08x total=%08x pass=%08x fail=%08x exec=%08x skip=%08x\r\n",
               g_current_pass, g_counts.total, g_counts.passed, g_counts.failed, g_counts.exec_only,
               g_counts.skipped);
}
