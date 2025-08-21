SECTION code_l
EXTERN _pages
PUBLIC _get_text_ptr
PUBLIC _get_text_char
PUBLIC _set_text_char

_get_text_ptr:
    ; --- form logical page index in C: C = (L << 3) + (D >> 5) ---
    ld a, l
    rlca
    rlca
    rlca            ; A = L << 3 (with wrap)
    and 0xF8        ; keep bits 7..3 = L[4..0] << 3, zero low 3
    ld c, a

    ld a, d
    and 0xE0        ; keep bits 7..5
    rrca
    rrca
    rrca
    rrca
    rrca            ; A = D >> 5 in bits 2..0, high bits now zero
    add a, c
    ld c, a         ; C = (L<<3) + (D>>5)

    ; --- lookup actual page from _pages[C] and map it at 0xC000 ---
    ld hl, _pages
    ld b, 0
    add hl, bc
    ld a, (hl)
    nextreg 0x56, a     ; MMU slot 6 -> 0xC000

    ; --- build DE pointer inside the 8K window ---
    ld a, d
    and 0x1F
    or 0xC0
    ld d, a
    ret

_get_text_char:
    call _get_text_ptr
    ld a, (de)
    ret
    
_set_text_char:
    call _get_text_ptr
    ld hl, 2            ; Stack offset to character argument
    add hl, sp 
    ld a, (hl)
    ld (de), a
    pop hl
    inc sp
    jp (hl)


