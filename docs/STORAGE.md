# Saving results on a hard drive

The program still boots from its 1.44 MB floppy. An attached hard disk supplies
optional storage for the log; it does not need to contain a bootable OS.

## Choosing a destination

After selecting the pass count and log detail, choose:

1. **Floppy.** The existing preallocated `RESULTS.TXT` has 1,261,056 bytes of
   space. A full pass with the detailed ALL log exceeds that capacity; use
   FAIL/SKIP logging here or select a hard drive for all comparison values.
2. **Hard drive.** Select one of the detected FAT16/FAT32 volumes. Each entry
   identifies its BIOS disk number, partition number, filesystem, size and label.
   Partition 0 means FAT directly on the disk, without a partition table.

The menu shows the actual capture capacity. Both destinations keep the log
in RAM during tests. The hard-drive option uses up to 64 MiB, limited by the
contiguous usable region reported by BIOS E820 starting at physical 1 MiB.
Available space on the selected volume can lower the capture limit further.
Reserved ranges and holes shorten the RAM region. An unusable or incomplete memory
map stops the run before capture can write into unverified RAM. With a full
64 MiB buffer, approximately 29 ALL-results passes fit at the measured size
of about 2.3 MB per pass. This varies with outcomes and skipped families; the
menu uses a conservative 4 MB/pass estimate for its capacity warning. Free
disk space must also accommodate the resulting file. Disk space does not
remove the RAM capture limit: active tests still perform no disk I/O.

At the end, **Y** saves the captured log. A hard-drive save creates `RESULTS.TXT`
when that name is available, otherwise `RES00001.TXT`, `RES00002.TXT`, and so on.
It does not truncate or replace an existing file. **N** leaves the hard drive
unchanged. The floppy retains its original behavior: N clears the existing
floppy log's directory size, including on a reused image.

Capture reserves 512 bytes for final totals. If the record area fills, tests
continue and the file ends with an explicit `[TRUNCATED: ...]` marker plus the
summary and aggregate counters. `coverage_report.py --check-log` rejects such
a file as an incomplete record capture, even though its final counters survive.

## What disks are supported

- BIOS disks `80h` through `87h` with INT 13h Extensions (EDD packet I/O).
- 512-byte logical sectors and addresses below the 2 TiB boundary.
- FAT16/FAT32 in MBR primary partitions, logical partitions in extended
  containers, or an unpartitioned FAT volume.
- Up to 16 detected volumes. Extended partition walks are bounded to 128
  records and stop on cycles.
- FAT32 with FAT mirroring enabled, or a single FAT. Volumes marked dirty or
  reporting a previous disk error are excluded. Mismatched FAT copies cause
  the save preflight to fail before creating a file.

GPT, exFAT, NTFS, 4 KiB sectors and FAT32 active-FAT-only mode are unsupported.
The program does not format or repartition disks. The BIOS must expose the
controller and disk; a disk that requires an OS-only driver is unavailable.
IDE and SCSI controller drivers are not loaded by this program.

An existing FAT volume needs free clusters and room for a root-directory entry.
FatFs follows fragmented allocation chains and grows FAT32 root directories when
necessary. A full FAT16 root directory cannot grow. File timestamps are fixed
at 2026-01-01 because the runner has no clock service.

## Implementation and failure handling

`entry.S` provides EDD discovery/read/write calls and E820 memory-map collection
through the existing real-mode bridge. Disk and memory discovery happen before
the first pass resets CPU control state. No disk calls occur inside the tests.

`storage.c` validates partition and BPB bounds, then presents only the selected
volume to the vendored FatFs library. Saving revalidates and remounts the volume,
counts actual free clusters, creates a new short-name file, writes its contents,
and closes it. A latched disk error prevents a later successful call from hiding
an earlier failure. Disk-full detection normally occurs before file creation.

FAT is not journaled. A failed write or interrupted save may leave the newly
created file incomplete, allocated clusters without a complete directory entry,
or inconsistent FAT copies. An error is reported rather than claiming a complete
save. The filesystem does not provide an atomic power-loss guarantee.

## Validation

Run `make verify-storage HOSTCC=cc` with a native compiler. The tests use
disposable image files, never physical disks. They cover FAT16/FAT32, MBR and
logical partitions, unpartitioned volumes, fragmented allocation, name collisions,
preservation of old files and surrounding sectors, FAT32 root growth, full disks
and directories, malformed layouts, dirty/divergent FATs, and injected data and
metadata write failures. Separate tests exercise E820 bounds, reserved holes,
destination routing, deferred writes and truncation summaries.

These host tests do not execute BIOS interrupts. A QEMU TCG/SeaBIOS boot run
with a disposable partitioned FAT16 disk completed two passes and saved a
4,513,282-byte structured log as `RES00001.TXT`. The existing `RESULTS.TXT`
retained its original bytes, both FAT copies matched, file chains stayed in
bounds, and the parsed records matched all 21,396 outcomes. This exercises
EDD/E820 through the guest BIOS; PCBox and physical-machine validation remain
separate. Use a disposable FAT disk image for that first run on another target.
`tools/extract_results.py` remains a
reader for this project's FAT12 floppy; open HDD results through a FAT-capable
OS or image tool.

## References

- [Phoenix EDD specification](https://www.singlix.com/trdos/archive/pdf_archive/ATA_EDD_11.PDF)
- [ACPI E820 interface](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/15_System_Address_Map_Interfaces/int-15h-e820h---query-system-address-map.html)
- [Microsoft FAT specification](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf)
- [FatFs documentation and license](https://elm-chan.org/fsw/ff/doc/appnote.html)
