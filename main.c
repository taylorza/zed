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
    edit(argc == 2 ? argv[1] : NULL);
    
    return 0;
}

