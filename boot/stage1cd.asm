; Leact-Loader 1.2 - boot sector of the install CD (El Torito, no emulation).
; The BIOS loads this at 0x7C00; it reads stage 2 (CD sectors 25..26, 2048 bytes each) to 0x7E00 and jumps there.
bits 16
org 0x7C00
    jmp 0x0000:go             ; some BIOSes start at 07C0:0000
go:
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
        dw 2                  ; CD sectors
        dw 0x7E00, 0
        dd 25, 0              ; first CD sector of stage 2
drive:  db 0
msg:    db "Leact-Loader: cannot read the CD", 13, 10, 0
times 512 - ($ - $$) db 0
