#ifndef CRTIO_H_
#define CRTIO_H_

#include <stdint.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   32

#define KEY_TAB         0x04
#define KEY_ESC         0x01
#define KEY_CAPSLOCK    0x02
#define KEY_LEFT        0x05
#define KEY_DOWN        0x06
#define KEY_UP          0x07
#define KEY_RIGHT       0x08
#define KEY_BACKSPACE   0x0a
#define KEY_ENTER       0x0d

#define KEY_WORDLEFT    181
#define KEY_PAGEDOWN    182
#define KEY_PAGEUP      183
#define KEY_WORDRIGHT   184

#define KEY_MARK        237
#define KEY_PASTE       246
#define KEY_COPY        227
#define KEY_CUT         248
#define KEY_QUIT        241
#define KEY_FIND        230
#define KEY_SAVE        243
#define KEY_GOTO        231
#define KEY_CUTLINE     235 // ^K

#define NL              '\r'

void screen_init(void) MYCC;
void screen_restore(void) MYCC;

void show_caret(void) MYCC;
void hide_caret(void) MYCC;
void toggle_caret(void) MYCC; 

void cls(void) MYCC;
void clreol(void) MYCC;
void gotosol(void) MYCC;

void putch(char ch) MYCC;
void putch_at(uint8_t x, uint8_t y, char ch) MYCC;
void print(const char *fmt, ...) MYCC;
char getch(void) MYCC;

void set_cursor_pos(uint8_t x, uint8_t y) MYCC;
void get_cursor_pos(uint8_t *x, uint8_t *y) MYCC;

void highlight(void) MYCC;
void standard(void) MYCC;
void set_attr_at(uint8_t x, uint8_t y, uint8_t a) MYCC;

uint16_t get_ticks(void) MYCC;

#endif //CRTIO_H_