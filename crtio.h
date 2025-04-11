#ifndef CRTIO_H_
#define CRTIO_H_

#include <stdint.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   32

#define KEY_TAB         0x04
#define KEY_ESC         0x07
#define KEY_LEFT        0x08
#define KEY_RIGHT       0x09
#define KEY_UP          0x0b
#define KEY_DOWN        0x0a
#define KEY_BACKSPACE   0x0c
#define KEY_ENTER       0x0d
#define KEY_EXTEND      0x0e
#define KEY_MARK        0x2e
#define KEY_PASTE       0x2f
#define KEY_COPY        0x3f
#define KEY_CUT         0x60
#define KEY_QUIT        0xc7
#define KEY_FIND        0x7b
#define KEY_SAVE        0x7c
#define KEY_GOTO        0x7d

#define NL              '\r'

void screen_init(void);
void screen_restore(void);

void show_caret(void);
void hide_caret(void);
void toggle_caret(void); 

void cls(void);
void clreol(void);

void putch(char ch);
void print(const char *fmt, ...);
char getch(void);

void set_cursor_pos(uint8_t x, uint8_t y);
void get_cursor_pos(uint8_t *x, uint8_t *y);

void highlight(void);
void standard(void);

#endif //CRTIO_H_