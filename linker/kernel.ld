OUTPUT_FORMAT("elf32-i386")
ENTRY(_start16)
SECTIONS {
  . = 0x00010000;
  .entry16 : ALIGN(16) { KEEP(*(.entry16)) }
  .lowdata : ALIGN(16) { *(.lowdata) }
  .text : ALIGN(16) { *(.text*) }
  .rodata : ALIGN(16) { *(.rodata*) }
  .data : ALIGN(16) { *(.data*) }
  __bss_start = .;
  .bss (NOLOAD) : ALIGN(16) { *(.bss*) *(COMMON) }
  __bss_end = .;
  /* Real-mode BIOS pointers use segment 1000h and 16-bit offsets. */
  ASSERT(ADDR(.lowdata) + SIZEOF(.lowdata) <= 0x00020000,
         "BIOS code and buffers exceed the real-mode segment")
  ASSERT(__bss_end < 0x0007e000, "Kernel overlaps the panic/BIOS stacks")
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*) }
}
