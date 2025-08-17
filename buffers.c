#include <stdint.h>
#include <string.h>

#include <z80.h>
#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>

#include "platform.h"
#include "buffers.h"
#include "crtio.h"

// 8K Pages
#define PAGE_SIZE  ((uint32_t)8192)

#define MAX_PAGES ((uint32_t)128)
#define TEXT_PAGE 0xc000

uint8_t pages[MAX_PAGES];
int32_t text_buffer_size;

char scratch_buffer[SCRATCH_BUFFER_SIZE];
char tmpbuffer[MAX_FILENAME_LEN+4];
uint8_t old_mmu6;

void init_buffers(void) {
    memset(pages, 0, sizeof(pages));
    int32_t avail_pages = esx_ide_bank_avail(ESX_BANKTYPE_RAM);
    if (avail_pages > MAX_PAGES) avail_pages = MAX_PAGES;
    text_buffer_size = avail_pages * PAGE_SIZE;
    old_mmu6 = ZXN_READ_MMU6();
    for(int i=0; i<avail_pages;++i){
        pages[i] = esx_ide_bank_alloc(ESX_BANKTYPE_RAM);
    }
}

void release_buffers(void) {
    ZXN_WRITE_MMU6(old_mmu6);
    for(int i=0; i<MAX_PAGES;++i) {
        uint8_t mmu_page = pages[i];
        if (mmu_page) esx_ide_bank_free(ESX_BANKTYPE_RAM, mmu_page);
    }
}
