#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <z80.h>
#include <arch/zxn.h>

#include "font.h"
#include "crtio.h"

#define REPEAT_DELAY 20
#define REPEAT_RATE  2

#define START_BANKS     0x4000
#define START_MAP       0x4000
#define START_TILE_DEF  0x5400      // After 80x32 timemap with attributes
#define OFFSET_MAP      ((START_MAP - START_BANKS) >> 8)
#define OFFSET_TILES    ((START_TILE_DEF - START_BANKS) >> 8)

extern uint8_t kbstate[];
const uint8_t unshifted [] = {
    'a','s','d','f','g',
    'q','w','e','r','t',
    '1','2','3','4','5',
    '0','9','8','7','6',
    'p','o','i','u','y',
    0x0d,'l','k','j','h',
    ' ',0xff,'m','n','b',
    0xff,'z','x','c','v',
};

const uint8_t caps [] = {
    'A','S','D','F','G',
    'Q','W','E','R','T',
    0x01,0x02,0x03,0x04,0x05,
    0x0a,0x09,0x08,0x07,0x06,
    'P','O','I','U','Y',
    0x1d,'L','K','J','H',
    0x1b,0xff,'M','N','B',
    0xff,'Z','X','C','V',
};

const uint8_t sym[] = {
    '~','|','\\','{', '}',
    0x7f,0x10,0x11,'<','>',
    '!','@','#','$','%',
    '_',')','(',0x27,'&',
    0x22,';',0x12,']','[',
    0x1e,'=','+','-','^',
    0x1c,0xff,'.',',','*',
    0xff,':','`','?','/',
};

const uint8_t ext[] = {
    'a'|0x80,'s'|0x80,'d'|0x80,'f'|0x80,'g'|0x80,
    'q'|0x80,'w'|0x80,'e'|0x80,'r'|0x80,'t'|0x80,
    '1'|0x80,'2'|0x80,'3'|0x80,'4'|0x80,'5'|0x80,
    '0'|0x80,'9'|0x80,'8'|0x80,'7'|0x80,'6'|0x80,
    'p'|0x80,'o'|0x80,'i'|0x80,'u'|0x80,'y'|0x80,
    0x8d,    'l'|0x80,'k'|0x80,'j'|0x80,'h'|0x80,
    0xa0,    0xff,    'm'|0x80,'n'|0x80,'b'|0x80,
    0xff,    'z'|0x80,'x'|0x80,'c'|0x80,'v'|0x80,
};

char * const screen = (char * const)START_MAP;

uint8_t old_reg_6b;
uint8_t old_reg_15;
uint8_t old_border;

uint8_t cx = 0;         // caret X
uint8_t cy = 0;         // caret Y
uint8_t attr = 0;       // current attribute

uint16_t ticks;         // internal ticker to track

uint8_t caret_state[] = {
    0,              // 0x35 - X
    0,              // 0x36 - Y
    0b00000000,     // 0x37 - X9
    0b01000000,     // 0x38 - Visible, Enable Attr-4
    0b10000000,     // 0x39 - 4-bit sprite, Y9
};

extern void kbd_scan(void) MYCC;

extern void setup_caret_sprite(void) MYCC;
void position_caret(void) MYCC;
void update_caret(void) MYCC;

void screen_init(void) MYCC {
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

    // Hide all sprites
    for (uint8_t i=0;i<64;++i) {
        ZXN_NEXTREGA(0x34, i);
        ZXN_NEXTREG(0x38, 0);
    }

    memcpy((void*)START_TILE_DEF, &font_crtio[0], sizeof(font_crtio));
    
    setup_caret_sprite();
    update_caret();
    show_caret();
    cls();
}

void screen_restore(void) MYCC {
    memset((void*)START_MAP, 0, 6144);
    hide_caret();
    ZXN_NEXTREGA(0x6b, old_reg_6b);
    ZXN_NEXTREGA(0x15, old_reg_15);
    zx_border(old_border);
}

void cls(void) MYCC {
    memset(screen, 0, (SCREEN_WIDTH * SCREEN_HEIGHT)*2);
    cx=0;
    cy=0;
}

void putch(char ch) MYCC {
    if (ch == NL) {
        if (cy < SCREEN_HEIGHT-1) {
            ++cy;            
        }
        cx=0;
        return;
    }
    if (ch < 32 || ch > 128) ch=128;

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

void clreol(void) MYCC {
    if (cx > SCREEN_WIDTH-1) return;
    char * p = screen+(((cy*SCREEN_WIDTH) + cx) << 1);
    memset(p, 0, (SCREEN_WIDTH - cx) << 1);    
}

void print(const char *fmt, ...) MYCC {
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

char kbhandler(void) MYCC {
    kbd_scan();
    
    uint8_t shift = (uint8_t)((kbstate[7] & 1) | (kbstate[6] & 2));
    
    const uint8_t *tbl = unshifted;    
    if (shift == 1) tbl = caps;
    else if (shift == 2) tbl = sym;
    else if (shift == 3) tbl = ext;

    // Switch off the shift flags
    kbstate[6] &= 0xfd;
    kbstate[7] &= 0xfe;
    
    for (uint8_t i=0; i<8; ++i, tbl += 5) {
        uint8_t c = kbstate[i];
        if (!c) continue;
        for (uint8_t j=0; c; c >>= 1, ++j) {
            if (c & 1) return tbl[j];                                        
        }
    } 
    return 0;  
}

char getch(void) MYCC {
    static char lastkey = 0;
    static uint8_t repeating = 0;
    static uint8_t repeat_delay = 0;
    position_caret();
    for(;;) {
        __asm__("halt");
        ++ticks;
        toggle_caret();
        char key = kbhandler();
        if (!key) {
            lastkey = 0;
            repeating = 0;
            continue;
        }
        if (key != lastkey) {
            lastkey = key;
            repeat_delay = 0;
            return key;
        } else {
            ++repeat_delay;
            if (repeat_delay < REPEAT_DELAY) continue;
            if (repeat_delay < REPEAT_DELAY+REPEAT_RATE) continue;
            repeat_delay = REPEAT_DELAY;
            return key;
        }
    }  
}

void position_caret(void) MYCC {
    uint16_t x = cx * 4;
    uint16_t y = cy * 8;

    caret_state[0] = (uint8_t)x;                        // 0x35
    caret_state[1] = (uint8_t)y;                        // 0x36
    caret_state[2] = (uint8_t)((x >> 8) & 1);           // 0x37
    caret_state[3] |= 0x80;                             // 0x38
    caret_state[4] = (uint8_t)(0x80 | ((y >> 8) & 1));  // 0x39
    
    update_caret();
}

void update_caret(void) MYCC {
    ZXN_NEXTREG(0x34, 0);
    ZXN_NEXTREGA(0x35, caret_state[0]);
    ZXN_NEXTREGA(0x36, caret_state[1]);
    ZXN_NEXTREGA(0x37, caret_state[2]);
    ZXN_NEXTREGA(0x38, caret_state[3]);
    ZXN_NEXTREGA(0x39, caret_state[4]);
}

void show_caret(void) MYCC {
    caret_state[3] |= 0x80;         // 0x38
    update_caret();
}

void hide_caret(void) MYCC {
    caret_state[3] &= 0x7f;         // 0x38
    update_caret();
}

void toggle_caret(void) MYCC {
    static uint8_t rate = 0;
    if ((++rate & 15) == 0) {
        caret_state[3] ^= 0x80;     // 0x38
        update_caret();    
    }
}

void set_cursor_pos(uint8_t x, uint8_t y) MYCC {
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH-1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT-1;
    cx = x;
    cy = y;
}

void get_cursor_pos(uint8_t *x, uint8_t *y) MYCC {
    *x = cx;
    *y = cy;
}

void highlight(void) MYCC {
    attr = 0b00010000;
}

void standard(void) MYCC {
    attr = 0b00000000;
}

uint16_t get_ticks(void) MYCC {
    return ticks;
}

