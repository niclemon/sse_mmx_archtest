#!/usr/bin/env python3
"""Read a committed RESULTS.TXT from this project's floppy image.

The root entry supplies file size and first cluster. Follow 12-bit FAT links
and trim the last sector to that size. Assumes the project's 512-byte sectors
and one sector per cluster; this is not a general FAT filesystem reader.
"""
from pathlib import Path
import argparse, struct
SECTOR=512

def fat12_get(fat:bytes,c:int)->int:
    o=c+c//2; w=fat[o]|fat[o+1]<<8
    return (w>>4)&0xfff if c&1 else w&0xfff

def main():
    ap=argparse.ArgumentParser();ap.add_argument('image',type=Path);ap.add_argument('output',type=Path,nargs='?',default=Path('RESULTS.TXT'));a=ap.parse_args()
    b=a.image.read_bytes()
    reserved=struct.unpack_from('<H',b,14)[0]; nfats=b[16]; spf=struct.unpack_from('<H',b,22)[0]; roots=struct.unpack_from('<H',b,17)[0]
    root_secs=(roots*32+511)//512; root_lba=reserved+nfats*spf; data_lba=root_lba+root_secs
    root=b[root_lba*SECTOR:(root_lba+root_secs)*SECTOR]
    ent=None
    for i in range(0,len(root),32):
        if root[i:i+11]==b'RESULTS TXT': ent=root[i:i+32];break
    if ent is None: raise SystemExit('RESULTS.TXT root entry not found')
    size=struct.unpack_from('<I',ent,28)[0]; c=struct.unpack_from('<H',ent,26)[0]
    if size==0: raise SystemExit('RESULTS.TXT has size 0 (log was not committed, or N was selected)')
    fat=b[reserved*SECTOR:(reserved+spf)*SECTOR]; out=bytearray()
    while len(out)<size and 2<=c<0xff8:
        lba=data_lba+(c-2);out += b[lba*SECTOR:(lba+1)*SECTOR];c=fat12_get(fat,c)
    a.output.write_bytes(out[:size]);print(f'extracted {size} bytes to {a.output}')
if __name__=='__main__':main()
