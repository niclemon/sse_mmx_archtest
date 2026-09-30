#include <stdint.h>
#include <stdarg.h>
#include "archtest.h"
#include "disk.h"
#include "log.h"
#include "storage.h"

/*
 * The harness performs no BIOS/disk I/O while the architectural tests
 * are running.  Repeated protected<->real mode transitions were polluting the
 * very CPU state we are trying to validate and made failures hard to localize.
 *
 * The complete RESULTS.TXT stream is captured in extended RAM instead and is
 * saved to the selected floppy or hard-drive volume after the user answers Y.
 *
 * The BIOS memory map bounds the scratch area above 1 MiB. Keeping it outside
 * .bss avoids clearing a large buffer on startup and leaves the kernel and
 * BIOS stacks below it. The first unused byte is never touched by capture.
 */
#define SUMMARY_RESERVE 512u

static uint8_t root_buf[512] __attribute__((aligned(16)));
static uint8_t commit_sector[512] __attribute__((aligned(16)));
static uint32_t size_bytes;
static uint32_t io_error;
static uint32_t truncated;
static uint32_t capacity = RESULTS_MAX_BYTES;
static int on_hard_drive, finishing;
static unsigned hard_volume;
static char saved_name[13] = "RESULTS.TXT";

void log_configure(uint32_t ram_capacity, int hard_drive, unsigned volume) {
    capacity = ram_capacity;
    on_hard_drive = hard_drive;
    hard_volume = volume;
    if (!hard_drive && capacity > RESULTS_MAX_BYTES) capacity = RESULTS_MAX_BYTES;
}

uint32_t log_capacity(void) { return capacity; }
const char *log_filename(void) { return saved_name; }

static volatile uint8_t *log_ram(void) {
#ifdef ARCHTEST_HOSTED_LOG
    extern uint8_t test_log_ram[];
    return test_log_ram;
#else
    return (volatile uint8_t *)(uintptr_t)LOG_RAM_BASE;
#endif
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
    finishing = 0;
    /* No disk access here.  Logging during tests is RAM-only. */
    log_puts(ARCHTEST_NAME "\r\n");
    log_puts("FORMAT result-tsv=2 escaping=backslash numeric-order=msb-first bytes-order=address\r\n");
    log_printf("configured-passes=%u log-mode=%s\r\n", passes,
               mode == LOG_ALL ? "ALL-results" : "FAIL-only");
}

void log_putc(char c) {
    if (truncated && !finishing)
        return;
    uint32_t limit = finishing ? capacity : (capacity > SUMMARY_RESERVE ? capacity - SUMMARY_RESERVE : 0);
    if (size_bytes >= limit) {
        truncated = 1;
        return;
    }
    log_ram()[size_bytes++] = (uint8_t)c;
}

void log_begin_summary(void) {
    finishing = 1;
    if (truncated)
        log_puts("\r\n[TRUNCATED: record capture filled; final counters follow]\r\n");
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

    if (on_hard_drive) {
        if (hd_select(hard_volume) || hd_save((const void *)log_ram(), size_bytes, saved_name)) {
            io_error = 1;
            return -1;
        }
        return 0;
    }

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
    if (on_hard_drive) return; /* No file was created, so there is nothing to delete. */
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
