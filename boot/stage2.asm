; LeactedOS boot loader, stage 2 (4096 bytes at disk sector 8, loaded at 0x7E00).
; Reads the kernel image from the disk to 1 MB, clears its BSS, builds a small
; multiboot info block and starts the kernel in 32-bit protected mode exactly
; like a multiboot loader would (EAX = 0x2BADB002, EBX = info block).
; The installer patches the five fields below (offsets 8..24).
bits 16
org 0x7E00
S2:
    jmp short start
    nop
    nop
    db 'LST2'                 ; +4  signature checked by stage 1
k_lba    dd 16                ; +8  first sector of the kernel image
k_secs   dd 0                 ; +12 number of sectors (0 = nothing installed)
k_entry  dd 0                 ; +16 entry point (_start)
k_bssend dd 0                 ; +20 end of the kernel's BSS
k_end    dd 0                 ; +24 1 MB + secs*512: BSS is cleared from here

MBI     equ 0x600
BUFSEG  equ 0x1000            ; 64 sectors are read to 0x10000 at a time

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    cld
    mov [drive], dl
    sti
    mov si, m_boot
    call puts
    cmp dword [k_secs], 0
    jne .have
    mov si, m_nokern
    jmp fatal
.have:
    call a20
    call getmem
    call memcheck
    call load
    call bssclr
    call mkmbi
    jmp enter

; ---------------------------------------------------------------- text
putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    pop bx
    pop ax
    ret
puts:
    lodsb
    test al, al
    jz .d
    call putc
    jmp puts
.d: ret
fatal:
    call puts
    mov si, m_halt
    call puts
.h: cli
    hlt
    jmp .h

; ---------------------------------------------------------------- A20 gate
a20chk:                       ; ZF = 1 when A20 is OFF (memory wraps around at 1 MB)
    push ds
    push es
    push si
    push di
    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    mov ax, 0xFFFF
    mov es, ax
    mov di, 0x0510
    mov al, [ds:si]
    mov ah, [es:di]
    push ax
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    cmp byte [ds:si], 0xFF
    pop ax
    mov [ds:si], al
    mov [es:di], ah
    pop di
    pop si
    pop es
    pop ds
    ret
a20:
    call a20chk
    jne .ok
    mov ax, 0x2401            ; BIOS
    int 0x15
    call a20chk
    jne .ok
    in al, 0x92               ; fast gate
    or al, 2
    and al, 0xFE
    out 0x92, al
    mov cx, 0xFFFF
.w: call a20chk
    jne .ok
    loop .w
    mov si, m_a20
    jmp fatal
.ok:
    ret

; ---------------------------------------------------------------- memory size (KB above 1 MB)
getmem:
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .old
    test cx, cx
    jnz .have
    mov cx, ax
    mov dx, bx
.have:
    movzx ecx, cx
    movzx edx, dx
    shl edx, 6
    add ecx, edx
    mov [memup], ecx
    ret
.old:
    mov ah, 0x88
    int 0x15
    jc .none
    movzx eax, ax
    mov [memup], eax
    ret
.none:
    mov dword [memup], 15360
    ret
memcheck:
    mov eax, [memup]
    shl eax, 10
    add eax, 0x100000
    cmp [k_bssend], eax
    jbe .ok
    mov si, m_mem
    jmp fatal
.ok: ret

; ---------------------------------------------------------------- "unreal" mode: 4 GB data segments in real mode
unreal:
    cli
    push ds
    push es
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short $ + 2
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    and al, 0xFE
    mov cr0, eax
    pop es
    pop ds
    sti
    ret

; ---------------------------------------------------------------- read the kernel to 1 MB
load:
    mov eax, [k_lba]
    mov [dap_lba], eax
    mov eax, [k_secs]
    mov [remain], eax
    mov dword [dest], 0x100000
.loop:
    mov eax, [remain]
    test eax, eax
    jz .done
    cmp eax, 64
    jbe .ok
    mov eax, 64
.ok:
    mov [chunk], eax
    mov [dap_cnt], ax
    mov byte [tries], 3
.try:
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jnc .read
    xor ax, ax
    mov dl, [drive]
    int 0x13
    dec byte [tries]
    jnz .try
    mov si, m_read
    jmp fatal
.read:
    call unreal               ; (some BIOSes undo it)
    mov esi, BUFSEG * 16
    mov edi, [dest]
    mov ecx, [chunk]
    shl ecx, 7
    a32 rep movsd
    mov eax, [chunk]
    add [dap_lba], eax
    sub [remain], eax
    shl eax, 9
    add [dest], eax
    mov al, '.'
    call putc
    jmp .loop
.done:
    ret

bssclr:
    call unreal
    mov edi, [k_end]
    mov ecx, [k_bssend]
    sub ecx, edi
    jbe .d
    add ecx, 3
    shr ecx, 2
    xor eax, eax
    a32 rep stosd
.d: ret

mkmbi:
    xor ax, ax
    mov es, ax
    mov di, MBI
    mov cx, 100
    xor al, al
    rep stosb
    mov dword [MBI + 0], 0x201        ; flags: memory sizes + boot loader name
    mov dword [MBI + 4], 639
    mov eax, [memup]
    mov [MBI + 8], eax
    mov dword [MBI + 64], MBI + 0x80  ; boot loader name
    mov si, m_name
    mov di, MBI + 0x80
.c: lodsb
    stosb
    test al, al
    jnz .c
    ret

; ---------------------------------------------------------------- go
enter:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp dword 0x08:pm
bits 32
pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov ecx, [k_entry]
    mov eax, 0x2BADB002
    mov ebx, MBI
    jmp ecx
bits 16

; ---------------------------------------------------------------- data
align 8
gdt:    dq 0
        dq 0x00CF9A000000FFFF     ; 0x08 code, 4 GB
        dq 0x00CF92000000FFFF     ; 0x10 data, 4 GB
gdtr:   dw gdtr - gdt - 1
        dd gdt
dap:    db 0x10, 0
dap_cnt dw 0
        dw 0, BUFSEG
dap_lba dd 0, 0
drive   db 0
tries   db 0
memup   dd 0
remain  dd 0
chunk   dd 0
dest    dd 0
m_boot   db "LeactedOS boot loader", 13, 10, "Loading ", 0
m_nokern db "No kernel on this disk. Run the Installer from a running LeactedOS.", 0
m_a20    db "Cannot enable the A20 line.", 0
m_read   db "Disk read error.", 0
m_mem    db "Not enough memory.", 0
m_halt   db 13, 10, "System halted.", 0
m_name   db "LeactedOS", 0
times 4096 - ($ - $$) db 0
