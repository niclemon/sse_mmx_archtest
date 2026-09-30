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
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*) }
}
