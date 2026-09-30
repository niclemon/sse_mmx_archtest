/* BIOS stand-ins for exercising memory-map decisions and log capture on a
 * host. They never call the BIOS or access a real disk. */
#include <stdint.h>
#include "archtest.h"
#include "disk.h"
#include "log.h"

uint8_t boot_drive, bios_bounce[512], bios_edd_parameters[30];
bios_memory_range bios_memory_ranges[64];
uint32_t bios_memory_count;
uint8_t test_log_ram[8192];
static uint8_t floppy[FAT_TOTAL_SECTORS * 512];
static int map_error, save_error;
static uint32_t disk_writes, hd_writes;

void test_reset(void) {
    bios_memory_count = 0;
    map_error = save_error = 0;
    disk_writes = hd_writes = 0;
    for (unsigned i = 0; i < sizeof(test_log_ram); ++i) test_log_ram[i] = 0xa5;
}
void test_range(uint64_t base, uint64_t length, uint32_t type, uint32_t attr) {
    if (bios_memory_count < 64)
        bios_memory_ranges[bios_memory_count++] = (bios_memory_range){base, length, type, attr};
}
void test_map_error(int e) { map_error = e; }
void test_save_error(int e) { save_error = e; }
uint32_t test_disk_writes(void) { return disk_writes; }
uint32_t test_hd_writes(void) { return hd_writes; }
int bios_get_memory_map(void) { return map_error; }
int bios_edd_command(uint32_t op, uint32_t drive, uint32_t lba, void *buffer) {
    (void)op; (void)drive; (void)lba; (void)buffer; return -1;
}
int bios_rw_sector(uint32_t write, uint32_t drive, uint32_t lba, void *buffer) {
    (void)drive;
    if (lba >= FAT_TOTAL_SECTORS || save_error) return -1;
    uint8_t *a = buffer, *b = floppy + lba * 512;
    if (write) ++disk_writes;
    for (unsigned i = 0; i < 512; ++i) {
        if (write) b[i] = a[i]; else a[i] = b[i];
    }
    return 0;
}
int hd_select(unsigned index) { (void)index; return save_error; }
int hd_save(const void *data, uint32_t size, char name[13]) {
    (void)data; (void)size;
    const char *n = "RES00001.TXT";
    for (unsigned i = 0; i < 13; ++i) name[i] = n[i];
    ++hd_writes;
    return save_error;
}
