#!/usr/bin/env python3
"""Build a 1.44 MiB FAT12 floppy containing the fixed-location test kernel.

The boot sector reserves sectors 1..256 for the kernel.  The first root entry
is RESULTS.TXT, whose FAT chain is preallocated over all remaining data
clusters.  The guest logger leaves its size at zero until the user chooses Y.
"""
from pathlib import Path
import argparse, struct

SECTOR=512
TOTAL=2880
KERNEL_SECTORS=256
RESERVED=1+KERNEL_SECTORS
SPF=9
NFATS=2
ROOT_ENTRIES=224
ROOT_SECTORS=(ROOT_ENTRIES*32 + SECTOR-1)//SECTOR
FAT1=RESERVED
FAT2=FAT1+SPF
ROOT=RESERVED+NFATS*SPF
DATA=ROOT+ROOT_SECTORS
DATA_SECTORS=TOTAL-DATA


def fat12_set(fat: bytearray, cluster: int, value: int) -> None:
    # Two 12-bit entries share three bytes. Preserve the adjacent entry's
    # nibble when updating either member of the pair (even: low, odd: high).
    value &= 0xFFF
    off = cluster + cluster//2
    if cluster & 1:
        fat[off] = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
        fat[off+1] = (value >> 4) & 0xFF
    else:
        fat[off] = value & 0xFF
        fat[off+1] = (fat[off+1] & 0xF0) | ((value >> 8) & 0x0F)


def main() -> None:
    ap=argparse.ArgumentParser()
    ap.add_argument('--boot',type=Path,required=True)
    ap.add_argument('--kernel',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()

    boot=args.boot.read_bytes(); kernel=args.kernel.read_bytes()
    if len(boot)!=SECTOR or boot[510:512]!=b'\x55\xaa':
        raise SystemExit('boot.bin must be exactly 512 bytes with 55 AA signature')
    if len(kernel)>KERNEL_SECTORS*SECTOR:
        raise SystemExit(f'kernel is {len(kernel)} bytes; maximum is {KERNEL_SECTORS*SECTOR}')

    img=bytearray(TOTAL*SECTOR)
    img[:SECTOR]=boot
    img[SECTOR:SECTOR+len(kernel)]=kernel

    fat=bytearray(SPF*SECTOR)
    # Media descriptor + reserved cluster.
    fat12_set(fat,0,0xFF0)
    fat12_set(fat,1,0xFFF)
    first=2
    last=first+DATA_SECTORS-1
    for c in range(first,last): fat12_set(fat,c,c+1)
    fat12_set(fat,last,0xFFF)
    img[FAT1*SECTOR:(FAT1+SPF)*SECTOR]=fat
    img[FAT2*SECTOR:(FAT2+SPF)*SECTOR]=fat

    root=bytearray(ROOT_SECTORS*SECTOR)
    ent=bytearray(32)
    ent[0:11]=b'RESULTS TXT'
    ent[11]=0x20                 # archive
    struct.pack_into('<H',ent,26,first)
    struct.pack_into('<I',ent,28,0)  # invisible/empty until guest commits
    root[:32]=ent
    img[ROOT*SECTOR:(ROOT+ROOT_SECTORS)*SECTOR]=root

    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(img)
    print(f'wrote {args.output} ({len(img)} bytes)')
    print(f'kernel {len(kernel)} bytes / {KERNEL_SECTORS*SECTOR} reserved')
    print(f'FAT12: reserved={RESERVED} fat1={FAT1} fat2={FAT2} root={ROOT} data={DATA}')
    print(f'RESULTS.TXT capacity={DATA_SECTORS*SECTOR} bytes, clusters 2..{last}')

if __name__=='__main__': main()
