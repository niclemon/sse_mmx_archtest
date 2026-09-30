#include <stdint.h>
#include "disk.h"
#include "archtest.h"

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

extern int bios_edd_command(uint32_t operation, uint32_t drive, uint32_t lba, void *buffer);
extern uint8_t bios_edd_parameters[30];

static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int disk_hd_probe(uint8_t drive, uint32_t *sectors) {
    for (unsigned i = 0; i < 30; ++i) bios_edd_parameters[i] = 0;
    bios_edd_parameters[0] = 30;
    if (bios_edd_command(0, drive, 0, bios_bounce)) return -1;
    if (bios_edd_parameters[0] < 26 || bios_edd_parameters[1] ||
        bios_edd_parameters[24] != 0 || bios_edd_parameters[25] != 2) return -1;
    *sectors = le32(bios_edd_parameters + 20) ? 0xffffffffu : le32(bios_edd_parameters + 16);
    return *sectors ? 0 : -1;
}

int disk_hd_read(uint8_t drive, uint32_t lba, void *buffer) {
    for (unsigned i = 0; i < 3; ++i) {
        if (!bios_edd_command(1, drive, lba, bios_bounce)) {
            copy512(buffer, bios_bounce);
            return 0;
        }
    }
    return -1;
}

int disk_hd_write(uint8_t drive, uint32_t lba, const void *buffer) {
    for (unsigned i = 0; i < 3; ++i) {
        copy512(bios_bounce, buffer);
        if (!bios_edd_command(2, drive, lba, bios_bounce)) return 0;
    }
    return -1;
}

uint32_t disk_log_memory_capacity(uint32_t limit) {
    uint64_t start = LOG_RAM_BASE, end = start;
    if (bios_get_memory_map() || bios_memory_count > 64) return 0;
    /* Accept one backed range, then trim it against every reserved overlap.
     * Do not bridge holes or assume that the BIOS lists ranges in order. */
    for (uint32_t i = 0; i < bios_memory_count; ++i) {
        const bios_memory_range *r = &bios_memory_ranges[i];
        uint64_t top = r->base + r->length;
        if (r->attributes != 1 || r->type != 1 || top < r->base) continue;
        if (r->base <= start && top > end) end = top;
    }
    if (end > start + limit) end = start + limit;
    for (uint32_t i = 0; i < bios_memory_count; ++i) {
        const bios_memory_range *r = &bios_memory_ranges[i];
        uint64_t top = r->base + r->length;
        if (!(r->attributes & 1) || !r->length) continue;
        if (r->type == 1 && r->attributes == 1) continue;
        if (top < r->base) return 0;
        if (r->base < end && top > start) end = r->base > start ? r->base : start;
    }
    return end > start ? (uint32_t)(end - start) & ~511u : 0;
}
