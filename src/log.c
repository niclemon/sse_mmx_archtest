#include <stdint.h>
#include <stdarg.h>
#include "archtest.h"
#include "disk.h"
#include "log.h"

/*
 * The harness performs no BIOS/disk I/O while the architectural tests
 * are running.  Repeated protected<->real mode transitions were polluting the
 * very CPU state we are trying to validate and made failures hard to localize.
 *
 * The complete RESULTS.TXT stream is captured in extended RAM instead and is
 * copied to the preallocated FAT12 cluster chain only after the user answers
 * Y at the end of the run.
 *
 * Pentium-III 86Box configurations have far more than 3 MiB of RAM.  Keeping
 * this scratch area out of the linked .bss keeps it separate from the kernel's
 * low-memory allocation. BSS itself has no bytes in kernel.bin.
 */
#define LOG_CAPACITY RESULTS_MAX_BYTES

static uint8_t root_buf[512] __attribute__((aligned(16)));
static uint8_t commit_sector[512] __attribute__((aligned(16)));
static uint32_t size_bytes;
static uint32_t io_error;
static uint32_t truncated;

static volatile uint8_t *log_ram(void) {
    return (volatile uint8_t *)(uintptr_t)LOG_RAM_BASE;
}

static void zero512(uint8_t *p) {
    for (uint32_t i = 0; i < 512; ++i)
        p[i] = 0;
}

/* Patch the little-endian 32-bit file size at byte 28 of the first FAT12
 * directory entry. This logger assumes our image builder's fixed layout;
 * it is not a filesystem driver for arbitrary FAT12 disks. */
static int set_root_size(uint32_t size) {
    if (disk_read_sector(FAT_ROOT_LBA, root_buf))
        return -1;
    /* RESULTS TXT is deliberately the first root entry. */
    root_buf[28] = (uint8_t)size;
    root_buf[29] = (uint8_t)(size >> 8);
    root_buf[30] = (uint8_t)(size >> 16);
    root_buf[31] = (uint8_t)(size >> 24);
    return disk_write_sector(FAT_ROOT_LBA, root_buf) ? -1 : 0;
}

void log_prepare(uint32_t passes, uint32_t mode) {
    size_bytes = io_error = truncated = 0;
    /* No disk access here.  Logging during tests is RAM-only. */
    log_puts(ARCHTEST_NAME "\r\n");
    log_printf("configured-passes=%u log-mode=%s\r\n", passes,
               mode == LOG_ALL ? "ALL-results" : "FAIL-only");
}

void log_putc(char c) {
    if (truncated)
        return;
    if (size_bytes >= LOG_CAPACITY) {
        truncated = 1;
        return;
    }
    log_ram()[size_bytes++] = (uint8_t)c;
}

void log_puts(const char *s) {
    while (*s)
        log_putc(*s++);
}

static void log_hex(uint32_t v, uint32_t digits) {
    static const char h[] = "0123456789ABCDEF";
    for (int shift = (int)(digits * 4u) - 4; shift >= 0; shift -= 4)
        log_putc(h[(v >> shift) & 0xfu]);
}
static void log_uint(uint32_t v) {
    char b[11];
    uint32_t n = 0;
    if (!v) {
        log_putc('0');
        return;
    }
    while (v) {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    }
    while (n)
        log_putc(b[--n]);
}

static void log_vprintf(const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            log_putc(*fmt++);
            continue;
        }
        ++fmt;
        uint32_t width = 0;
        if (*fmt == '0')
            ++fmt;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (uint32_t)(*fmt++ - '0');
        }
        char s = *fmt ? *fmt++ : 0;
        if (s == '%')
            log_putc('%');
        else if (s == 'c')
            log_putc((char)va_arg(ap, int));
        else if (s == 's') {
            const char *p = va_arg(ap, const char *);
            log_puts(p ? p : "(null)");
        } else if (s == 'u')
            log_uint(va_arg(ap, uint32_t));
        else if (s == 'x' || s == 'X')
            log_hex(va_arg(ap, uint32_t), width ? width : 8u);
        else {
            log_putc('%');
            if (s)
                log_putc(s);
        }
    }
}

void log_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(fmt, ap);
    va_end(ap);
}

/* Kept for API compatibility.  There is intentionally nothing to flush until
 * log_commit(), because the active log lives entirely in RAM. */
void log_flush(void) {
}

int log_commit(void) {
    io_error = 0;

    /* Keep the file logically empty while sectors are being written. */
    if (set_root_size(0)) {
        io_error = 1;
        return -1;
    }

    uint32_t sectors = (size_bytes + 511u) / 512u;
    volatile uint8_t *src = log_ram();

    /* The builder already linked every data cluster contiguously, so no FAT
     * allocation/update is needed. Pad the final sector, write data, then
     * publish the actual byte count. A failed write leaves size zero. */
    for (uint32_t s = 0; s < sectors; ++s) {
        uint32_t base = s * 512u;
        uint32_t remain = size_bytes - base;
        uint32_t n = remain < 512u ? remain : 512u;
        zero512(commit_sector);
        for (uint32_t i = 0; i < n; ++i)
            commit_sector[i] = src[base + i];
        if (disk_write_sector(FAT_DATA_LBA + s, commit_sector)) {
            io_error = 1;
            return -1;
        }
    }

    if (set_root_size(size_bytes)) {
        io_error = 1;
        return -1;
    }
    return 0;
}

void log_discard(void) {
    /* The release image starts with size=0.  On a reused writable image this
     * also removes visibility of an older RESULTS.TXT.  This is the only disk
     * access on the N path, and it occurs after all architectural tests. */
    if (set_root_size(0))
        io_error = 1;
}
uint32_t log_size(void) {
    return size_bytes;
}
int log_is_truncated(void) {
    return (int)truncated;
}
int log_has_io_error(void) {
    return (int)io_error;
}
