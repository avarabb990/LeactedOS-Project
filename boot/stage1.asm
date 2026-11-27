; LeactedOS boot sector (first 446 bytes of sector 0; the partition table follows).
; Loads stage 2 (sectors 8..15 of the disk) to 0x7E00 with the BIOS extended read and jumps to it.
bits 16
org 0x7C00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7BF0
    sti
    cld
    mov [drive], dl
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc fail
    cmp dword [0x7E04], 0x3254534C      ; 'LST2'
    jne fail
    mov dl, [drive]
    jmp 0x0000:0x7E00
fail:
    mov si, msg
.p: lodsb
    test al, al
    jz .h
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp .p
.h: cli
    hlt
    jmp .h
dap:    db 0x10, 0
        dw 8                  ; sectors
        dw 0x7E00, 0          ; offset, segment
        dd 8, 0               ; LBA
drive:  db 0
msg:    db "LeactedOS: cannot load the boot loader", 13, 10, 0
times 446 - ($ - $$) db 0
