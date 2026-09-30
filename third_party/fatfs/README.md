# FatFs R0.16, patches 1 and 2

Vendored from ChaN's [official FatFs distribution](https://elm-chan.org/fsw/ff/).
The original license is retained at the top of `ff.c` and `ff.h`. Redistribution
in source and binary form is permitted under that notice.

- Archive: <https://elm-chan.org/fsw/ff/arc/ff16.zip>
- Archive SHA-256: `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e`
- Applied in order: [patch 1](https://elm-chan.org/fsw/ff/patch/ff16p1.diff),
  [patch 2](https://elm-chan.org/fsw/ff/patch/ff16p2.diff).
- `ff.c`, `ff.h`, and `diskio.h` have no project-specific code changes. Line
  endings in `ff.c` are normalized to LF.

`ffconf.h` uses code page 437, short filenames, a shared sector buffer, fixed
512-byte sectors, and a fixed timestamp (2026-01-01). It ignores FAT32 free-space
hints and scans the FAT. Formatting, exFAT, long filenames, dynamic allocation,
and threading are disabled. The logger only creates short filenames in the root;
existing long-name entries remain on disk.

`src/storage.c` supplies the disk interface and confines reads/writes to the
selected partition. Write errors remain latched through `CTRL_SYNC`, including
errors in secondary FAT and FSInfo writes. This matters because upstream FatFs
does not check every secondary metadata write's return value directly.

The build is offline: downloading FatFs is not part of `make`.
