#include <stdint.h>
#include "input.h"
#include "console.h"
#include "io.h"

/* Poll the PC keyboard controller; protected-mode execution keeps hardware
 * interrupts disabled. This assumes BIOS-configured translated set-1 scan
 * codes and recognizes only the keys needed by the three runner prompts. */
static uint8_t read_scancode(void) {
    for (;;) {
        if (inb(0x64) & 1u)
            return inb(0x60);
    }
}

static char translate(uint8_t sc) {
    static const char top_digits[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    if (sc >= 0x02 && sc <= 0x0b)
        return top_digits[sc - 0x02];
    switch (sc) {
    case 0x1c:
        return '\n';
    case 0x0e:
        return '\b';
    case 0x1e:
        return 'A';
    case 0x21:
        return 'F';
    case 0x15:
        return 'Y';
    case 0x31:
        return 'N';
    /* numeric keypad */
    case 0x52:
        return '0';
    case 0x4f:
        return '1';
    case 0x50:
        return '2';
    case 0x51:
        return '3';
    case 0x4b:
        return '4';
    case 0x4c:
        return '5';
    case 0x4d:
        return '6';
    case 0x47:
        return '7';
    case 0x48:
        return '8';
    case 0x49:
        return '9';
    default:
        return 0;
    }
}

/* Ignore key releases (bit 7) and E0-prefixed extended keys. Letters are
 * returned uppercase regardless of Shift/Caps Lock; no full keyboard layout
 * or modifier-state handling is needed for A/F/Y/N input. */
static char get_key(void) {
    int extended = 0;
    for (;;) {
        uint8_t sc = read_scancode();
        if (sc == 0xe0) {
            extended = 1;
            continue;
        }
        if (sc & 0x80) {
            extended = 0;
            continue;
        }
        if (extended) {
            extended = 0;
            continue;
        }
        char c = translate(sc);
        if (c)
            return c;
    }
}

uint32_t input_read_decimal(uint32_t min_value, uint32_t max_value) {
    for (;;) {
        uint32_t value = 0, digits = 0;
        for (;;) {
            char c = get_key();
            if (c == '\n') {
                if (digits && value >= min_value && value <= max_value) {
                    console_putc('\n');
                    return value;
                }
                console_printf("\nPlease enter %u..%u: ", min_value, max_value);
                value = digits = 0;
                continue;
            }
            if (c == '\b') {
                if (digits) {
                    value /= 10u;
                    --digits;
                    console_putc('\b');
                }
                continue;
            }
            if (c >= '0' && c <= '9') {
                uint32_t d = (uint32_t)(c - '0');
                if (value > (max_value - d) / 10u)
                    continue;
                value = value * 10u + d;
                ++digits;
                console_putc(c);
            }
        }
    }
}

static int in_choices(char c, const char *choices) {
    while (*choices) {
        char x = *choices++;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (c == x)
            return 1;
    }
    return 0;
}

char input_read_choice(const char *choices) {
    for (;;) {
        char c = get_key();
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (in_choices(c, choices)) {
            console_putc(c);
            console_putc('\n');
            return c;
        }
    }
}
