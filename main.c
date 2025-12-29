#include <stdlib.h>
#include <stdio.h>
#include <z80.h>
#include <arch/zxn.h>
#include <intrinsic.h>

#include "platform.h"
#include "buffers.h"
#include "settings.h"
#include "crtio.h"
#include "editor.h"

uint8_t oldspeed;
uint8_t old_mmu7;

void cleanup(void) {
    ula_screen_restore();
    screen_restore();
    buffers_release();    
    ZXN_WRITE_MMU7(old_mmu7);
    ZXN_NEXTREGA(0x07, oldspeed);
}

void init(void) {
    atexit(cleanup);
    old_mmu7 = ZXN_READ_MMU7();
    oldspeed = ZXN_READ_REG(0x07) & 0x03;
    ZXN_NEXTREG(0x07, 3);    
}

int main(int argc, char *argv[]) { 
    init();
    
    ula_screen_save();

    // Load editor settings
    if (settings_load(NULL) == -1) {
        // If not found, save default settings
        settings_save(NULL);
    }
    settings_apply();

    screen_init();
    buffers_init();

    char *filename = NULL;
    int32_t line = 0;
    int32_t col = 0;

    for (int i=1; i<argc; ++i) {
        if (*argv[i] == '+') {
            const char *p = argv[i];
            ++p; // skip '+'
            line = to_int32(p, &p);
            if (*p == ',') {
                ++p; // skip ','
                col = to_int32(p, &p);
            }
        } else {
            filename = argv[i];
        }
    }    
    edit(filename, line, col);
    
    return 0;
}

