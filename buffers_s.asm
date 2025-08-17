SECTION code_l
EXTERN _pages
PUBLIC _get_text_ptr
PUBLIC _get_text_char
PUBLIC _set_text_char

_get_text_ptr:
    ld c, d
    ld b,5
get_text_ptr_SR:
    srl h
    rr  l 
    rr  c
    djnz get_text_ptr_SR
    
    ld hl,_pages
    add hl, bc
    ld a,(hl)
    
    nextreg 0x56,a
    
    ld a,d
    and 0x1f
    ld d, a
    add de,0xc000    
    ret

_get_text_char:
    call _get_text_ptr
    ld a, (de)
    ret

_set_text_char:
    call _get_text_ptr
    ld hl, 2   ; Stack offset to character argument
    add hl, sp 
    ld a, (hl)
    ld (de), a
    pop hl
    inc sp
    jp (hl)


