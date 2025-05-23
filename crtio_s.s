SECTION code_l
PUBLIC _setup_caret_sprite
PUBLIC _kbd_scan
PUBLIC _kbstate

_setup_caret_sprite:        
        push af
        push bc
        ld bc, #0x303b
        xor a
        out (c), a                  ; select pattern 0
        ld b, #0x80                 ; write 128 _In_reads_bytes_
        ld c, #0x5b
caret_sprite_next:
        ld a, b
        cp #64
        jr c, caret_sprite_transparent
        and #7
        jr nz, caret_sprite_transparent
        ld a, #0x03
        jr caret_sprite_write_byte
caret_sprite_transparent:
        ld a, #0x33
caret_sprite_write_byte:
        out (c), a
        djnz caret_sprite_next
        pop bc
        pop af
        nextreg #0x15, #0b01000011  ; Enable sprites, SLU
        ret

_kbd_scan:
        ld hl, #_kbstate
        ld bc, #0xfdfe
        ld e, #8
scan_port:
        in a, (c)
        rlc b
        cpl
        and #0x1f
        ld (hl), a
        inc hl
        dec e
        jr nz, scan_port
        ret

_kbstate: db 0,0,0,0,0,0,0,0