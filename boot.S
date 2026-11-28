/* LeactedOS XP - multiboot entry + interrupt stubs */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000003            /* align modules, want memory map */
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECK

.section .text
.global _start
_start:
    cli
    lgdt gdt_desc            /* our own flat segments, so we do not depend on the loader's */
    ljmp $0x08, $1f
1:  mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $stack_top, %esp
    cld
    push %ebx            /* multiboot info */
    push %eax            /* multiboot magic */
    call kernel_main
1:  cli
    hlt
    jmp 1b

.global irq0_stub
irq0_stub:
    pusha
    call timer_irq
    popa
    iret

.global exc_table
.macro EXC n
exc_\n:
.if (\n==8)|(\n==10)|(\n==11)|(\n==12)|(\n==13)|(\n==14)|(\n==17)|(\n==21)
.else
    pushl $0
.endif
    pushl $\n
    jmp exc_common
.endm
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
EXC \n
.endr
exc_common:
    cli
    cld
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    push %esp
    call kernel_exception
1:  hlt
    jmp 1b

.global idt_ignore
idt_ignore:
    iret

.section .rodata
.align 4
exc_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long exc_\n
.endr
.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word gdt_desc - gdt - 1
    .long gdt

.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:
.section .note.GNU-stack,"",@progbits
