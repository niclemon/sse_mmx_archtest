#ifndef DISK_H
#define DISK_H
#include <stdint.h>

extern uint8_t boot_drive;
int bios_rw_sector(uint32_t write, uint32_t drive, uint32_t lba, void *buffer);
int disk_read_sector(uint32_t lba, void *buffer);
int disk_write_sector(uint32_t lba, const void *buffer);

/* Hard disks use BIOS packet I/O, independently of the floppy boot drive.
 * Only 512-byte sectors and the first 2 TiB are exposed to the FAT layer. */
int disk_hd_probe(uint8_t drive, uint32_t *sectors);
int disk_hd_read(uint8_t drive, uint32_t lba, void *buffer);
int disk_hd_write(uint8_t drive, uint32_t lba, const void *buffer);

typedef struct {
    uint64_t base, length;
    uint32_t type, attributes;
} bios_memory_range;
extern bios_memory_range bios_memory_ranges[64];
extern uint32_t bios_memory_count;
int bios_get_memory_map(void);
/* Return contiguous usable bytes at LOG_RAM_BASE, capped by limit. */
uint32_t disk_log_memory_capacity(uint32_t limit);

#endif
