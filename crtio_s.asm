SECTION code_l
PUBLIC _kbd_scan
PUBLIC _kbstate

_kbd_scan:
        ld hl, _kbstate
        ld bc, 0xfdfe
        ld e, 8
scan_port:
        in a, (c)
        rlc b
        cpl
        and 0x1f
        ld (hl), a
        inc hl
        dec e
        jr nz, scan_port
        ret

_kbstate: db 0,0,0,0,0,0,0,0