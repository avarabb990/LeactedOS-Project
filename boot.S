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

.global idt_ignore
idt_ignore:
    iret

.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:
.section .note.GNU-stack,"",@progbits
