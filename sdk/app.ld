/* Linker script for LeactedOS apps: one flat image, entry point first. */
ENTRY(lapp_main)
SECTIONS {
    . = DEFINED(LAPP_BASE) ? LAPP_BASE : 0;
    .text   : { *(.text.entry) *(.text .text.*) }
    .rodata : ALIGN(4) { *(.rodata .rodata.*) }
    .data   : ALIGN(4) { *(.data .data.*) }
    .bss    : ALIGN(4) { *(.bss .bss.* COMMON) . = ALIGN(4); }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.got*) *(.gnu*) }
}
