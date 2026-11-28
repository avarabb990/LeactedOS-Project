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
fail:                         ; blue screen
    mov ax, 3
    int 0x10
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov cx, 2000
    mov ax, 0x1F20
    rep stosw
    mov ah, 1
    mov cx, 0x2000
    int 0x10
    mov si, msg
    mov di, 160 * 3 + 12
    mov ah, 0x1F
.p: lodsb
    test al, al
    jz .h
    cmp al, 10
    je .n
    stosw
    jmp .p
.n: push ax                   ; next line, 6 columns in
    mov ax, di
    xor dx, dx
    mov cx, 160
    div cx
    inc ax
    mul cx
    add ax, 12
    mov di, ax
    pop ax
    jmp .p
.h: cli
    hlt
    jmp .h
dap:    db 0x10, 0
        dw 8                  ; sectors
        dw 0x7E00, 0          ; offset, segment
        dd 8, 0               ; LBA
drive:  db 0
msg:    db ":(", 10, 10, "Your Leacted PC ran into an error and needs to be", 10, "repaired or reinstalled.", 10, 10, "Stop code: BOOT_LOADER_NOT_FOUND", 10, 10, "Boot the LeactedOS install CD and reinstall.", 0
times 446 - ($ - $$) db 0
