#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <z80.h>
#include <arch/zxn.h>

#include "font.h"
#include "crtio.h"

#define START_BANKS     0x4000
#define START_MAP       0x4000
#define START_TILE_DEF  0x5400      // After 80x32 timemap with attributes
#define OFFSET_MAP      ((START_MAP - START_BANKS) >> 8)
#define OFFSET_TILES    ((START_TILE_DEF - START_BANKS) >> 8)

typedef struct KeyMapEntry {
    uint8_t code;
    uint8_t alt;
} KeyMapEntry;

KeyMapEntry keymap[] = {
    {.code = 0xc3, .alt = 0x7c}, // '|'
    {.code = 0xc5, .alt = 0x5d}, // ']'
    {.code = 0xc6, .alt = 0x5b}, // '['
    {.code = 0xcb, .alt = 0x7d}, // '}'
    {.code = 0xcc, .alt = 0x7b}, // '{'
    {.code = 0xcd, .alt = 0x5c}, // '\'
    {.code = 0xe2, .alt = 0x7e}, // '~'
    
};

char * const screen = (char *)START_MAP;

uint8_t old_reg_6b;
uint8_t old_reg_15;
uint8_t old_border;

uint8_t cx = 0;         // caret X
uint8_t cy = 0;         // caret Y
uint8_t attr = 0;       // current attribute

uint8_t caret_state[] = {
    0,              // 0x35 - X
    0,              // 0x36 - Y
    0b00000000,     // 0x37 - X9
    0b01000000,     // 0x38 - Visible, Enable Attr-4
    0b10000000,     // 0x39 - 4-bit sprite, Y9
};

extern void setup_caret_sprite(void);
void position_caret(void);
void update_caret(void);

void screen_init(void) {
    cx = 0;
    cy = 0;
    old_reg_6b = ZXN_READ_REG(0x6b);
    old_reg_15 = ZXN_READ_REG(0x15);
    old_border = ((*(uint8_t*)(0x5c48)) & 0b00111000) >> 3;

    zx_border(1);
    // Sprite palette
    ZXN_NEXTREG(0x43, 0b00100000);      // Sprite palette 1 auto increment
    ZXN_NEXTREG(0x40, 0);             
    ZXN_NEXTREG(0x41, 0b11001111);      // 0-Purple

    // Tilemap palette
    ZXN_NEXTREG(0x43, 0b00110000);      // Tilemap palette 1 auto increment
    ZXN_NEXTREG(0x40, 0);             
    ZXN_NEXTREG(0x41, 0b00000010);      // 0-Blue
    ZXN_NEXTREG(0x41, 0b11111100);      // 1-Yellow
    
    ZXN_NEXTREG(0x40, 16);             
    ZXN_NEXTREG(0x41, 0b11111100);      // 0-Yellow
    ZXN_NEXTREG(0x41, 0b00000010);      // 1-Blue

    ZXN_NEXTREG(0x6b, 0b11001001);      // 80x32 text mode with attributes
    ZXN_NEXTREGA(0x6e, OFFSET_MAP);
    ZXN_NEXTREGA(0x6f, OFFSET_TILES);

    // Set tilemap defaults
    // Reset clip window
    ZXN_NEXTREG(0x1c, 0b00001000);      // Reset tilemap clip index
    ZXN_NEXTREG(0x1b, 0);               // X1 - 0
    ZXN_NEXTREG(0x1b, 159);             // X2 - 318
    ZXN_NEXTREG(0x1b, 0);               // Y1 - 0
    ZXN_NEXTREG(0x1b, 255);             // Y1 - 255

    // Reset scroll offsets
    ZXN_NEXTREG(0x2f, 0);               // X Scroll offset - MSB
    ZXN_NEXTREG(0x30, 0);               // X Scroll offset - LSB
    ZXN_NEXTREG(0x31, 0);               // Y Scroll offset

    memcpy((void*)START_TILE_DEF, &font_crtio[0], sizeof(font_crtio));
    
    setup_caret_sprite();
    update_caret();
    show_caret();
    cls();
}

void screen_restore(void) {
    memset((void*)START_MAP, 0, 6144);
    ZXN_NEXTREGA(0x6b, old_reg_6b);
    ZXN_NEXTREGA(0x15, old_reg_15);
    zx_border(old_border);
}

void cls(void) {
    memset(screen, 0, (SCREEN_WIDTH * SCREEN_HEIGHT)*2);
    cx=0;
    cy=0;
}

void putch(char ch) {
    if (ch == NL) {
        if (cy < SCREEN_HEIGHT-1) {
            ++cy;            
        }
        cx=0;
        return;
    }
    if (ch < 32 || ch > 128) return;

    if (cx > SCREEN_WIDTH-1) {
        cx = 0;
        if (cy < SCREEN_HEIGHT-1) {
            ++cy;            
        }
    }
    char * p = screen+(((cy*SCREEN_WIDTH) + cx) << 1);
    *p++ = ch - 32;
    *p = attr;
    ++cx;  
}

void clreol(void) {
    if (cx > SCREEN_WIDTH-1) return;
    char * p = screen+(((cy*SCREEN_WIDTH) + cx) << 1);
    memset(p, 0, (SCREEN_WIDTH - cx) << 1);    
}

void print(const char *fmt, ...) {
    char buf[80];
    va_list v;
    va_start(v, fmt);
    vsnprintf(buf, sizeof(buf), (char*)fmt,v);
    va_end(v);
    char *p = &buf[0];
    while (*p){
        putch(*p++);
    }
}

char getch(void) {
    char *lastk = (char*)0x5c08;
    position_caret();
    *lastk = 0;
    do {
        __asm__("halt");
        toggle_caret();
    } while(*lastk == 0);
    char ch = *lastk;
    if (ch > 128) {
        for(uint8_t i=0; i<sizeof(keymap)/sizeof(KeyMapEntry); ++i){
            if (ch == keymap[i].code) {
                ch = keymap[i].alt;
                break;
            }
        }
    }
    return ch;
}

void position_caret(void) {
    uint16_t x = cx * 4;
    uint16_t y = cy * 8;

    caret_state[0] = (uint8_t)x;                        // 0x35
    caret_state[1] = (uint8_t)y;                        // 0x36
    caret_state[2] = (uint8_t)((x >> 8) & 1);           // 0x37
    caret_state[3] |= 0x80;                             // 0x38
    caret_state[4] = (uint8_t)(0x80 | ((y >> 8) & 1));  // 0x39
    
    update_caret();
}

void update_caret(void) {
    ZXN_NEXTREG(0x34, 0);
    ZXN_NEXTREGA(0x35, caret_state[0]);
    ZXN_NEXTREGA(0x36, caret_state[1]);
    ZXN_NEXTREGA(0x37, caret_state[2]);
    ZXN_NEXTREGA(0x38, caret_state[3]);
    ZXN_NEXTREGA(0x39, caret_state[4]);
}

void show_caret(void) {
    caret_state[3] |= 0x80;         // 0x38
    update_caret();
}

void hide_caret(void) {
    caret_state[3] &= 0x7f;         // 0x38
    update_caret();
}

void toggle_caret(void) {
    static uint8_t rate = 0;
    if ((++rate & 15) == 0) {
        caret_state[3] ^= 0x80;     // 0x38
        update_caret();    
    }
}

void set_cursor_pos(uint8_t x, uint8_t y) {
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH-1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT-1;
    cx = x;
    cy = y;
}

void get_cursor_pos(uint8_t *x, uint8_t *y) {
    *x = cx;
    *y = cy;
}

void highlight(void) {
    attr = 0b00010000;
}

void standard(void) {
    attr = 0b00000000;
}

