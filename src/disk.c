#include <stdint.h>
#include "disk.h"

/* BIOS real-mode disk services cannot use arbitrary protected-mode buffers.
 * entry.S supplies a low-memory bounce buffer and the mode-switching thunk.
 * Copy one sector per call, retrying up to three times. Only use these
 * routines after the CPU test window, as part of log commit/discard. */
extern uint8_t bios_bounce[512];

static void copy512(void *dst, const void *src) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < 512; ++i)
        d[i] = s[i];
}

int disk_read_sector(uint32_t lba, void *buffer) {
    int rc = 1;
    for (uint32_t tries = 0; tries < 3; ++tries) {
        rc = bios_rw_sector(0, boot_drive, lba, bios_bounce);
        if (!rc) {
            copy512(buffer, bios_bounce);
            return 0;
        }
    }
    return rc;
}

int disk_write_sector(uint32_t lba, const void *buffer) {
    copy512(bios_bounce, buffer);
    int rc = 1;
    for (uint32_t tries = 0; tries < 3; ++tries) {
        rc = bios_rw_sector(1, boot_drive, lba, bios_bounce);
        if (!rc)
            return 0;
    }
    return rc;
}
