#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>

typedef enum {
    LINE_ENDING_NONE = -1,
    LINE_ENDING_CR = 0,
    LINE_ENDING_LF = 1,
    LINE_ENDING_CRLF = 2
} LineEndingStyle;

typedef struct {
    uint8_t background;
    uint8_t foreground;
    uint8_t highlight;
    uint8_t caret_default;
    uint8_t caret_caps;
    uint8_t caret_graphics;
    uint8_t repeat_delay;
    uint8_t repeat_rate;
    uint8_t blink_rate;
    uint8_t key_beep_cycles;    
    uint16_t key_beep_period;
    uint8_t sticky_extend;
    LineEndingStyle default_line_ending;

    char font[256];
} EditorSettings;

extern EditorSettings current_settings;

int settings_load(const char* path);
int settings_save(const char* path);
void settings_apply(void);

#endif // SETTINGS_H_
