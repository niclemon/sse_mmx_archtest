/* Host-only disk adapter. All writes go to disposable image fixtures. */
#include <stdint.h>
#include <stdio.h>
#include "disk.h"

static FILE *images[8];
static uint32_t sectors[8], writes, reads, fail_write, bad_access;

void test_close(void) {
    for (unsigned i = 0; i < 8; ++i) {
        if (images[i]) fclose(images[i]);
        images[i] = 0; sectors[i] = 0;
    }
    writes = reads = fail_write = bad_access = 0;
}
int test_attach(const char *name, unsigned index) {
    if (index >= 8 || images[index]) return -1;
    images[index] = fopen(name, "r+b");
    if (!images[index]) return -1;
    if (fseek(images[index], 0, SEEK_END)) return -1;
    sectors[index] = (uint32_t)(ftell(images[index]) / 512);
    return 0;
}
void test_fail_write(unsigned nth) { fail_write = nth; }
uint32_t test_writes(void) { return writes; }
uint32_t test_bad_access(void) { return bad_access; }
int disk_hd_probe(uint8_t drive, uint32_t *count) {
    unsigned i = (unsigned)drive - 0x80;
    if (i >= 8 || !images[i]) return -1;
    *count = sectors[i]; return 0;
}
static FILE *seek_sector(uint8_t drive, uint32_t lba) {
    unsigned i = (unsigned)drive - 0x80;
    if (i >= 8 || !images[i] || lba >= sectors[i]) { ++bad_access; return 0; }
    if (fseek(images[i], (long)lba * 512, SEEK_SET)) return 0;
    return images[i];
}
int disk_hd_read(uint8_t drive, uint32_t lba, void *data) {
    FILE *f = seek_sector(drive, lba); ++reads;
    return f && fread(data, 1, 512, f) == 512 ? 0 : -1;
}
int disk_hd_write(uint8_t drive, uint32_t lba, const void *data) {
    FILE *f = seek_sector(drive, lba); ++writes;
    if (fail_write && writes >= fail_write) return -1;
    if (!f || fwrite(data, 1, 512, f) != 512) return -1;
    return fflush(f);
}
