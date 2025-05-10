#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifdef __ZXNEXT
#include <arch/zxn/esxdos.h>
#endif
#include <errno.h>

#include "platform.h"
#include "buffers.h"
#include "crtio.h"
#include "editor.h"

#define VERSION "0.3"

#define HOTKEY_ITEM_WIDTH 12
#define HOTKEY_ITEMS_PER_LINE 6

#define LINES SCREEN_HEIGHT-5
#define COLS 80

#define TEXT_BUFFER_SIZE 24576
#define AUTO_SAVE_TICKS 6000 // auto save roughly every 2 minutes

#define FLAG_DIRTY 1
#define FLAG_AUTOSAVE 2

#define MASK_DIRTY (~FLAG_DIRTY)
#define MASK_AUTOSAVE (~FLAG_AUTOSAVE)

struct esx_cat cat;
struct esx_lfn lfn;
char *filename = &lfn.filename[0];

char text_buffer[TEXT_BUFFER_SIZE];

typedef enum RedrawMode {
    REDRAW_NONE,
    REDRAW_ALL,
    REDRAW_LINE,
    REDRAW_CURSOR,
} RedrawMode;

typedef enum CommandAction {
    COMMAND_ACTION_NONE,
    COMMAND_ACTION_QUIT,
    COMMAND_ACTION_FAILED,
    COMMAND_ACTION_CANCEL,
    COMMAND_ACTION_YES,
    COMMAND_ACTION_NO,
} CommandAction;

// Editor state
char* e_filename;         // current filename (null if no filename)
char* e_buffer;           // the edit buffer (text + gap)    
int e_gap_start;          // the index of the beginning of the gap (cursor position)
int e_gap_end;            // one past the end of the gap
int e_row_offset;         // vertical scrolling offset (first displayed line)
int e_col_offset;         // horizontal scrolling offset (first displayed column)
int e_top_row_index;      // top row of the window (in the displayed text)      
int e_cursor_row;         // current cursor row (in the gap buffer)
int e_cursor_col;         // current cursor column (in the gap buffer)
int e_mark_start;         // start of marked text (-1 if none)
uint16_t e_last_save_tick;// ticks at the time of the last save
uint8_t e_dirty;          // dirty flag
uint8_t e_file_too_large; // indicates that the file is too large (disable save)
RedrawMode e_redraw_mode; // what to redraw    

typedef struct {
    const char* short_cut_key;
    const char* description;
    char key;
    CommandAction(*action)(void) MYCC;
} Command;

CommandAction editor_save(void) MYCC;
CommandAction editor_mark(void) MYCC;
CommandAction editor_copy(void) MYCC;
CommandAction editor_cut(void) MYCC;
CommandAction editor_paste(void) MYCC;
CommandAction editor_find(void) MYCC;
CommandAction editor_goto(void) MYCC;
CommandAction editor_quit(void) MYCC;

Command commands[] = {
    {"^S", "Save", KEY_SAVE, editor_save},
    {"^M", "Mark", KEY_MARK, editor_mark},
    {"^C", "Copy", KEY_COPY, editor_copy},
    {"^X", "Cut", KEY_CUT, editor_cut},
    {"^V", "Paste", KEY_PASTE, editor_paste},
    {"^F", "Find", KEY_FIND, editor_find},
    {"^G", "Goto", KEY_GOTO, editor_goto},
    {"^Q", "Quit", KEY_QUIT, editor_quit},
    {NULL, NULL, 0, NULL}
};

uint8_t is_whitespace(char c) MYCC {
    return c == ' ' || c == NL;
}

uint16_t to_uint16(const char* s, char** p) MYCC {
    uint16_t v = 0;
    while (isdigit(*s)) {
        v = (v * 10) + (*s - '0');
        ++s;
    }
    if (p) *p = s;
    return v;
}

/* Returns the logical length (number of characters) of the text. */
int editor_length(void) MYCC {
    return e_gap_start + (TEXT_BUFFER_SIZE - e_gap_end);
}

/* Returns the i-th character of the text (ignoring the gap). */
char editor_get_char(int index) MYCC {
    if (index < e_gap_start)
        return e_buffer[index];
    else
        return e_buffer[(index - e_gap_start) + e_gap_end];
}

/* Returns index of the start of the line in the buffer at position 'at'*/
uint16_t editor_find_line_start(int16_t at) MYCC {
    int16_t pos = at < 0 ? 0 : at;
    while (pos > 0 && e_buffer[pos] != NL)
        --pos;
    if (e_buffer[pos] == NL) ++pos;
    return pos;
}

/* Insert a character at the current cursor position (i.e. at gap_start). */
void editor_insert(char c) MYCC {
    if (e_gap_start == e_gap_end) {
        return;  // No space to insert.
    }
    e_buffer[e_gap_start] = c;
    ++e_gap_start;
    if (c != NL) {
        e_cursor_col++;
    }
    else {
        e_cursor_row++;
        e_cursor_col = 0;
    }
    e_dirty = FLAG_DIRTY | FLAG_AUTOSAVE;
    e_redraw_mode = REDRAW_LINE;
}

/* Insert 2 spaces the current cursor position. */
void editor_insert_tab(void) MYCC {
    uint8_t spaces = e_cursor_col % 2;
    if (spaces == 0) spaces = 2;
    while (spaces--)
        editor_insert(' ');
}

/* Insert newline, matching the current lines indent. */
void editor_insert_newline(void) MYCC {
    uint16_t pos = editor_find_line_start(e_gap_start - 1);
    uint8_t spaces = 0;
    while (pos < e_gap_start && e_buffer[pos] == ' ') {
        ++spaces;
        ++pos;
    }

    editor_insert(NL);
    // Add the indentation
    while (spaces--)
        editor_insert(' ');
    e_redraw_mode = REDRAW_ALL;
}

/* Delete the character to the left of the cursor (if any). */
void editor_backspace(void) MYCC {
    if (e_gap_start > 0) {
        char c = e_buffer[--e_gap_start];
        e_redraw_mode = (c == NL ? REDRAW_ALL : REDRAW_LINE);
        if (c != NL) {
            e_cursor_col--;
        }
        else {
            int i = e_gap_start - 1;
            e_cursor_col = 0;
            while (i >= 0 && e_buffer[i] != NL) {
                i--;
                e_cursor_col++;
            }
            e_cursor_row--;
        }
        e_dirty = FLAG_DIRTY | FLAG_AUTOSAVE;
    }
}

void editor_update_mark(void) MYCC {
    if (e_mark_start == -1) return;
    int marklen = abs(e_gap_start - e_mark_start);

    while (marklen > SCRATCH_BUFFER_SIZE && e_mark_start > e_gap_start) {
        --e_mark_start;
        --marklen;
    }
    while (marklen > SCRATCH_BUFFER_SIZE && e_mark_start < e_gap_start) {
        ++e_mark_start;
        --marklen;
    }
}

/*
 * Move the gap one character to the left.
 * (That is, move one character from before the gap to after it.)
 */
void editor_move_left(void) MYCC {
    if (e_gap_start > 0) {
        --e_gap_start;
        --e_gap_end;
        e_buffer[e_gap_end] = e_buffer[e_gap_start];

        e_redraw_mode = REDRAW_CURSOR;
        if (e_buffer[e_gap_start] != NL) {
            e_cursor_col--;
        }
        else {
            int i = e_gap_start - 1;
            e_cursor_col = 0;
            while (i >= 0 && e_buffer[i] != NL) {
                i--;
                e_cursor_col++;
            }
            e_cursor_row--;
        }

        if (e_col_offset > 0) {
            uint8_t cx, cy;
            get_cursor_pos(&cx, &cy);
            if (cx < 3) {
                e_col_offset--;
                e_redraw_mode = REDRAW_ALL;
            }
        }
        editor_update_mark();
    }
}

/*
 * Move the gap one character to the right.
 * (That is, move one character from after the gap into the gap.)
 */
void editor_move_right(void) MYCC {
    if (e_gap_end < TEXT_BUFFER_SIZE) {
        e_buffer[e_gap_start] = e_buffer[e_gap_end];
        e_gap_start++;
        e_gap_end++;

        e_redraw_mode = REDRAW_CURSOR;
        if (e_buffer[e_gap_start - 1] != NL) {
            e_cursor_col++;
        }
        else {
            e_cursor_col = 0;
            e_cursor_row++;
        }

        if (e_col_offset > 0) {
            uint8_t cx, cy;
            get_cursor_pos(&cx, &cy);
            if (cx > COLS - 3) {
                e_col_offset++;
                e_redraw_mode = REDRAW_ALL;
            }
        }
        editor_update_mark();
    }
}

/*
 * Reposition the gap (i.e. the cursor) to a given logical text index.
 * This is done by repeated left/right moves.
 */
void editor_move_cursor_to(int pos) MYCC {
    while (e_gap_start > pos) editor_move_left();
    while (e_gap_start < pos) editor_move_right();
}

/*
 * Reposition the gap to the next character after a space
 */
void editor_move_word(int8_t direction) MYCC {
    int pos = e_gap_start;
    int len = editor_length();

    if (direction > 0) {
        // if in the middle of a word move to the end
        while (pos < len && !is_whitespace(editor_get_char(pos))) {
            ++pos;
        }
        // move to the start of the next word
        while (pos < len && is_whitespace(editor_get_char(pos))) {
            ++pos;
        }
    }
    else if (direction < 0 && pos > 0) {
        // If left character is not a space move left until we hit a space
        // at the begining of the current word
        if (pos > 0 && !is_whitespace(editor_get_char(pos - 1))) {
            while (pos > 0 && !is_whitespace(editor_get_char(pos - 1))) {
                --pos;
            }
        }
        else {
            // If we are in a white space region move left past the white space
            while (pos > 0 && is_whitespace(editor_get_char(pos - 1))) {
                --pos;
            }
            // then move left through the previous work
            while (pos > 0 && !is_whitespace(editor_get_char(pos - 1))) {
                --pos;
            }
        }
    }

    // Finally updat the cursor to the new possition
    editor_move_cursor_to(pos);
}

/*
 * Compute the cursor's current (row, col) in the text by scanning
 * the characters up to gap_start. (This ignores the text after the gap.)
 */
void editor_get_cursor_position(int* row, int* col) MYCC {
    *row = e_cursor_row;
    *col = e_cursor_col;
}

/*
 * Move the cursor one line up.
 * This finds the start of the previous line and positions the cursor in that line
 * at the same column as the current cursor (or the end of the line, whichever comes first).
 */
void editor_move_up(void) MYCC {
    int cur_row, cur_col;
    editor_get_cursor_position(&cur_row, &cur_col);
    if (cur_row == 0)
        return;  // Already on the first line.

    int16_t current_line_start = editor_find_line_start(e_gap_start - 1);
    int16_t prev_line_start = editor_find_line_start(current_line_start - 2);

    // Compute previous line’s length.
    int prev_line_length = current_line_start - 1 - prev_line_start;
    int target_col = (cur_col < prev_line_length ? cur_col : prev_line_length);
    int target_pos = prev_line_start + target_col; ;
    editor_move_cursor_to(target_pos);
}

/*
 * Move the cursor one line down.
 * This finds the next line and moves to the same column (or as far as is available).
 */
void editor_move_down(void) MYCC {
    int cur_row, cur_col;
    editor_get_cursor_position(&cur_row, &cur_col);
    int total = editor_length();

    // Find the end of the current line.
    int pos = e_gap_start;
    while (pos < total) {
        char c = editor_get_char(pos);
        if (c == NL)
            break;
        pos++;
    }
    if (pos >= total)
        return;  // No next line available.

    int next_line_start = pos + 1;
    // Determine the length of the next line.
    int next_line_length = 0;
    while (next_line_start + next_line_length < total &&
        editor_get_char(next_line_start + next_line_length) != NL) {
        next_line_length++;
    }
    int target_col = (cur_col < next_line_length ? cur_col : next_line_length);
    int target_pos = next_line_start + target_col;
    editor_move_cursor_to(target_pos);
}

/*
 * Update the vertical (row_offset) and horizontal (col_offset) scrolling
 * based on the current cursor position so that the cursor stays visible.
 */
void editor_update_scroll(void) MYCC {
    int cursor_row, cursor_col;
    editor_get_cursor_position(&cursor_row, &cursor_col);

    int old_row_offset = e_row_offset;
    int old_col_offset = e_col_offset;
    // Vertical scrolling:
    if (cursor_row < e_row_offset)
        e_row_offset = cursor_row;
    else if (cursor_row >= e_row_offset + LINES - 1)
        e_row_offset = cursor_row - (LINES - 2); // -1 reserved for status line

    // Horizontal scrolling:
    if (cursor_col < e_col_offset)
        e_col_offset = cursor_col;
    else if (cursor_col >= e_col_offset + COLS)
        e_col_offset = cursor_col - COLS + 1;

    // If the offsets have changed, redraw the screen.
    if (e_row_offset != old_row_offset || e_col_offset != old_col_offset) {
        int lines_to_scroll = abs(e_row_offset - old_row_offset);
        if (e_row_offset > old_row_offset) {
            while (lines_to_scroll && e_top_row_index < e_gap_start) {
                char c = e_buffer[e_top_row_index];
                if (c == NL) {
                    --lines_to_scroll;
                }
                ++e_top_row_index;
            }
        }
        else if (e_row_offset < old_row_offset) {
            while (lines_to_scroll && e_top_row_index > 0) {
                char c = e_buffer[e_top_row_index - 1];
                if (c == NL) {
                    --lines_to_scroll;
                }
                --e_top_row_index;
            }
            // move to start of the line
            while (e_top_row_index > 0 && e_buffer[e_top_row_index - 1] != NL) {
                --e_top_row_index;
            }
        }

        e_redraw_mode = REDRAW_ALL;
    }
}

void update_hardware_cursor(int cursor_col, int cursor_row) MYCC 
{
    // Place the hardware cursor in the proper on-screen position.
    int screen_cursor_row = cursor_row - e_row_offset;
    int screen_cursor_col = cursor_col - e_col_offset;
    if (screen_cursor_row >= 0 && screen_cursor_row < LINES - 1 &&
        screen_cursor_col >= 0 && screen_cursor_col < COLS)
        set_cursor_pos(screen_cursor_col, screen_cursor_row);
    else
        set_cursor_pos(0, LINES - 2);
}

void editor_draw_line(void) MYCC {
    int i = e_gap_start;
    while (i > 0 && e_buffer[i - 1] != NL)
        i--;

    uint8_t cx, cy; // cursor position
    get_cursor_pos(&cx, &cy);
    (cx);

    int total = editor_length();
    set_cursor_pos(0, cy);
    int col_offset = e_col_offset;
    for (int col = 0; col < COLS + col_offset && i < total; ++col, ++i) {
        char c = editor_get_char(i);
        if (c == NL) break;
        if (col >= col_offset) putch(c);
    }
    clreol();

    int cursor_row, cursor_col;
    editor_get_cursor_position(&cursor_row, &cursor_col);
    update_hardware_cursor(cursor_col, cursor_row);
    e_redraw_mode = REDRAW_NONE;
}

/*
 * Draw the contents of the gap buffer on the screen.
 * We “reassemble” the text (ignoring the gap) into lines. A reserved status line
 * is drawn at the bottom.
 */
void editor_draw(void) MYCC {
    int total = editor_length();
    int i = e_top_row_index;

    set_cursor_pos(0, 0);
    int row;
    int col_offset = e_col_offset;

    if (e_mark_start != -1) editor_update_mark();

    standard();
    for (row = 0; row < LINES - 1 && i < total; ++row, ++i) {
        for (int col = 0; i < total; ++col, ++i) {
            char c = editor_get_char(i);
            if (c == NL) {
                clreol();
                putch(NL);
                break;
            }

            if (col >= col_offset && col < COLS + col_offset) {
                if (e_mark_start != -1) {
                    if ((e_mark_start < e_gap_start && i >= e_mark_start && i < e_gap_start) ||
                        (e_mark_start > e_gap_start && i >= e_gap_start && i < e_mark_start))
                        highlight();
                    else
                        standard();
                }
                putch(c);
            }
        }
        if (i == total) clreol();
    }
    // Standard attribute, needed when the marker runs to the end 
    // of the file and therefore does not reset
    standard();

    // If we reached the end of the text, fill the rest of the screen with blank lines.
    while (row++ < LINES) {
        clreol();
        putch(NL);
    }

    int cursor_row, cursor_col;
    editor_get_cursor_position(&cursor_row, &cursor_col);
    update_hardware_cursor(cursor_col, cursor_row);
    e_redraw_mode = REDRAW_NONE;
}

void editor_message(const char* msg) MYCC {
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);
    set_cursor_pos(0, LINES);
    if (msg) print("%s", msg);
    clreol();
    set_cursor_pos(ox, oy);
}

void editor_update_filename(void) MYCC {
    set_cursor_pos(0, SCREEN_HEIGHT - 1);
    highlight();
    print("Filename: %s%c", e_filename ? e_filename : "Untitled", e_dirty ? '*' : ' ');
    standard();
    clreol();
}

void editor_print_hotkey(const char* short_cut_key, const char* description) MYCC {
    int len = HOTKEY_ITEM_WIDTH - (strlen(short_cut_key) + strlen(description));
    highlight(); print(short_cut_key); standard();
    print(" %s", description);
    while (len-- > 0) putch(' ');
}

void editor_show_hotkeys(void) MYCC {
    set_cursor_pos(0, LINES + 1);
    int i = 0;
    for (Command* cmd = &commands[0]; cmd->short_cut_key != NULL; ++cmd) {
        editor_print_hotkey(cmd->short_cut_key, cmd->description);
        if (++i % HOTKEY_ITEMS_PER_LINE == 0) putch(NL);
    }
    print("Version: %s", VERSION);
    if (e_file_too_large) editor_message("File too large, save is disabled");
}

void editor_update_status(char key) MYCC {
    static uint8_t wasdirty = 0;
    int total = editor_length();

    int cursor_row, cursor_col;
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);
    editor_get_cursor_position(&cursor_row, &cursor_col);

    if (wasdirty != e_dirty) {
        wasdirty = e_dirty;
        editor_update_filename();
    }
    set_cursor_pos(45, SCREEN_HEIGHT - 1);
    print("Mem: %d Ln %d, Col %d Key: %d", TEXT_BUFFER_SIZE - total, cursor_row + 1, cursor_col + 1, key);
    clreol();
    set_cursor_pos(ox, oy);
}

void editor_redraw(void) MYCC {
    editor_update_scroll();

    if (e_mark_start != -1) {
        e_redraw_mode = REDRAW_ALL;
    }

    if (e_redraw_mode == REDRAW_ALL) {
        editor_draw();
    }
    else if (e_redraw_mode == REDRAW_LINE) {
        editor_draw_line();
    }
    else if (e_redraw_mode == REDRAW_CURSOR) {
        int cursor_row, cursor_col;
        editor_get_cursor_position(&cursor_row, &cursor_col);
        update_hardware_cursor(cursor_col, cursor_row);
    }
}

CommandAction confirm(const char* prompt) MYCC {
    CommandAction retval;

    set_cursor_pos(0, LINES);
    print("%s (y/n) ", prompt);
    char ch = getch();

    switch (ch) {
        case 'Y':
        case 'y': retval = COMMAND_ACTION_YES; break;
        case KEY_ESC: retval = COMMAND_ACTION_CANCEL; break;
        default: retval = COMMAND_ACTION_NO; break;
    }
    set_cursor_pos(0, LINES);
    clreol();
    return retval;
}

// Edit a line of text restricting to the given alphabet.
uint8_t edit_line(const char* prompt, const char* alphabet, char* buffer, uint8_t maxlen) MYCC {
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);

    uint8_t ex, ey;
    print("%s:", prompt);
    get_cursor_pos(&ex, &ey);

    size_t len = strlen(buffer);
    uint8_t i = len;
    uint8_t redraw = 1;
    uint8_t retval = 255;

    while (retval == 255) {
        if (redraw) {
            set_cursor_pos(ex, ey);
            print(buffer);
            clreol();
            redraw = 0;
        }
        set_cursor_pos(ex + i, ey);

        char ch = getch();
        switch (ch) {
            case KEY_ESC:
                retval = 0;
                break;
            case KEY_BACKSPACE:
                if (i > 0) {
                    --i;
                    memmove(&buffer[i], &buffer[i + 1], len - i);
                    --len;
                }
                redraw = 1;
                break;
            case KEY_LEFT:
                if (i > 0) --i;
                break;
            case KEY_RIGHT:
                if (i < len) ++i;
                break;
            case KEY_ENTER:
                retval = 1;
                break;
            default:
                if (len < maxlen && ch >= 32 && ch <= 128) {
                    if (!alphabet || strchr(alphabet, ch)) {
                        memmove(buffer + i + 1, buffer + i, len - i);
                        buffer[i++] = ch;
                        if (i > len) buffer[i] = '\0';
                        ++len;
                        redraw = 1;
                    }
                }
                break;
        }
    }
    set_cursor_pos(ox, oy);
    clreol();
    return retval;
}

int editor_save_file(uint8_t temp) MYCC {
    e_last_save_tick = get_ticks();

    if (e_file_too_large) return 0;

    editor_message("Saving...");

    char buf[32];
#ifdef __ZXNEXT
    errno = 0;
    strcpy(tmpbuffer, e_filename);
    if (temp)
        strcat(tmpbuffer, ".bak");
    else
        strcat(tmpbuffer, ".zed");
    char f = esxdos_f_open(tmpbuffer, ESXDOS_MODE_W | ESXDOS_MODE_CT);
    if (errno) return errno;

    int total = editor_length();
    int i = 0;
    while (i < total) {
        int j = 0;
        while (j < sizeof(buf) >> 1 && i < total) {
            char ch = editor_get_char(i++);
            buf[j++] = ch;
            if (ch == '\r') buf[j++] = '\n';
        }
        esxdos_f_write(f, buf, j);
        if (errno) {
            esxdos_f_close(f);
            return errno;
        }
    }
    esxdos_f_close(f);
    if (!temp) {
        esx_f_unlink(e_filename);
        if (esx_f_rename(tmpbuffer, e_filename)) return errno;
        esx_f_unlink(tmpbuffer);
    }
#endif //__ZXNEXT
    editor_message(NULL);
    return 0;
}

void editor_init_file(void) MYCC {
#ifdef __ZXNEXT
    errno = 0;
    char f = esxdos_f_open(filename, ESXDOS_MODE_R | ESXDOS_MODE_OE);
    if (!errno) {
        size_t bytes_read = esxdos_f_read(f, e_buffer, TEXT_BUFFER_SIZE);

        e_file_too_large = (bytes_read == TEXT_BUFFER_SIZE);
        if (bytes_read > 0) {
            char* start = &e_buffer[0];

            char* src = (char*)(start + bytes_read - 1);
            char* dst = (char*)(start + TEXT_BUFFER_SIZE - 1);
            uint16_t bytescopied = 0;
            char skip_eol_char = 0;
            char lead_eol_char = 0;
            while (src >= start) {
                char ch = *src;
                if (!skip_eol_char) {
                    switch (ch) {
                        case '\r':
                            lead_eol_char = '\r';
                            skip_eol_char = '\n';
                            break;
                        case '\n':
                            lead_eol_char = '\n';
                            skip_eol_char = '\r';
                            break;
                    }
                }
                if (ch == '\t') {
                    if (dst - 2 < start) {
                        e_file_too_large = 1;
                        break;
                    }
                    *dst-- = ' ';
                    *dst-- = ' ';
                    bytescopied += 2;
                }
                else if (ch != skip_eol_char) {
                    if (dst - 1 < start) {
                        e_file_too_large = 1;
                        break;
                    }
                    if (lead_eol_char && ch == lead_eol_char) ch = NL;
                    *dst-- = ch;
                    ++bytescopied;
                }
                --src;
            }
            e_gap_start = 0;
            e_gap_end = TEXT_BUFFER_SIZE - bytescopied;
        }
        esxdos_f_close(f);
    }
#endif
}

CommandAction editor_save(void) MYCC {
    if (e_file_too_large) return COMMAND_ACTION_NONE;

    e_redraw_mode = REDRAW_CURSOR;
    set_cursor_pos(0, LINES);

    if (!edit_line("File name", NULL, filename, MAX_FILENAME_LEN))
        return COMMAND_ACTION_CANCEL;

    e_filename = &filename[0];
    int status = editor_save_file(0);
    if (status) {
#ifdef __ZXNEXT
        esx_m_geterr(status, tmpbuffer);
        editor_message(tmpbuffer);
#endif
        return COMMAND_ACTION_FAILED;
    }

    e_dirty = 0;
    editor_update_filename();
    return COMMAND_ACTION_NONE;
}

void editor_autosave(void) MYCC {
    if (!e_filename || (get_ticks() - e_last_save_tick) < AUTO_SAVE_TICKS) return;

    // reset autosave flag and save backup
    e_dirty &= MASK_AUTOSAVE;
    int status = editor_save_file(1);
#ifdef __ZXNEXT
    if (status) {
        esx_m_geterr(status, tmpbuffer);
        editor_message(tmpbuffer);
    }
#endif
}

CommandAction editor_mark(void) MYCC {
    if (e_mark_start == -1)
        e_mark_start = e_gap_start;
    else {
        e_mark_start = -1;
        e_redraw_mode = REDRAW_ALL;
    }
    return COMMAND_ACTION_NONE;
}

void editor_cutcopy(uint8_t cut) MYCC {
    if (e_mark_start == -1) return;

    int start = e_mark_start < e_gap_start ? e_mark_start : e_gap_start;
    int end = e_mark_start > e_gap_start ? e_mark_start : e_gap_start;
    int len = end - start;

    // Copy marked text
    for (int i = 0; i < len && i < SCRATCH_BUFFER_SIZE; ++i) {
        scratch_buffer[i] = editor_get_char(start + i);
    }
    scratch_buffer[len] = '\0';

    if (cut) {
        // Cut marked text by deleting character by character from
        // the end of the marker to the begining
        editor_move_cursor_to(end);
        for (int i = 0; i < len; ++i) {
            editor_backspace();
        }
    }

    e_mark_start = -1;
    e_redraw_mode = REDRAW_ALL;
}

CommandAction editor_copy(void) MYCC {
    editor_cutcopy(0);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_cut(void) MYCC {
    editor_cutcopy(1);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_paste(void) MYCC {
    char* src = scratch_buffer;
    for (int i = 0; *src && i < SCRATCH_BUFFER_SIZE; ++i) {
        editor_insert(*src++);
    }
    e_mark_start = -1;
    e_redraw_mode = REDRAW_ALL;
    return COMMAND_ACTION_NONE;
}

int editor_search(const char* str, int start) MYCC {
    int len = strlen(str);
    int total = editor_length();
    if (start < 0 || start >= total) {
        start = 0;
    }
    for (int i = start; i < total - len; ++i) {
        for (int j = 0; j < len; ++j) {
            if (editor_get_char(i + j) != str[j]) {
                break;
            }
            if (j == len - 1) {
                return i;
            }
        }
    }
    return -1;
}

CommandAction editor_find(void) MYCC {
    static char input[32] = { 0 };
    set_cursor_pos(0, LINES);
    while (edit_line("Find", NULL, input, sizeof(input))) {
        int len = strlen(input);
        int total = editor_length();

        int i = editor_search(input, e_gap_start);
        if (i == -1) i = editor_search(input, 0);

        if (i != -1) {
            editor_move_cursor_to(i + len);
            e_mark_start = i;
            e_redraw_mode = REDRAW_ALL;
            editor_redraw();
            editor_update_status(0);
        }
        set_cursor_pos(0, LINES);
    }
    e_mark_start = -1;
    e_redraw_mode = REDRAW_CURSOR;
    return COMMAND_ACTION_NONE;
}

void editor_gotoline(uint16_t line, uint16_t col) MYCC {
    int total = editor_length();
    int i = 0;
    for (int j = 1; j < line && i < total; ++j) {
        while (i < total && editor_get_char(i) != NL)
            ++i;
        ++i;
    }
    int c = 1;
    while (c < col && i < total && editor_get_char(i) != NL) {
        ++c;
        ++i;
    }
    if (i < total) editor_move_cursor_to(i);
}

CommandAction editor_goto(void) MYCC {
    char input[8] = { 0 };
    set_cursor_pos(0, LINES);
    if (edit_line("Line number", "1234567890", input, sizeof(input))) {
        int lineno = to_uint16(input, NULL);
        editor_gotoline(lineno, 0);
    }
    return COMMAND_ACTION_NONE;
}

CommandAction editor_quit(void) MYCC {
    e_redraw_mode = REDRAW_CURSOR;
    if (e_dirty && !e_file_too_large) {
        CommandAction action = confirm("File modified. Save?");
        switch (action) {
            case COMMAND_ACTION_YES:
                if (editor_save() != COMMAND_ACTION_NONE) {
                    return COMMAND_ACTION_NONE;
                }
                break;
            case COMMAND_ACTION_CANCEL:
                return COMMAND_ACTION_NONE;
        }
        return COMMAND_ACTION_QUIT;
    }
    else if (confirm("Quit?") == COMMAND_ACTION_YES) {
        return COMMAND_ACTION_QUIT;
    }

    return COMMAND_ACTION_NONE;
}

void edit(const char* filepath, uint16_t line, uint16_t col) MYCC {
    /* Initial the editor; the entire space is initially the gap. */
    e_buffer = &text_buffer[0];
    e_filename = NULL;
    e_gap_start = 0;
    e_gap_end = TEXT_BUFFER_SIZE;
    e_row_offset = 0;
    e_col_offset = 0;
    e_top_row_index = 0;
    e_cursor_row = 0;
    e_cursor_col = 0;
    e_last_save_tick = get_ticks();
    e_dirty = 0;
    e_mark_start = -1;
    e_file_too_large = 0;
    e_redraw_mode = REDRAW_ALL;

    filename[0] = '\0';

    if (filepath) {
        strncpy(filename, filepath, MAX_FILENAME_LEN);

        cat.filter = ESX_CAT_FILTER_SYSTEM | ESX_CAT_FILTER_LFN;
        cat.filename = p3dos_cstr_to_pstr(filename);
        cat.cat_sz = 2;
        
        if (esx_dos_catalog(&cat) == 1) {
            lfn.cat = &cat;
            esx_ide_get_lfn(&lfn, &cat.cat[1]);
        } else {
            strncpy(filename, filepath, MAX_FILENAME_LEN);
        }
        
        e_filename = &filename[0];
        editor_init_file();
    }

    char ch = 0;
    cls();
    editor_show_hotkeys();
    editor_update_filename();
    editor_gotoline(line, col);
    e_redraw_mode = REDRAW_ALL;
    while (1) {
        editor_redraw();
        editor_update_status(ch);
        if (e_dirty & FLAG_AUTOSAVE) editor_autosave();

        ch = getch();
        switch (ch) {
            case KEY_WORDLEFT:
                editor_move_word(-1);
                break;
            case KEY_WORDRIGHT:
                editor_move_word(1);
                break;
            case KEY_LEFT:
                editor_move_left();
                break;
            case KEY_RIGHT:
                editor_move_right();
                break;
            case KEY_PAGEUP:
                for (int i = 0; i < LINES - 2; ++i)
                    editor_move_up();
                break;
            case KEY_UP:
                editor_move_up();
                break;
            case KEY_PAGEDOWN:
                for (int i = 0; i < LINES - 2; ++i)
                    editor_move_down();
                break;
            case KEY_DOWN:
                editor_move_down();
                break;
            default:
                switch (ch) {
                    case KEY_BACKSPACE:
                        editor_backspace();
                        break;
                    case KEY_TAB:
                        editor_insert_tab();
                        break;
                    case KEY_ENTER:
                        editor_insert_newline();
                        break;
                    default:
                        if (ch >= 32 && ch <= 127) {
                            editor_insert((char)ch);
                        }
                        else {
                            for (Command* cmd = (Command*)commands; cmd->short_cut_key != NULL; ++cmd) {
                                if (ch == cmd->key) {
                                    if (cmd->action) {
                                        CommandAction action = cmd->action();
                                        switch (action) {
                                            case COMMAND_ACTION_QUIT:
                                                return;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        break;
                }
        }
    }
}

