#include <stdlib.h>
#include <stdio.h>
#include <z80.h>
#include <arch/zxn.h>

#include "crtio.h"
#include "editor.h"

uint8_t oldspeed;

void cleanup(void) {
    screen_restore();
    ZXN_NEXTREGA(0x07, oldspeed);
}

void init(void) {
    atexit(cleanup);
    oldspeed = ZXN_READ_REG(0x07) & 0x03;
    ZXN_NEXTREG(0x07, 3);
}

int main(int argc, char *argv[]) { 
    init();
    screen_init();

    const char *filename = NULL;
    uint16_t line = 0;
    uint16_t col = 0;

    for (int i=1; i<argc; ++i) {
        if (*argv[i] == '+') {
            char *p = argv[i];
            ++p; // skip '+'
            line = to_uint16(p, &p);
            if (*p == ',') {
                ++p; // skip ','
                col = to_uint16(p, &p);
            }
        } else {
            filename = argv[i];
        }
    }    
    edit(filename, line, col);
    
    return 0;
}

