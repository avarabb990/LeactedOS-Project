ENTRY(_start)
SECTIONS {
  . = 1M;
  __image_start = .;
  .multiboot : { *(.multiboot) }
  .text : ALIGN(16) { *(.text .text.*) }
  .rodata : ALIGN(4096) { *(.rodata .rodata.*) }
  .data : ALIGN(4096) { *(.data .data.*) }
  __image_end = .;
  .bss : ALIGN(4096) { *(COMMON) *(.bss .bss.*) }
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
