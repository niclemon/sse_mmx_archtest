#include <stdint.h>
#include <stdarg.h>
#include "console.h"
#include "io.h"

/* Direct VGA text memory plus polled COM1 output, without BIOS calls.
 * Serial output times out if the UART is not ready so a missing serial
 * device does not prevent the architectural tests from progressing. */
#define VGA_COLS 80
#define VGA_ROWS 25
#define COM1 0x3f8

static volatile uint16_t *const vga = (volatile uint16_t *)0x000b8000;
static uint32_t row, col;
static int serial_ok;

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01); /* 115200 baud divisor = 1 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
    serial_ok = 1;
}

static void serial_putc(char c) {
    if (!serial_ok)
        return;
    for (uint32_t spin = 0; spin < 100000u; ++spin) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (uint8_t)c);
            return;
        }
    }
}

static void scroll(void) {
    if (row < VGA_ROWS)
        return;
    for (uint32_t y = 1; y < VGA_ROWS; ++y)
        for (uint32_t x = 0; x < VGA_COLS; ++x)
            vga[(y - 1) * VGA_COLS + x] = vga[y * VGA_COLS + x];
    for (uint32_t x = 0; x < VGA_COLS; ++x)
        vga[(VGA_ROWS - 1) * VGA_COLS + x] = (uint16_t)(0x0700 | ' ');
    row = VGA_ROWS - 1;
}

void console_init(void) {
    row = 0;
    col = 0;
    serial_init();
    for (uint32_t i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        vga[i] = (uint16_t)(0x0700 | ' ');
}

void console_putc(char c) {
    if (c == '\r') {
        col = 0;
        serial_putc(c);
        return;
    }
    if (c == '\n') {
        row++;
        col = 0;
        scroll();
        serial_putc('\r');
        serial_putc('\n');
        return;
    }
    if (c == '\b') {
        if (col) {
            --col;
            vga[row * VGA_COLS + col] = (uint16_t)(0x0700 | ' ');
        }
        serial_putc(c);
        return;
    }
    vga[row * VGA_COLS + col] = (uint16_t)(0x0700 | (uint8_t)c);
    if (++col >= VGA_COLS) {
        col = 0;
        row++;
        scroll();
    }
    serial_putc(c);
}

void console_puts(const char *s) {
    while (*s)
        console_putc(*s++);
}

static void put_hex(uint32_t v, uint32_t digits) {
    static const char h[] = "0123456789ABCDEF";
    for (int shift = (int)(digits * 4u) - 4; shift >= 0; shift -= 4)
        console_putc(h[(v >> shift) & 0xfu]);
}

void console_hex32(uint32_t v) {
    put_hex(v, 8);
}

static void put_uint(uint32_t v) {
    char buf[11];
    uint32_t n = 0;
    if (!v) {
        console_putc('0');
        return;
    }
    while (v && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n)
        console_putc(buf[--n]);
}

/* Small freestanding formatter, not libc printf: supports %, c, s, u, x/X
 * and p. Width controls hexadecimal digits only; no floating-point formats. */
void console_vprintf(const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            console_putc(*fmt++);
            continue;
        }
        ++fmt;
        uint32_t zero = 0, width = 0;
        if (*fmt == '0') {
            zero = 1;
            ++fmt;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (uint32_t)(*fmt - '0');
            ++fmt;
        }
        char spec = *fmt ? *fmt++ : 0;
        if (spec == '%')
            console_putc('%');
        else if (spec == 'c')
            console_putc((char)va_arg(ap, int));
        else if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            console_puts(s ? s : "(null)");
        } else if (spec == 'u')
            put_uint(va_arg(ap, uint32_t));
        else if (spec == 'x' || spec == 'X') {
            uint32_t v = va_arg(ap, uint32_t);
            uint32_t d = width ? width : 8u;
            (void)zero;
            put_hex(v, d > 8u ? 8u : d);
        } else if (spec == 'p') {
            console_puts("0x");
            put_hex((uint32_t)(uintptr_t)va_arg(ap, void *), 8);
        } else {
            console_putc('%');
            if (spec)
                console_putc(spec);
        }
    }
}

void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    console_vprintf(fmt, ap);
    va_end(ap);
}
