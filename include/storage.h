#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>

#define HD_MAX_VOLUMES 16u
typedef struct {
    uint32_t lba, sectors;
    uint16_t partition;
    uint8_t drive, fat_bits;
    char label[12];
} hd_volume;

/* Discovery and selection read only. Saving creates a new short-name file
 * in the selected root directory; existing files are never truncated. */
unsigned hd_scan(void);
const hd_volume *hd_get_volume(unsigned index);
int hd_select(unsigned index);
uint32_t hd_capture_capacity(uint32_t ram_limit);
int hd_save(const void *data, uint32_t size, char filename[13]);
const char *hd_error(void);

#endif
