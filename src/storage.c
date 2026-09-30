#include <stdint.h>
#include "disk.h"
#include "storage.h"
#include "ff.h"
#include "diskio.h"

static hd_volume volumes[HD_MAX_VOLUMES], mounted;
static unsigned volume_count;
static int selected, writing, write_error;
static FATFS filesystem;
static const char *last_error = "no error";

static uint32_t get16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }
static int range_ok(uint32_t start, uint32_t count, uint32_t size) {
    return count && start < size && count <= size - start;
}
static int fat_type(uint8_t t) { return t == 4 || t == 6 || t == 0x0e || t == 0x0b || t == 0x0c; }
static int extended_type(uint8_t t) { return t == 5 || t == 0x0f || t == 0x85; }

/* Validate the BPB ourselves before letting FatFs interpret it. In particular,
 * a partition must not direct a filesystem write into a neighboring volume.
 * FAT32 with mirroring disabled is deliberately unsupported by this adapter. */
static int inspect(uint8_t drive, uint32_t base, uint32_t bound, unsigned part, hd_volume *v) {
    uint8_t b[512];
    if (disk_hd_read(drive, base, b) || get16(b + 510) != 0xaa55 || get16(b + 11) != 512) return 0;
    uint32_t spc = b[13], reserved = get16(b + 14), fats = b[16], roots = get16(b + 17);
    uint32_t total = get16(b + 19), spf = get16(b + 22);
    if (!total) total = get32(b + 32);
    if (!spf) spf = get32(b + 36);
    if (!spc || spc > 128 || (spc & (spc - 1)) || !reserved ||
        (fats != 1 && fats != 2) || !spf || spf >= 0x200000 || !total || total > bound) return 0;
    uint32_t metadata = reserved + fats * spf + (roots * 32 + 511) / 512;
    if (total <= metadata || base > 0xffffffffu - total) return 0;
    uint32_t clusters = (total - metadata) / spc;
    unsigned bits = clusters < 65525 ? 16 : 32;
    if (clusters < 4085 || clusters > 0x0ffffff3u || (uint64_t)(clusters + 2) * (bits / 8) > (uint64_t)spf * 512) return 0;
    if (bits == 16) {
        if (!get16(b + 22) || !roots || roots % 16) return 0;
    } else {
        if (get16(b + 22) || roots || get16(b + 42) || (get16(b + 40) & 0x80) ||
            get32(b + 44) < 2 || get32(b + 44) >= clusters + 2) return 0;
    }
    v->drive = drive; v->lba = base; v->sectors = total;
    v->fat_bits = (uint8_t)bits; v->partition = (uint16_t)part;
    unsigned label = bits == 16 ? 43 : 71;
    for (unsigned i = 0; i < 11; ++i) v->label[i] = b[label + i] >= 32 && b[label + i] < 127 ? (char)b[label + i] : '?';
    v->label[11] = 0;
    /* Refuse a volume marked dirty or with a previous disk error. */
    if (disk_hd_read(drive, base + reserved, b)) return 0;
    uint32_t clean = bits == 16 ? get16(b + 2) & 0xc000 : get32(b + 4) & 0x0c000000;
    return clean == (bits == 16 ? 0xc000u : 0x0c000000u);
}

static int candidate(uint8_t drive, uint32_t base, uint32_t size, unsigned part) {
    for (unsigned i = 0; i < volume_count; ++i) {
        const hd_volume *v = &volumes[i];
        if (v->drive != drive) continue;
        if ((uint64_t)base < (uint64_t)v->lba + v->sectors &&
            (uint64_t)v->lba < (uint64_t)base + size) return -1;
    }
    if (volume_count < HD_MAX_VOLUMES && inspect(drive, base, size, part, &volumes[volume_count])) ++volume_count;
    return 0;
}

static void extended(uint8_t drive, uint32_t base, uint32_t size) {
    uint32_t offset = 0, seen[128], seen_count = 0;
    uint32_t starts[128], lengths[128], ranges = 0;
    unsigned first = volume_count;
    uint8_t b[512];
    for (unsigned part = 5; part < 133; ++part) {
        for (unsigned i = 0; i < seen_count; ++i) if (seen[i] == offset) goto invalid;
        seen[seen_count++] = offset;
        if (offset >= size || disk_hd_read(drive, base + offset, b) || get16(b + 510) != 0xaa55) goto invalid;
        for (unsigned i = 0; i < ranges; ++i)
            if (offset >= starts[i] && offset - starts[i] < lengths[i]) goto invalid;
        const uint8_t *p = b + 446;
        uint32_t rel = get32(p + 8), count = get32(p + 12);
        if (p[4]) {
            if (!rel || rel > size - offset || !range_ok(offset + rel, count, size)) goto invalid;
            for (unsigned i = 0; i < seen_count; ++i)
                if (seen[i] >= offset + rel && seen[i] - (offset + rel) < count) goto invalid;
            for (unsigned i = 0; i < ranges; ++i)
                if ((uint64_t)offset + rel < (uint64_t)starts[i] + lengths[i] &&
                    (uint64_t)starts[i] < (uint64_t)offset + rel + count) goto invalid;
            starts[ranges] = offset + rel;
            lengths[ranges++] = count;
            if (fat_type(p[4]) && candidate(drive, base + offset + rel, count, part) < 0) goto invalid;
        }
        p += 16;
        if (!p[4]) return;
        if (!extended_type(p[4])) goto invalid;
        offset = get32(p + 8);
        if (!offset || !range_ok(offset, get32(p + 12), size)) goto invalid;
    }
invalid:
    /* Do not retain an earlier candidate from a corrupt extended chain: its
     * advertised data area may overlap a later volume or an EBR sector. */
    volume_count = first;
}

unsigned hd_scan(void) {
    volume_count = 0; selected = writing = write_error = 0;
    f_mount(0, "", 0);
    for (unsigned d = 0x80; d < 0x88 && volume_count < HD_MAX_VOLUMES; ++d) {
        uint32_t sectors;
        uint8_t mbr[512];
        if (disk_hd_probe((uint8_t)d, &sectors) || disk_hd_read((uint8_t)d, 0, mbr)) continue;
        unsigned before = volume_count;
        candidate((uint8_t)d, 0, sectors, 0); /* FAT directly on disk, without MBR. */
        if (before != volume_count || get16(mbr + 510) != 0xaa55) continue;
        int valid = 1;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *p = mbr + 446 + i * 16;
            if (!p[4]) continue;
            uint32_t start = get32(p + 8), count = get32(p + 12);
            if (p[4] == 0xee || !start || !range_ok(start, count, sectors)) valid = 0;
            for (unsigned j = 0; j < i; ++j) {
                const uint8_t *q = mbr + 446 + j * 16;
                if (q[4] && (uint64_t)start < (uint64_t)get32(q + 8) + get32(q + 12) &&
                    (uint64_t)get32(q + 8) < (uint64_t)start + count) valid = 0;
            }
        }
        if (!valid) continue;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *p = mbr + 446 + i * 16;
            if (fat_type(p[4])) candidate((uint8_t)d, get32(p + 8), get32(p + 12), i + 1);
            else if (extended_type(p[4])) extended((uint8_t)d, get32(p + 8), get32(p + 12));
        }
    }
    return volume_count;
}

const hd_volume *hd_get_volume(unsigned index) { return index < volume_count ? &volumes[index] : 0; }

int hd_select(unsigned index) {
    f_mount(0, "", 0);
    selected = writing = write_error = 0;
    if (index >= volume_count) { last_error = "invalid volume selection"; return -1; }
    mounted = volumes[index];
    hd_volume now;
    if (!inspect(mounted.drive, mounted.lba, mounted.sectors, mounted.partition, &now) ||
        now.sectors != mounted.sectors || now.fat_bits != mounted.fat_bits) {
        last_error = "volume is unavailable or changed"; return -1;
    }
    selected = 1;
    if (f_mount(&filesystem, "", 1) != FR_OK ||
        (filesystem.fs_type != FS_FAT16 && filesystem.fs_type != FS_FAT32)) {
        selected = 0; last_error = "cannot mount FAT16/FAT32 volume"; return -1;
    }
    return 0;
}

/* FatFs sees the chosen partition as a whole device starting at sector zero.
 * These bounds apply to every read and write, including metadata operations. */
DSTATUS disk_status(BYTE drive) { return drive || !selected ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE drive) { return disk_status(drive); }
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count) {
    if (disk_status(drive) || !range_ok(sector, count, mounted.sectors)) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (disk_hd_read(mounted.drive, mounted.lba + sector + i, data + i * 512)) return RES_ERROR;
        /* The free-space scan reads the whole FAT before creating a file.
         * Reject divergent mirrors instead of choosing one and allocating
         * clusters that the other copy might still assign to an old file. */
        if (!writing && filesystem.fs_type && filesystem.n_fats == 2 &&
            sector + i >= filesystem.fatbase && sector + i - filesystem.fatbase < filesystem.fsize) {
            uint8_t mirror[512];
            if (disk_hd_read(mounted.drive, mounted.lba + sector + i + filesystem.fsize, mirror)) return RES_ERROR;
            for (unsigned j = 0; j < 512; ++j)
                if (mirror[j] != data[i * 512 + j]) return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count) {
    if (!writing || write_error || disk_status(drive) || !range_ok(sector, count, mounted.sectors)) return RES_WRPRT;
    for (UINT i = 0; i < count; ++i) {
        if (disk_hd_write(mounted.drive, mounted.lba + sector + i, data + i * 512)) {
            write_error = 1;
            return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data) {
    (void)data;
    if (disk_status(drive)) return RES_NOTRDY;
    /* BIOS calls are synchronous. Keep any write error sticky, including a
     * secondary FAT/FSInfo error that upstream FatFs might otherwise ignore. */
    return command == CTRL_SYNC && !write_error ? RES_OK : RES_ERROR;
}

int hd_save(const void *data, uint32_t size, char filename[13]) {
    FIL file;
    FATFS *fs;
    DWORD free_clusters;
    filename[0] = 0;
    if (!selected) { last_error = "no hard-drive volume selected"; return -1; }
    write_error = 0;
    filesystem.free_clst = 0xffffffffu;
    /* FSInfo hints are disabled: count the actual free FAT entries. Leave one
     * extra cluster for a FAT32 root-directory extension if it is needed. */
    if (f_getfree("", &free_clusters, &fs) != FR_OK) {
        last_error = "could not read free space"; return -1;
    }
    uint32_t cluster_bytes = (uint32_t)fs->csize * 512;
    uint32_t needed = size / cluster_bytes + (size % cluster_bytes != 0);
    if (free_clusters < needed + (fs->fs_type == FS_FAT32)) {
        last_error = "not enough free space for the complete log"; return -1;
    }
    writing = 1;
    FRESULT rc = FR_EXIST;
    for (unsigned attempt = 0; attempt <= 99999 && rc == FR_EXIST; ++attempt) {
        const char *initial = "RESULTS.TXT";
        for (unsigned i = 0; i <= 11; ++i) filename[i] = initial[i];
        if (attempt) {
            const char *numbered = "RES00000.TXT";
            for (unsigned i = 0; i <= 12; ++i) filename[i] = numbered[i];
            unsigned value = attempt;
            for (int i = 7; i >= 3; --i) { filename[i] = (char)('0' + value % 10); value /= 10; }
        }
        rc = f_open(&file, filename, FA_WRITE | FA_CREATE_NEW);
    }
    if (rc != FR_OK) {
        writing = 0; filename[0] = 0;
        last_error = "could not create a new log file (full directory or disk error)"; return -1;
    }
    UINT done = 0;
    rc = f_write(&file, data, size, &done);
    FRESULT closed = f_close(&file);
    writing = 0;
    if (rc != FR_OK || closed != FR_OK || done != size || write_error) {
        last_error = "write failed; the newly created file may be incomplete"; return -1;
    }
    last_error = "no error";
    return 0;
}

const char *hd_error(void) { return last_error; }

uint32_t hd_capture_capacity(uint32_t ram_limit) {
    DWORD free_clusters;
    FATFS *fs;
    if (!selected) { last_error = "no hard-drive volume selected"; return 0; }
    filesystem.free_clst = 0xffffffffu;
    if (f_getfree("", &free_clusters, &fs) != FR_OK) {
        last_error = "cannot read free space or FAT copies disagree"; return 0;
    }
    if (fs->fs_type == FS_FAT32 && free_clusters) --free_clusters;
    uint64_t bytes = (uint64_t)free_clusters * fs->csize * 512;
    if (!bytes) last_error = "no free space for a log";
    return bytes < ram_limit ? (uint32_t)bytes : ram_limit;
}
