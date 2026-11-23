ENTRY(_start)
SECTIONS {
  . = 1M;
  .multiboot : { *(.multiboot) }
  .text : ALIGN(16) { *(.text .text.*) }
  .rodata : ALIGN(4096) { *(.rodata .rodata.*) }
  .data : ALIGN(4096) { *(.data .data.*) }
  .bss : ALIGN(4096) { *(COMMON) *(.bss .bss.*) }
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
