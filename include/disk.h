#ifndef DISK_H
#define DISK_H
#include <stdint.h>

extern uint8_t boot_drive;
int bios_rw_sector(uint32_t write, uint32_t drive, uint32_t lba, void *buffer);
int disk_read_sector(uint32_t lba, void *buffer);
int disk_write_sector(uint32_t lba, const void *buffer);

#endif
