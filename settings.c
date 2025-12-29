#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include "platform.h"
#include "settings.h"
#include "crtio.h"
#ifdef __ZXNEXT
#include <arch/zxn/esxdos.h>
#endif

static const char* default_settings_file = "c:/sys/zed.cfg";

EditorSettings current_settings = {
    .background         = 0b00000010, /* Blue */
    .foreground         = 0b11111100, /* Yellow */
    .highlight          = 0b00001011, /* Light Blue */
    .caret_default      = 0b11001111, /* magenta */
    .caret_caps         = 0b11111111, /* white */
    .caret_graphics     = 0b00011100, /* green */
    .repeat_delay       = 20,
    .repeat_rate        = 2,
    .blink_rate         = 15,
    .key_beep_cycles    = 0,
    .key_beep_period    = 60,
    .font               = "",
};

static char *trim(char *s) {
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) ++s;
    char *end = s + strlen(s);
    while (end > s && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r' || *(end-1) == '\n')) --end;
    *end = '\0';
    return s;
}

static int16_t parse_number(const char* s) MYCC {
    int16_t v = 0;
    if (strlen(s) >= 2) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
            while (isxdigit((unsigned char)*s)) {
                v <<= 4;
                if (*s >= '0' && *s <= '9') v |= (*s - '0');
                else if (*s >= 'a' && *s <= 'f') v |= (*s - 'a' + 10);
                else if (*s >= 'A' && *s <= 'F') v |= (*s - 'A' + 10);
                ++s;
            }    
            return v;
        } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            s += 2;
            while (*s == '0' || *s == '1') {
                v = (v << 1) | (*s - '0');
                ++s;
            }        
            return v;
        }
    }

    while (isdigit((unsigned char)*s)) {
        v = (v * 10) + (*s - '0');
        ++s;
    }
    return v;
}

/* Parse and apply a single line of key=value settings */
static void settings_apply_line(char *line) {
    char* s = trim(line);
    if (*s == '#' || *s == '\0') return;
    char* eq = strchr(s, '=');
    if (!eq) return;
    *eq = '\0';
    char* key = trim(s);
    char* val = trim(eq + 1);
    if (!key || !val || !*val) return;

    uint16_t v = 0;
    if (isdigit(val[0])) {
        v = parse_number(val);   
    }

    if (strcmp(key, "background") == 0) {
        current_settings.background = (uint8_t)v;
    } else if (strcmp(key, "foreground") == 0) {
        current_settings.foreground = (uint8_t)v;
    } else if (strcmp(key, "highlight") == 0) {
        current_settings.highlight = (uint8_t)v;
    } else if (strcmp(key, "caret_default") == 0) {
        current_settings.caret_default = (uint8_t)v;
    } else if (strcmp(key, "caret_caps") == 0) {
        current_settings.caret_caps = (uint8_t)v;
    } else if (strcmp(key, "caret_graphics") == 0) {
       current_settings.caret_graphics = (uint8_t)v;
    } else if (strcmp(key, "repeat_delay") == 0) {
        current_settings.repeat_delay = (uint8_t)v;
    } else if (strcmp(key, "repeat_rate") == 0) {
        current_settings.repeat_rate = (uint8_t)v;
    } else if (strcmp(key, "blink_rate") == 0) {
        current_settings.blink_rate = (uint8_t)v;
    } else if (strcmp(key, "key_beep_cycles") == 0) {
        if (v == 1) v = 2; // avoid 1 cycle which is too short to hear
        current_settings.key_beep_cycles = (uint8_t)v;
    } else if (strcmp(key, "key_beep_period") == 0) {
        current_settings.key_beep_period = (uint16_t)v;
    } else if (strcmp(key, "font") == 0) {
        strcpy(current_settings.font, val);
    }
}

int settings_load(const char* path) {
    if (path == NULL) path = default_settings_file;
#ifdef __ZXNEXT
    /* Use ESXDOS low-level file IO */
    errno = 0;
    char fd = esxdos_f_open((char*)path, ESXDOS_MODE_R | ESXDOS_MODE_OE);
    if (errno) return -1;

    char buf[80];
    char linebuf[80];
    int linepos = 0;
    int32_t r;

    while ((r = esxdos_f_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < r; ++i) {
            char ch = buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                linebuf[linepos] = '\0';
                if (linepos) settings_apply_line(linebuf);
                linepos = 0;
            } else {
                if (linepos < (int)sizeof(linebuf) - 1) linebuf[linepos++] = ch;
            }
        }
    }
    if (linepos) {
        linebuf[linepos] = '\0';
        settings_apply_line(linebuf);
    }
    esxdos_f_close(fd);
    settings_apply();
    return 0;
#else
    FILE* f = fopen(path, "r");
    if (!f) return -1;

    char line[128];
    settings_init_defaults(&current_settings);

    while (fgets(line, sizeof(line), f)) {
        settings_apply_line(line);
    }
    fclose(f);

    settings_apply();
    return 0;
#endif
}

int settings_save(const char* path) {
    if (path == NULL) path = default_settings_file;
#ifdef __ZXNEXT
    errno = 0;
    char fd = esxdos_f_open((char*)path, ESXDOS_MODE_W | ESXDOS_MODE_CT);
    if (errno) return -1;

    char buf[80];
    int len = snprintf(buf, sizeof(buf), "# zed editor settings (key=value)\n");
    esxdos_f_write(fd, buf, len);

    len = snprintf(buf, sizeof(buf), "background=0x%02x\n", current_settings.background);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "foreground=0x%02x\n", current_settings.foreground);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "highlight=0x%02x\n", current_settings.highlight);
    esxdos_f_write(fd, buf, len);

    len = snprintf(buf, sizeof(buf), "caret_default=0x%02x\n", current_settings.caret_default);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "caret_caps=0x%02x\n", current_settings.caret_caps);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "caret_graphics=0x%02x\n", current_settings.caret_graphics);
    esxdos_f_write(fd, buf, len);

    len = snprintf(buf, sizeof(buf), "repeat_delay=%d\n", current_settings.repeat_delay);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "repeat_rate=%d\n", current_settings.repeat_rate);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "blink_rate=%d\n", current_settings.blink_rate);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "key_beep_cycles=%d\n", current_settings.key_beep_cycles);
    esxdos_f_write(fd, buf, len);
    len = snprintf(buf, sizeof(buf), "key_beep_period=%d\n", current_settings.key_beep_period);
    esxdos_f_write(fd, buf, len);

    esxdos_f_write(fd, "font=\n", 6);

    esxdos_f_close(fd);
    return 0;
#else
    FILE* f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# zed editor settings (key=value)\n");
    fprintf(f, "background=0x%02x\n", current_settings.background);
    fprintf(f, "foreground=0x%02x\n", current_settings.foreground);
    fprintf(f, "highlight=0x%02x\n", current_settings.highlight);

    fprintf(f, "caret_default=0x%02x\n", current_settings.caret_default);
    fprintf(f, "caret_caps=0x%02x\n", current_settings.caret_caps);
    fprintf(f, "caret_graphics=0x%02x\n", current_settings.caret_graphics);

    fclose(f);
    return 0;
#endif
}

void settings_apply(void) {
    crt_apply_settings(&current_settings);
    crt_load_font(current_settings.font);
}
