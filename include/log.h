#ifndef LOG_H
#define LOG_H
#include <stdint.h>

/* prepare/put/printf only touch RAM. commit and discard access the floppy
 * after testing; discard sets file size to zero even on a reused image. */
void log_prepare(uint32_t passes, uint32_t mode);
void log_putc(char c);
void log_puts(const char *s);
void log_printf(const char *fmt, ...);
void log_flush(void);
int log_commit(void);
void log_discard(void);
uint32_t log_size(void);
int log_is_truncated(void);
int log_has_io_error(void);

#endif
