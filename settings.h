#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>

typedef struct {
    uint8_t background;
    uint8_t foreground;
    uint8_t highlight;
    uint8_t caret_default;
    uint8_t caret_caps;
    uint8_t caret_graphics;
} EditorSettings;

extern EditorSettings current_settings;

int settings_load(const char* path);
int settings_save(const char* path);
void settings_apply(void);

#endif // SETTINGS_H_
