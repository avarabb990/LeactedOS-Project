; Leact-Loader 1.2 - LeactedOS boot loader, stage 2 (4096 bytes at disk sector 8, loaded at 0x7E00).
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
k_end    dd 0                 ; +24 1 MB + secs*sector size: BSS is cleared from here
k_flags  dd 0                 ; +28 bit 0: booted from the CD (2048-byte sectors, k_lba/k_secs count those)

TIMEOUT equ 5                 ; seconds before the highlighted entry starts by itself
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
    cmp dword [k_secs], 0
    jne .have
    mov ax, 3
    int 0x10
    mov si, m_title
    call puts
    mov si, m_nokern
    jmp fatal
.have:
    call menu
    call a20
    call getmem
    call memcheck
    call load
    call bssclr
    call mkmbi
    jmp enter

; ---------------------------------------------------------------- boot menu (text mode, 80x25)
; cell address in DI for row DH, column DL
setpos:
    push ax
    push cx
    push dx
    mov al, dh
    mov ah, 0
    mov cx, 80
    mul cx
    pop dx
    mov cl, dl
    mov ch, 0
    add ax, cx
    shl ax, 1
    mov di, ax
    pop cx
    pop ax
    ret
str_at:                       ; SI = text, DH/DL = row/col, BL = attribute (ES = 0xB800)
    call setpos
.l: lodsb
    test al, al
    jz .d
    mov ah, bl
    stosw
    jmp .l
.d: ret
fill_at:                      ; CX cells of character AL, attribute BL at DH/DL
    call setpos
    mov ah, bl
    rep stosw
    ret
drawsel:                      ; the two entries, the selected one is shown inverted
    xor bp, bp
.e: mov bl, 0x07
    movzx ax, byte [sel]
    cmp bp, ax
    jne .a
    mov bl, 0x70
.a: mov dh, 5
    mov ax, bp
    add dh, al
    mov dl, 11
    mov cx, 58
    mov al, ' '
    call fill_at
    mov dh, 5
    mov ax, bp
    add dh, al
    mov dl, 13
    mov si, m_entry0
    test bp, bp
    jz .s
    mov si, m_entry1
.s: call str_at
    inc bp
    cmp bp, 2
    jb .e
    ret
drawcount:
    mov dh, 15
    mov dl, 2
    mov si, m_count
    mov bl, 0x07
    jmp str_at
gettick:                      ; AX = low word of the BIOS tick counter (18.2 per second)
    mov ah, 0
    int 0x1A
    mov ax, dx
    ret
menu:
    mov ax, 3                 ; 80x25 text mode, clears the screen
    int 0x10
    mov ah, 1                 ; hide the cursor
    mov cx, 0x2000
    int 0x10
    mov ax, 0xB800
    mov es, ax
    mov dh, 1                 ; title bar
    mov dl, 0
    mov cx, 80
    mov al, ' '
    mov bl, 0x1F
    call fill_at
    mov dh, 1
    mov dl, 32
    mov si, m_title
    call str_at
    mov dh, 3                 ; box
    mov dl, 10
    mov al, 0xC9
    mov bl, 0x07
    call box_row
    mov dh, 10
    mov al, 0xC8
    call box_row
    mov dh, 4
.r: mov dl, 10
    mov cx, 1
    mov al, 0xBA
    call fill_at
    mov dl, 69
    mov cx, 1
    call fill_at
    inc dh
    cmp dh, 10
    jb .r
    mov dh, 12
    mov dl, 2
    mov si, m_help1
    mov bl, 0x07
    call str_at
    mov dh, 13
    mov dl, 2
    mov si, m_help2
    call str_at
    mov byte [sel], 0
    call drawsel
    mov byte [timeout], 1
    call gettick
    mov [t0], ax
    mov byte [cnt_d], '0' + TIMEOUT
    call drawcount
.wait:
    cmp byte [timeout], 0
    je .poll
    call gettick
    sub ax, [t0]
    xor dx, dx
    mov cx, 18
    div cx
    cmp ax, TIMEOUT
    jae .go
    mov cx, TIMEOUT
    sub cx, ax
    add cl, '0'
    cmp cl, [cnt_d]
    je .poll
    mov [cnt_d], cl
    call drawcount
.poll:
    mov ah, 1
    int 0x16
    jz .wait
    mov ah, 0
    int 0x16
    cmp byte [timeout], 0     ; any key stops the countdown
    je .key
    mov byte [timeout], 0
    push ax
    mov dh, 15
    mov dl, 0
    mov cx, 80
    mov al, ' '
    mov bl, 0x07
    call fill_at
    pop ax
.key:
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp al, 13
    je .go
    cmp al, '1'
    je .one
    cmp al, '2'
    je .two
    jmp .wait
.up:
    mov byte [sel], 0
    jmp .redraw
.down:
    mov byte [sel], 1
    jmp .redraw
.one:
    mov byte [sel], 0
    jmp .go
.two:
    mov byte [sel], 1
    jmp .go
.redraw:
    call drawsel
    jmp .wait
.go:
    call drawsel
    xor ax, ax
    mov es, ax
    mov ax, 3                 ; clear, then tell what is starting
    int 0x10
    mov si, m_title
    call puts
    mov si, m_crlf
    call puts
    mov si, m_start0
    cmp byte [sel], 0
    je .p
    mov si, m_start1
.p: call puts
    ret
box_row:                      ; top/bottom border of the box on row DH (corner char in AL)
    mov dl, 10
    mov cx, 1
    mov bl, 0x07
    push ax
    call fill_at
    mov dl, 11
    mov cx, 58
    mov al, 0xCD
    call fill_at
    mov dl, 69
    mov cx, 1
    pop ax
    cmp al, 0xC9
    mov al, 0xBB
    je .c
    mov al, 0xBC
.c: jmp fill_at

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
    mov byte [maxc], 64       ; sectors per read, dwords-per-sector shift, bytes-per-sector shift
    mov byte [dsh], 7
    mov byte [bsh], 9
    test byte [k_flags], 1
    jz .m
    mov byte [maxc], 16       ; CD: 2048-byte sectors
    mov byte [dsh], 9
    mov byte [bsh], 11
.m: mov eax, [k_lba]
    mov [dap_lba], eax
    mov eax, [k_secs]
    mov [remain], eax
    mov dword [dest], 0x100000
.loop:
    mov eax, [remain]
    test eax, eax
    jz .done
    movzx ecx, byte [maxc]
    cmp eax, ecx
    jbe .ok
    mov eax, ecx
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
    mov eax, [chunk]
    mov cl, [dsh]
    shl eax, cl
    mov ecx, eax
    a32 rep movsd
    mov eax, [chunk]
    add [dap_lba], eax
    sub [remain], eax
    mov cl, [bsh]
    shl eax, cl
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
    mov dword [MBI + 0], 0x205        ; flags: memory sizes + kernel command line + boot loader name
    mov dword [MBI + 16], MBI + 0xA0  ; command line: "desktop" or "cmd"
    mov dword [MBI + 4], 639
    mov eax, [memup]
    mov [MBI + 8], eax
    mov dword [MBI + 64], MBI + 0x80  ; boot loader name
    mov si, m_name
    test byte [k_flags], 1
    jz .nm
    mov si, m_namecd
.nm: mov di, MBI + 0x80
.c: lodsb
    stosb
    test al, al
    jnz .c
    mov si, m_cl0
    cmp byte [sel], 0
    je .q
    mov si, m_cl1
.q: mov di, MBI + 0xA0
.d: lodsb
    stosb
    test al, al
    jnz .d
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
maxc    db 0
dsh     db 0
bsh     db 0
memup   dd 0
remain  dd 0
chunk   dd 0
dest    dd 0
sel      db 0
timeout  db 0
t0       dw 0
m_title  db "Leact-Loader 1.2", 0
m_crlf   db 13, 10, 0
m_entry0 db "LeactedOS desktop (Normal)", 0
m_entry1 db "LeactedCmd (command line)", 0
m_help1  db "Use the ", 0x18, " and ", 0x19, " keys to select which entry is highlighted.", 0
m_help2  db "Press enter to boot the selected OS.", 0
m_count  db "The highlighted entry will be started automatically in "
cnt_d    db "5"
         db " seconds. ", 0
m_start0 db "Starting LeactedOS desktop...", 13, 10, "Loading ", 0
m_start1 db "Starting LeactedCmd...", 13, 10, "Loading ", 0
m_cl0    db "desktop", 0
m_cl1    db "cmd", 0
m_nokern db "No kernel on this disk. Run the Installer from a running LeactedOS.", 0
m_a20    db "Cannot enable the A20 line.", 0
m_read   db "Disk read error.", 0
m_mem    db "Not enough memory.", 0
m_halt   db 13, 10, "System halted.", 0
m_name   db "LeactedOS", 0
m_namecd db "LeactedOS CD", 0
times 4096 - ($ - $$) db 0
