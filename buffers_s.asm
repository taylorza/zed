SECTION code_l
EXTERN _pages
PUBLIC _get_text_ptr
PUBLIC _get_text_char
PUBLIC _set_text_char

;-------------------------------------------------------------------------------
; char* get_text_ptr(int32_t idx) __sdcccall(1)
; INPUT
;  HLDE - idx
; OUTPUT
;  DE   - pointer to character in active page
_get_text_ptr:
    ld    b, d
    ld    a, l
    rl    b
    rla
    rl    b
    rla
    rl    b
    rla
    ld    hl, _pages
    add   hl, a
    ld    a, (hl)
    nextreg 0x57, a
    ld    a, d
    or    0xE0
    ld    d, a
    ret

_get_text_char:
    ld    b, d
    ld    a, l
    rl    b
    rla
    rl    b
    rla
    rl    b
    rla
    ld    hl, _pages
    add   hl, a
    ld    a, (hl)
    nextreg 0x57, a
    ld    a, d
    or    0xE0
    ld    d, a
    ld a, (de)
    ret
    
_set_text_char:
    ld    b, d
    ld    a, l
    rl    b
    rla
    rl    b
    rla
    rl    b
    rla
    ld    hl, _pages
    add   hl, a
    ld    a, (hl)
    nextreg 0x57, a
    ld    a, d    
    or    0xE0
    ld    d, a
    ld hl, 2            ; Stack offset to character argument
    add hl, sp 
    ld a, (hl)
    ld (de), a
    pop hl
    inc sp
    jp (hl)


