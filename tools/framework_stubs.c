/* Console is separate from the machine-readable log under test. */
#include "console.h"
void console_putc(char c) { (void)c; }
void console_puts(const char *s) { (void)s; }
void console_printf(const char *format, ...) { (void)format; }
