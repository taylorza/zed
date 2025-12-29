#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <intrinsic.h>
#include <errno.h>

#include <z80.h>
#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>

#include "platform.h"
#include "settings.h"
#include "crtio.h"

#define ULA_ATTR_MAP    0x5800

#define START_BANKS     0x4000
#define START_MAP       0x4000
#define START_TILE_DEF  0x5400      // After 80x32 timemap with attributes
#define OFFSET_MAP      ((START_MAP - START_BANKS) >> 8)
#define OFFSET_TILES    ((START_TILE_DEF - START_BANKS) >> 8)

typedef enum EditMode {
    EDITMODE_INSERT,
    EDITMODE_OVERWRITE,
} EditMode;

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

static uint8_t old_reg_14;
static uint8_t old_reg_15;
static uint8_t old_reg_43;
static uint8_t old_reg_4a;
static uint8_t old_reg_4b;
static uint8_t old_reg_4c;
static uint8_t old_reg_6b;


static uint8_t old_border;

static uint8_t  cx = 0;                 // caret X
static uint8_t  cy = 0;                 // caret Y
static uint8_t  attr = 0;               // current attribute
static uint8_t  repeat_delay = 20;      // key repeat delay counter
static uint8_t  repeat_rate = 2;        // key repeat rate counter
static uint8_t  blink_rate = 15;        // caret blink rate
static uint8_t  key_beep_cycles = 0;    // beep cycles (0-off)
static uint16_t key_beep_period = 280;  // beep sound period

static uint8_t capslock = 0;            // is caplock engaged
static uint8_t codepoint;               // codepoint input accumulator
static uint8_t codepoint_count;         // codepoint input digit count

static uint16_t ticks;                  // internal ticker

static uint8_t caret_state[] = {
    0,              // 0x35 - X
    0,              // 0x36 - Y
    0b00000000,     // 0x37 - X9
    0b01000000,     // 0x38 - Visible, Enable Attr-4
    0b10000000,     // 0x39 - 4-bit sprite, Y9
};

static uint8_t spr_carets[] = {
    0b10000000,
    0b10000000,
    0b10000000,
    0b10000000,
    0b10000000,
    0b10000000,
    0b10000000,
    0b10000000,

    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b11110000,
    0b11110000,
    0b11110000,

    0b00000000,
    0b00000000,
    0b00000000,
    0b00100000,
    0b01100000,
    0b00100000,
    0b00100000,
    0b11110000,

    0b00000000,
    0b00000000,
    0b00000000,
    0b01100000,
    0b10010000,
    0b00100000,
    0b01000000,
    0b11110000,
};


static KeyMode current_key_mode = KEYMODE_NORMAL;
static EditMode current_edit_mode = EDITMODE_INSERT;

extern void kbd_scan(void) MYCC;

static void position_caret(void) MYCC;
static void update_caret(void) MYCC;

void crt_apply_settings(EditorSettings *settings) MYCC {
    /* Sprite palette */
    ZXN_NEXTREG(0x43, 0b00100000);
    ZXN_NEXTREG(0x40, 0);
    ZXN_NEXTREGA(0x41, settings->caret_default);
    ZXN_NEXTREG(0x40, 16);
    ZXN_NEXTREGA(0x41, settings->caret_caps);
    ZXN_NEXTREG(0x40, 32);
    ZXN_NEXTREGA(0x41, settings->caret_graphics);

    /* Tilemap palette */
    ZXN_NEXTREG(0x43, 0b00110000);
    ZXN_NEXTREG(0x40, 0);
    ZXN_NEXTREGA(0x41, settings->background);
    ZXN_NEXTREGA(0x41, settings->foreground);

    ZXN_NEXTREG(0x40, 16);
    ZXN_NEXTREGA(0x41, settings->foreground);
    ZXN_NEXTREGA(0x41, settings->background);

    ZXN_NEXTREG(0x40, 32);
    ZXN_NEXTREGA(0x41, settings->highlight);
    ZXN_NEXTREGA(0x41, settings->foreground);

    /*ULA 2nd Palette*/
    ZXN_NEXTREG(0x43, 0b01000010);
    ZXN_NEXTREG(0x40, 16); // Paper 0 palette entry
    ZXN_NEXTREGA(0x41, settings->background);

    ZXN_NEXTREG(0x43, 0b00000010); // Select palettes

    repeat_delay = settings->repeat_delay;
    repeat_rate = settings->repeat_rate;
    blink_rate = settings->blink_rate;

    key_beep_period = settings->key_beep_period;
    key_beep_cycles = settings->key_beep_cycles;    
}

void crt_load_font(const char* font_path) MYCC {
    uint8_t fh = 255;

    if (font_path && *font_path) {        
        fh = esxdos_f_open((char*)font_path, ESXDOS_MODE_R | ESXDOS_MODE_OE);                     
    }

    if (fh == 255) {
        // Load default embedded font
        fh = esxdos_m_gethandle();
    }
    
    esxdos_f_read(fh, (void*)START_TILE_DEF, 1792);
    esxdos_f_close(fh);
}

// src: 8-bit input
// dst: array of 4 bytes
// 1 → 0000, 0 → 0011
static void expand_bits_to_nibbles(uint8_t src, uint8_t dst[8]) {
    static const uint8_t map[2] = { 0x03, 0x00 }; 
    dst[0] = (map[(src >> 7) & 1] << 4) | map[(src >> 6) & 1];
    dst[1] = (map[(src >> 5) & 1] << 4) | map[(src >> 4) & 1];
    dst[2] = (map[(src >> 3) & 1] << 4) | map[(src >> 2) & 1];
    dst[3] = (map[(src >> 1) & 1] << 4) | map[(src >> 0) & 1];
}

static void setup_caret_sprites(void) MYCC {
    uint8_t pixels[128];

    uint8_t reg9 = ZXN_READ_REG(0x09);
    ZXN_NEXTREGA(0x09, reg9 & 0b11101111); // Disable sprite lockstep

    IO_SPRITE_SLOT = 0; 
    for(uint8_t i = 0; i < sizeof(spr_carets); i+=8) {
        memset(pixels, 0x33, sizeof(pixels));
        for(uint8_t j = 0; j < 8; ++j) {        
            expand_bits_to_nibbles(spr_carets[i+j], pixels + (j * 8));
        }
        //intrinsic_outi((void*)pixels, __IO_SPRITE_PATTERN, 128);
        for (uint8_t b=0;b<128;++b)
            IO_SPRITE_PATTERN = pixels[b];
    }
}

void ula_screen_save(void) MYCC {    
    old_border = ((*(uint8_t*)(0x5c48)) & 0b00111000) >> 3;
}

void ula_screen_restore(void) MYCC {
    zx_border(old_border);
    zx_cls(PAPER_WHITE | INK_BLACK);
}

void screen_init(void) MYCC {
    cx = 0;
    cy = 0;

    old_reg_14 = ZXN_READ_REG(0x14);
    old_reg_15 = ZXN_READ_REG(0x15);
    old_reg_43 = ZXN_READ_REG(0x43);
    old_reg_4a = ZXN_READ_REG(0x4a);
    old_reg_4b = ZXN_READ_REG(0x4b);
    old_reg_4c = ZXN_READ_REG(0x4c);
    old_reg_6b = ZXN_READ_REG(0x6b);

    ZXN_NEXTREG(0x14, 0xe3);            // Global Transparency index
    ZXN_NEXTREG(0x4a, 0xe3);            // Fallback Transparency index
    ZXN_NEXTREG(0x4b, 0xe3);            // Sprite Transparency index
    ZXN_NEXTREG(0x4c, 0x0f);            // Tilemap Transparency index
    
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


    setup_caret_sprites();
    update_caret();
    show_caret();
    cls();

    ZXN_NEXTREGA(0x15, 0b01000011);     // Enable sprites, SLU

    zx_border(0);
}

void screen_restore(void) MYCC {
    hide_caret();
    ZXN_NEXTREGA(0x14, old_reg_14);
    ZXN_NEXTREGA(0x15, old_reg_15);
    ZXN_NEXTREGA(0x43, old_reg_43);
    ZXN_NEXTREGA(0x4a, old_reg_4a);
    ZXN_NEXTREGA(0x4b, old_reg_4b);
    ZXN_NEXTREGA(0x4c, old_reg_4c);
    ZXN_NEXTREGA(0x6b, old_reg_6b);
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
    if (ch < 32) ch=128;

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
 
void putch_at(uint8_t x, uint8_t y, char ch) MYCC {
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH-1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT-1;
    char * p = screen+(((y*SCREEN_WIDTH) + x) << 1);
    *p++ = ch - 32;
    *p = attr;
}

void clreol(void) MYCC {
    if (cx > SCREEN_WIDTH-1) return;
    char * p = screen+(((cy*SCREEN_WIDTH) + cx) << 1);
    for (uint8_t i=cx; i<SCREEN_WIDTH; ++i) {
        *p++ = 0;
        *p++ = attr;
    }
}

void gotosol(void) MYCC {
    cx = 0;
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

static char kbhandler(void) MYCC {
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

static char process_key(char key) MYCC {
    if (capslock){
        if (key >= 'a' && key <= 'z') key -= 0x20;
        else if (key >= 'A' && key <= 'Z') key += 0x20;
    }
    if (current_key_mode == KEYMODE_GRAPHICS || current_key_mode == KEYMODE_INVGRAPHICS) {
        if (key >= '0' && key <= '8') {
            key += 0x80 - '0'; // Set high bit for graphics
            if (current_key_mode == KEYMODE_INVGRAPHICS) {
                key ^= 0x0f; // Invert lower nibble for inverse graphics
            }
        }
        if (key >= 'a' && key <= 'u' || key >= 'A' && key <= 'U') {
            key = (key & 0x1f) + 0x8f; // Map a-u/A-U to graphics range
        }
    }
    return key;
}



static uint8_t hex_value(char key)
{
    if ((unsigned)(key - '0') <= 9)
        return (uint8_t)(key - '0');

    key |= 0x20; // fold A–F → a–f

    if ((unsigned)(key - 'a') <= 5)
        return (uint8_t)(key - 'a' + 10);

    return 0xFF;
}

static uint8_t handle_hex_key(char key)
{
    uint8_t v = hex_value(key);
    if (v == 0xFF) {
        codepoint_count = 0;
        return 255;
    }

    if (codepoint_count == 0) {
        // First digit: shift into high nibble
        codepoint = (uint8_t)(v << 4);
        codepoint_count = 1;
        return 0;
    }

    // Second digit: OR into low nibble
    codepoint |= v;
    codepoint_count = 2;
    return 1;   // full byte ready
}

static void beep(void) MYCC {
    static uint8_t beeper;
    if (key_beep_cycles) {    
        beeper = 0;
        intrinsic_di();
        for(uint8_t i =  0; i < key_beep_cycles; ++i) {
            IO_FE = beeper ^= 0x10;
            for (volatile uint16_t j=0; j<key_beep_period; ++j) {
                intrinsic_emit(0);
            }
        }
        intrinsic_ei();
    }
}

char getch(void) MYCC {
    static char lastkey = 0;
    static uint8_t repeating = 0;
    static uint8_t delay_counter = 0;

    if (current_key_mode == KEYMODE_CODEPOINT) {
        current_key_mode = KEYMODE_NORMAL;        
    }

    position_caret();
    for(;;) {
        intrinsic_halt();
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
            delay_counter = 0;
            beep();
            if (current_key_mode == KEYMODE_CODEPOINT) {                                
                uint8_t res = handle_hex_key(key);
                switch(res) {
                    case 0: // first digit
                        continue;
                    case 1: // second digit
                        return codepoint;
                    case 255: // invalid digit
                        current_key_mode = KEYMODE_NORMAL;
                        continue;
                }                   
            }
            switch(key) {
                case KEY_CAPSLOCK:
                    capslock = !capslock;
                    continue;
                case KEY_INSERT:
                    if (current_edit_mode == EDITMODE_INSERT)
                        current_edit_mode = EDITMODE_OVERWRITE;
                    else
                        current_edit_mode = EDITMODE_INSERT;
                    continue;
                case KEY_CODEPOINT:
                    current_key_mode = KEYMODE_CODEPOINT;
                    codepoint = 0;
                    codepoint_count = 0;
                    continue;
                case KEY_GRAPH:
                    if (current_key_mode == KEYMODE_GRAPHICS)
                        current_key_mode = KEYMODE_NORMAL;
                    else
                        current_key_mode = KEYMODE_GRAPHICS;
                    continue;  
                case KEY_INVERSE:
                    if (current_key_mode == KEYMODE_INVGRAPHICS)
                        current_key_mode = KEYMODE_GRAPHICS;
                    else
                        current_key_mode = KEYMODE_INVGRAPHICS;
                    continue;              
            }                        
        } else {
            ++delay_counter;
            if (delay_counter < repeat_delay) continue;
            if (delay_counter < repeat_delay+repeat_rate) continue;
            delay_counter = repeat_delay; 
            beep();           
        }

        return process_key(key);
    }  
}

static void position_caret(void) MYCC {
    uint16_t x = cx * 4;
    uint16_t y = cy * 8;

    caret_state[0] = (uint8_t)x;                        // 0x35
    caret_state[1] = (uint8_t)y;                        // 0x36
    caret_state[2] = (uint8_t)((x >> 8) & 1);           // 0x37
    caret_state[3] |= 0x80;                             // 0x38
    caret_state[4] = (uint8_t)(0x80 | ((y >> 8) & 1));  // 0x39
    
    update_caret();
}

static void update_caret(void) MYCC {
    uint8_t palette = capslock ? 1 : 0;
    if (current_key_mode == KEYMODE_GRAPHICS || current_key_mode == KEYMODE_INVGRAPHICS) {
        palette = 2;
    }

    uint8_t pattern = 0;
    if (current_key_mode == KEYMODE_CODEPOINT && codepoint_count < 2) {
        pattern = (uint8_t)(2 + codepoint_count); /* 0 => 2, 1 => 3 */
    } else if (current_edit_mode == EDITMODE_OVERWRITE) {
        pattern = 1;
    }

    ZXN_NEXTREG(0x34, 0);
    ZXN_NEXTREGA(0x35, caret_state[0]);
    ZXN_NEXTREGA(0x36, caret_state[1]);
    ZXN_NEXTREGA(0x37, (caret_state[2] & 0x0f) | (palette << 4));
    ZXN_NEXTREGA(0x38, caret_state[3] | (pattern >> 1));
    ZXN_NEXTREGA(0x39, caret_state[4] | ((pattern & 1) << 6));
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
    if (++rate >= blink_rate) {
        rate = 0;
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

void set_attr(uint8_t a) MYCC {
    attr = a;
}

void set_attr_at(uint8_t x, uint8_t y, uint8_t a) MYCC {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    char * p = screen+(((y*SCREEN_WIDTH) + x) << 1) + 1;
    *p = a;
}

uint16_t get_ticks(void) MYCC {
    return ticks;
}

uint8_t is_insert_mode(void) MYCC {
    return (current_edit_mode == EDITMODE_INSERT) ? 1 : 0;
}

KeyMode get_key_mode(void) MYCC {
    return current_key_mode;
}
