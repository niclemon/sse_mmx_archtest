/* BIOS loads this sector at physical 0x7c00 and supplies the boot drive in DL.
 * The BPB describes FAT12 to host tools, but this loader does not read files:
 * it copies the fixed reserved kernel sectors to physical 0x10000, then jumps.
 * Keep geometry/layout in sync with archtest.h and tools/build_image.py. */
.intel_syntax noprefix
.code16
.section .text
.global _start

.equ KERNEL_SECTORS, 256
.equ LOAD_SEG, 0x1000

_start:
    jmp short boot_code
    nop
    .ascii "MMXSSE  "          # OEM name, 8 bytes
    .word 512                   # bytes/sector
    .byte 1                     # sectors/cluster
    .word KERNEL_SECTORS + 1    # reserved: boot + kernel
    .byte 2                     # FAT copies
    .word 224                   # root entries
    .word 2880                  # total sectors
    .byte 0xf0                  # media descriptor
    .word 9                     # sectors/FAT
    .word 18                    # sectors/track
    .word 2                     # heads
    .long 0                     # hidden sectors
    .long 0                     # large total sectors
    .byte 0                     # drive number
    .byte 0
    .byte 0x29
    .long 0x26081607
    .ascii "MMX SSETEST"        # 11-byte volume label
    .ascii "FAT12   "

boot_code:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov BYTE PTR [boot_drive_local], dl
    mov ax, LOAD_SEG
    mov es, ax
    mov si, 1                   # LBA 1, first kernel sector

load_next:
    cmp si, KERNEL_SECTORS + 1
    jae boot_kernel

    # LBA -> CHS for 1.44 MB floppy: 18 sectors/track, 2 heads.
    mov ax, si
    xor dx, dx
    mov di, 18
    div di                      # AX = track, DX = sector index
    mov cl, dl
    inc cl

    xor dx, dx
    mov di, 2
    div di                      # AX = cylinder, DX = head
    mov ch, al
    mov dh, dl

    mov dl, BYTE PTR [boot_drive_local]
    mov ah, 0x02
    mov al, 1
    xor bx, bx                  # every read starts at ES:0000

    push si
    push es
    int 0x13
    pop es
    pop si
    jc disk_error

    mov ax, es
    add ax, 0x20                # +512 physical bytes
    mov es, ax
    inc si
    jmp load_next

boot_kernel:
    mov dl, BYTE PTR [boot_drive_local]
    ljmp LOAD_SEG, 0x0000

disk_error:
    mov si, OFFSET FLAT:disk_msg
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0e
    xor bh, bh
    mov bl, 7
    int 0x10
    jmp .print
.halt:
    cli
1:  hlt
    jmp 1b

boot_drive_local: .byte 0
disk_msg: .asciz "Disk read error\r\n"

.org 510
.word 0xaa55
