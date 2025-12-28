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

#define VERSION "0.5"

#define HOTKEY_ITEM_WIDTH 12
#define HOTKEY_ITEMS_PER_LINE 6

#define LINES SCREEN_HEIGHT-5
#define COLS 80

#define AUTO_SAVE_TICKS 6000 // auto save roughly every 2 minutes

#define FLAG_DIRTY 1
#define FLAG_AUTOSAVE 2

#define MASK_DIRTY (~FLAG_DIRTY)
#define MASK_AUTOSAVE (~FLAG_AUTOSAVE)

#define INVALID_LINE 0xffffffff

struct esx_cat cat;
struct esx_lfn lfn;
char *filename = &lfn.filename[0];

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
char* e_filename;             // current filename (null if no filename)
int32_t e_length;             //
int32_t e_gap_start;          // the index of the beginning of the gap (cursor position)
int32_t e_gap_end;            // one past the end of the gap
int32_t e_row_offset;         // vertical scrolling offset (first displayed line)
int32_t e_col_offset;         // horizontal scrolling offset (first displayed column)
int32_t e_top_row_index;      // top row of the window (in the displayed text)
int32_t e_cursor_row;         // current cursor row (in the gap buffer)
int32_t e_cursor_col;         // current cursor column (in the gap buffer)
int32_t e_mark_start;         // start of marked text (-1 if none)
uint16_t e_last_save_tick;    // ticks at the time of the last save
uint8_t e_dirty;              // dirty flag
uint8_t e_file_too_large;     // indicates that the file is too large (disable save)
RedrawMode e_redraw_mode;     // what to redraw 

uint32_t visibile_row_index[LINES]; // starting index of each visible row
int32_t prev_mark_start;
int32_t prev_mark_end;

int32_t prev_highlight_row;

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
CommandAction editor_cutline(void) MYCC;
CommandAction editor_find(void) MYCC;
CommandAction editor_goto(void) MYCC;
CommandAction editor_quit(void) MYCC;

Command commands[] = {
    {"^S", "Save", KEY_SAVE, editor_save},
    {"^M", "Mark", KEY_MARK, editor_mark},
    {"^C", "Copy", KEY_COPY, editor_copy},
    {"^X", "Cut", KEY_CUT, editor_cut},
    {"^V", "Paste", KEY_PASTE, editor_paste},
    {"^K", "Cut Line", KEY_CUTLINE, editor_cutline},
    {"^F", "Find", KEY_FIND, editor_find},
    {"^G", "Goto", KEY_GOTO, editor_goto},
    {"^Q", "Quit", KEY_QUIT, editor_quit},
    {NULL, NULL, 0, NULL}
};

// Forward declarations
void editor_busy(void) MYCC;
void editor_move_right(void) MYCC;

char* get_filename(char* path) MYCC {
    char* s = &path[0];
    char* p = s + strlen(path);
    while (p > s && *(p - 1) != '/' && *(p - 1) != '\\') --p;
    return p;
}

uint8_t is_whitespace(char c) MYCC {
    return c == ' ' || c == NL;
}

int32_t to_int32(const char* s, const char** p) MYCC {
    int32_t v = 0;
    while (isdigit(*s)) {
        v = (v * 10) + (*s - '0');
        ++s;
    }
    if (p) *p = s;
    return v;
}

int8_t e_busy = -50;
void editor_busy(void) MYCC {
    static uint8_t chars[] = {'|','/','-', '\\'};

    if (e_busy < 0) {
        ++e_busy;
        return;
    }
    putch_at(79, SCREEN_HEIGHT-1, chars[e_busy]);
    ++e_busy;
    if (e_busy == sizeof(chars)) e_busy = 0;
}

void editor_ready(void) {
    putch_at(79, SCREEN_HEIGHT-1, ' ');
    e_busy = -50;
}

void editor_message(const char* msg) MYCC {
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);
    set_cursor_pos(0, LINES);
    if (msg) print("%s", msg);
    clreol();
    set_cursor_pos(ox, oy);
}

/* Returns the i-th character of the text (ignoring the gap). */
char editor_get_char(int32_t index) MYCC {
    if (index < e_gap_start)
        return get_text_char(index);
    else
        return get_text_char((index - e_gap_start) + e_gap_end);
}

inline void editor_update_row_index(uint8_t row, int32_t offset) MYCC {
    for(uint8_t r=row; r<LINES; ++r) {
        if (visibile_row_index[r] == INVALID_LINE)
            break;
        visibile_row_index[r] += offset;        
    }
}

/* Returns index of the start of the line in the buffer at position 'at'*/
int32_t g_pos;
uint8_t t_uint8;

int32_t editor_find_line_start(int32_t at) MYCC {
    g_pos = at;
    if (g_pos <= 0) {
        if (get_text_char(0) == NL) return 1;
        return 0;
    }
    while (g_pos > 0 && get_text_char(g_pos) != NL)
        --g_pos;
    if (get_text_char(g_pos) == NL) ++g_pos;
    return g_pos;
}

/* Returns index of the end of the line in the buffer at position 'at'*/
int32_t editor_find_line_end(int32_t at) MYCC {
    g_pos = at;
    while (g_pos < e_length) {
        char c = editor_get_char(g_pos);
        if (c == NL)
            break;
        g_pos++;
    }
    return g_pos;
}

/* Insert a character at the current cursor position (i.e. at gap_start). */
void editor_insert(char c) MYCC {
    if (e_gap_start == e_gap_end) {
        return;  // No space to insert.
    }

    uint8_t row = (e_cursor_row - e_row_offset);
    if (visibile_row_index[row] == INVALID_LINE) {
        visibile_row_index[row] = e_gap_start;
    }
    
    if (!is_insert_mode() && c != NL && e_gap_end < text_buffer_size && get_text_char(e_gap_start) != NL) {        
        set_text_char(e_gap_end, c);
        editor_move_right();
    } else {
        set_text_char(e_gap_start, c);

        ++e_length;
        ++e_gap_start;
        if (c != NL) {
            e_cursor_col++;
        }
        else {
            e_cursor_row++;
            e_cursor_col = 0;
        }
        editor_update_row_index(row+1, 1);
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
    uint32_t pos = editor_find_line_start(e_gap_start - 1);
    uint8_t spaces = 0;
    while (pos < e_gap_start && get_text_char(pos) == ' ') {
        ++spaces;
        ++pos;
    }

    editor_insert(NL);
    // Add the indentation
    while (spaces--)
        editor_insert(' ');
    e_redraw_mode = REDRAW_ALL;
}

void editor_update_mark(void) MYCC {
    static int32_t marklen;
    if (e_mark_start == -1) return;

    marklen = abs(e_gap_start - e_mark_start);

    while (marklen > SCRATCH_BUFFER_SIZE && e_mark_start > e_gap_start) {
        --e_mark_start;
        --marklen;
    }
    while (marklen > SCRATCH_BUFFER_SIZE && e_mark_start < e_gap_start) {
        ++e_mark_start;
        --marklen;
    }
}

/* Delete the character to the left of the cursor (if any). */
void editor_backspace(void) MYCC {
    if (e_gap_start > 0) {
        char c = get_text_char(--e_gap_start);
        if (e_mark_start != -1 && e_gap_start <= e_mark_start) {
            e_mark_start--;
        }
        --e_length;
        
        e_redraw_mode = (c == NL ? REDRAW_ALL : REDRAW_LINE);
        if (c != NL) {
            e_cursor_col--;
        }
        else {
            int32_t i = e_gap_start - 1;
            e_cursor_col = 0;
            while (i >= 0 && get_text_char(i) != NL) {
                i--;
                e_cursor_col++;
            }
            e_cursor_row--;
        }

        if (e_redraw_mode != REDRAW_ALL) {
            uint8_t row = (e_cursor_row - e_row_offset);
            editor_update_row_index(row + 1, -1);
        }        
        e_dirty = FLAG_DIRTY | FLAG_AUTOSAVE;
        editor_update_mark();
    }
}

/*
 * Move the gap one character to the left.
 * (That is, move one character from before the gap to after it.)
 */
void editor_move_left(void) MYCC {
    static int32_t i;
    static char ch;

    if (e_gap_start > 0) {
        --e_gap_start;
        --e_gap_end;
        ch = get_text_char(e_gap_start);
        set_text_char(e_gap_end, ch);

        e_redraw_mode = REDRAW_CURSOR;
        if (ch != NL) {
            e_cursor_col--;
        }
        else {
            i = e_gap_start - 1;
            e_cursor_col = 0;
            while (i >= 0 && get_text_char(i) != NL) {
                i--;
                e_cursor_col++;                
            }
            e_cursor_row--;
            editor_busy();
        }

        editor_update_mark();
    }
}

/*
 * Move the gap one character to the right.
 * (That is, move one character from after the gap int32_to the gap.)
 */
void editor_move_right(void) MYCC {
    static char ch;

    if (e_gap_end < text_buffer_size) {
        ch = get_text_char(e_gap_end);
        set_text_char(e_gap_start, ch);
        
        e_redraw_mode = REDRAW_CURSOR;
        if (ch != NL) {
            e_cursor_col++;
        }
        else {
            e_cursor_col = 0;
            e_cursor_row++;
            editor_busy();
        }
        ++e_gap_start;
        ++e_gap_end;        
        editor_update_mark();
    }
}

/*
 * Reposition the gap (i.e. the cursor) to a given logical text index.
 * This is done by repeated left/right moves.
 */
void editor_move_cursor_to(int32_t pos) MYCC {
    static int32_t target;
    target = pos;
    while (e_gap_start > target) editor_move_left();
    while (e_gap_start < target) editor_move_right();
}

/*
 * Reposition the gap to the next character after a space
 */
void editor_move_word(int8_t direction) MYCC {
    int32_t pos = e_gap_start;
    
    if (direction > 0) {
        // if in the middle of a word move to the end
        while (pos < e_length && !is_whitespace(editor_get_char(pos))) {
            ++pos;
        }
        // move to the start of the next word
        while (pos < e_length && is_whitespace(editor_get_char(pos))) {
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
void editor_get_cursor_position(int32_t* row, int32_t* col) MYCC {
    *row = e_cursor_row;
    *col = e_cursor_col;
}

/*
 * Move the cursor one line up.
 * This finds the start of the previous line and positions the cursor in that line
 * at the same column as the current cursor (or the end of the line, whichever comes first).
 */
void editor_move_up(void) MYCC {
    int32_t cur_row, cur_col;
    editor_get_cursor_position(&cur_row, &cur_col);
    if (cur_row == 0)
        return;  // Already on the first line.

    int32_t current_line_start = editor_find_line_start(e_gap_start - 1);
    int32_t prev_line_start = editor_find_line_start(current_line_start - 2);

    // Compute previous line’s length.
    int32_t prev_line_length = current_line_start - 1 - prev_line_start;
    int32_t target_col = (cur_col < prev_line_length ? cur_col : prev_line_length);
    int32_t target_pos = prev_line_start + target_col; ;
    editor_move_cursor_to(target_pos);
}

/*
 * Move the cursor one line down.
 * This finds the next line and moves to the same column (or as far as is available).
 */
void editor_move_down(void) MYCC {
    int32_t cur_row, cur_col;
    editor_get_cursor_position(&cur_row, &cur_col);
    
    int32_t pos = editor_find_line_end(e_gap_start);
    if (pos >= e_length)
        return;  // No next line available.

    int32_t next_line_start = pos + 1;
    // Determine the length of the next line.
    int32_t next_line_length = 0;
    while (next_line_start + next_line_length < e_length &&
        editor_get_char(next_line_start + next_line_length) != NL) {
        next_line_length++;
    }
    int32_t target_col = (cur_col < next_line_length ? cur_col : next_line_length);
    int32_t target_pos = next_line_start + target_col;
    editor_move_cursor_to(target_pos);
}

/*
 * Update the vertical (row_offset) and horizontal (col_offset) scrolling
 * based on the current cursor position so that the cursor stays visible.
 */
void editor_update_scroll(void) MYCC {
    int32_t cursor_row, cursor_col;
    editor_get_cursor_position(&cursor_row, &cursor_col);

    int32_t old_row_offset = e_row_offset;
    int32_t old_col_offset = e_col_offset;
    // Vertical scrolling:
    if (cursor_row < e_row_offset)
        e_row_offset = cursor_row;
    else if (cursor_row >= e_row_offset + LINES - 1)
        e_row_offset = cursor_row - (LINES - 2); // -1 reserved for status line

    // Horizontal scrolling:
    if (cursor_col <= e_col_offset)
        e_col_offset = cursor_col >= 4 ? cursor_col - 4 : 0;
    else if (cursor_col >= e_col_offset + COLS)
        e_col_offset = cursor_col - COLS + 4;

    // If the offsets have changed, redraw the screen.
    if (e_row_offset != old_row_offset || e_col_offset != old_col_offset) {
        int32_t lines_to_scroll = abs(e_row_offset - old_row_offset);
        if (e_row_offset > old_row_offset) {
            while (lines_to_scroll && e_top_row_index < e_gap_start) {
                char c = editor_get_char(e_top_row_index);
                if (c == NL) {
                    --lines_to_scroll;
                }
                ++e_top_row_index;
            }
        }
        else if (e_row_offset < old_row_offset) {
            while (lines_to_scroll && e_top_row_index > 0) {
                char c = editor_get_char(e_top_row_index - 1);
                if (c == NL) {
                    --lines_to_scroll;
                }
                --e_top_row_index;
            }
            // move to start of the line
            while (e_top_row_index > 0 && editor_get_char(e_top_row_index - 1) != NL) {
                --e_top_row_index;
            }
        }

        e_redraw_mode = REDRAW_ALL;
    }
}

void update_hardware_cursor(int32_t cursor_col, int32_t cursor_row) MYCC
{
    // Place the hardware cursor in the proper on-screen position.
    int32_t screen_cursor_row = cursor_row - e_row_offset;
    int32_t screen_cursor_col = cursor_col - e_col_offset;
    if (screen_cursor_row >= 0 && screen_cursor_row < LINES - 1 &&
        screen_cursor_col >= 0 && screen_cursor_col < COLS)
        set_cursor_pos(screen_cursor_col, screen_cursor_row);
    else
        set_cursor_pos(0, LINES - 2);
}

void editor_draw_line(void) MYCC {
    static int32_t i;
    static uint32_t col;
    static uint8_t row;
    static int32_t mark_from;
    static int32_t mark_to;
    static uint8_t marking;
    static uint8_t was_marking;
    static uint8_t should_mark;

    marking = 0;
    was_marking = 0;
    should_mark = (e_mark_start != -1);
    if (should_mark){
        mark_from = e_mark_start <= e_gap_start ? e_mark_start : e_gap_start;
        mark_to = e_mark_start >= e_gap_start ? e_mark_start : e_gap_start;
    }

    gotosol();
    row = e_cursor_row - e_row_offset;
    i = visibile_row_index[row];

    set_attr(should_mark? DEFAULT_ATTR : HIGHLIGHT_ATTR);
    for (col = 0; col < COLS && i < e_length; ++col, ++i) {
        char c = editor_get_char(i);
        if (should_mark) {
            marking = (i >= mark_from && i < mark_to);
            if (marking != was_marking){
                was_marking = marking;
                if (marking) {
                    set_attr(SELECT_ATTR);
                } else {
                    should_mark = 0;
                    set_attr(DEFAULT_ATTR);
                }
            }               
        } 
        if (c == NL) break;
        putch(c);
    }
    clreol();

    e_redraw_mode = REDRAW_NONE;
}

/*
 * Draw the contents of the gap buffer on the screen.
 * We “reassemble” the text (ignoring the gap) int32_to lines. A reserved status line
 * is drawn at the bottom.
 */
void editor_draw(void) MYCC {
    static int32_t i;
    static uint8_t row;
    static uint8_t edit_row;
    static uint32_t col;
    static char c;
    static int32_t mark_from;
    static int32_t mark_to;
    static uint8_t marking;
    static uint8_t was_marking;
    static uint8_t should_mark;
    
    i = e_top_row_index;

    set_cursor_pos(0, 0);

    marking = 0;
    was_marking = 0;
    should_mark = (e_mark_start != -1);

    if (should_mark) {
        mark_from = e_mark_start <= e_gap_start ? e_mark_start : e_gap_start;
        mark_to = e_mark_start >= e_gap_start ? e_mark_start : e_gap_start;
    }

    edit_row = e_cursor_row - e_row_offset;
    set_attr(DEFAULT_ATTR);
    for (row = 0; row < LINES - 1 && i < e_length; ++row, ++i) {
        for (col = 0; col < e_col_offset && i < e_length; ++col, ++i) {
            c = editor_get_char(i);
            if (c == NL) break;
        }
        visibile_row_index[row] = i;
         
        for (col = 0; col < COLS && i < e_length; ++col, ++i) {
            c = editor_get_char(i);
            
            if (should_mark) {
                marking = (i >= mark_from && i < mark_to);
                if (marking != was_marking){
                    was_marking = marking;
                    if (marking) {
                        set_attr(SELECT_ATTR);
                    } else {
                        should_mark = 0;
                        set_attr(DEFAULT_ATTR);
                    }
                }
            }

            if (c == NL) {
                if (marking) set_attr_at(col, row, SELECT_ATTR);
                break;
            }
            
            putch(c);
        }
        while (i < e_length && (c = editor_get_char(i)) != NL) ++i;
        if (c == NL) {
            set_attr(DEFAULT_ATTR);
            clreol();            
            putch(NL);            
            if (marking) {
                set_attr_at(col, row, SELECT_ATTR);
                set_attr(SELECT_ATTR);            
            }
        }
        if (i == e_length) clreol();
    }
    visibile_row_index[row] = i;

    // Standard attribute, needed when the marker runs to the end 
    // of the file and therefore does not reset
    set_attr(DEFAULT_ATTR);

    // If we reached the end of the text, fill the rest of the screen with blank lines.
    while (row < LINES) {
        visibile_row_index[row++] = INVALID_LINE;
        clreol();
        putch(NL);
    }

    e_redraw_mode = REDRAW_NONE;
}

void editor_update_filename(void) MYCC {
    set_cursor_pos(0, SCREEN_HEIGHT - 1);
    set_attr(SELECT_ATTR);

    char offs = 0;
    if (e_filename && strlen(e_filename) > 31) {
        offs = strlen(e_filename) - 31;
    }
    const char* prefix = offs ? "..." : "";
    const char* name = e_filename ? (const char*)(e_filename + offs) : "Untitled";
    print("%s", prefix);
    print("%s", name);
    if (e_dirty) putch('*'); else putch(' ');
    set_attr(DEFAULT_ATTR);
    clreol();
}

void editor_print_hotkey(const char* short_cut_key, const char* description) MYCC {
    int32_t len = HOTKEY_ITEM_WIDTH - (strlen(short_cut_key) + strlen(description));
    set_attr(SELECT_ATTR); print(short_cut_key); set_attr(DEFAULT_ATTR);
    print(" %s", description);
    while (len-- > 0) putch(' ');
}

void editor_show_hotkeys(void) MYCC {
    set_cursor_pos(0, LINES + 1);
    int32_t i = 0;
    for (Command* cmd = &commands[0]; cmd->short_cut_key != NULL; ++cmd) {
        editor_print_hotkey(cmd->short_cut_key, cmd->description);
        if (++i % HOTKEY_ITEMS_PER_LINE == 0) putch(NL);
    }
    print("Version: %s", VERSION);
    if (e_file_too_large) editor_message("File too large, save is disabled");
}

void editor_update_status(char key) MYCC {
    static uint8_t wasdirty = 0;
    static int32_t cursor_row, cursor_col;
    static uint8_t ox, oy;

    get_cursor_pos(&ox, &oy);
    editor_get_cursor_position(&cursor_row, &cursor_col);

    if (wasdirty != e_dirty) {
        wasdirty = e_dirty;
        editor_update_filename();
    }
    set_attr(DEFAULT_ATTR);
    set_cursor_pos(40, SCREEN_HEIGHT - 1);
    print("Mem: %ld Ln %ld, Col %ld Key: %d", text_buffer_size - e_length, cursor_row + 1, cursor_col + 1, key);
    clreol();
    set_cursor_pos(ox, oy);
}

void editor_highlight_current_line(void) MYCC {
    static uint32_t row;
    static uint8_t screen_col;
    static uint8_t screen_row;

    get_cursor_pos(&screen_col, &screen_row);
    row = e_cursor_row;
    
    if (prev_highlight_row != -1 && prev_highlight_row != e_cursor_row) {
        if (prev_highlight_row >= e_row_offset && prev_highlight_row < e_row_offset + LINES - 1) {
            static uint32_t prev_screen_row;
            prev_screen_row = prev_highlight_row - e_row_offset;
            for(screen_col = 0; screen_col < COLS; ++screen_col) {
                set_attr_at(screen_col, prev_screen_row, DEFAULT_ATTR);
            }
        }
    }

    if (e_mark_start == -1) {
        for(screen_col = 0; screen_col < COLS; ++screen_col) {
            set_attr_at(screen_col, screen_row, HIGHLIGHT_ATTR);
        }
        prev_highlight_row = e_cursor_row;
    } else {
        prev_highlight_row = -1;
    }
}

void set_attr_region(int32_t from, int32_t to, uint8_t a) {
    int32_t s, e;
    int32_t row, col;
    int32_t pos;
    
    s = from;
    e = to;
    if (s > e) {
        int32_t temp = s;
        s = e;
        e = temp;
    }
    
    if (s < e_top_row_index) s = e_top_row_index;
    if (e > e_length) e = e_length;

    row = 0;
    // Find index at starting row using the visible_row_index array
    while (row < LINES - 1 && (visibile_row_index[row] != INVALID_LINE)) {
        if (s >= visibile_row_index[row] &&
            s < visibile_row_index[row + 1]) {
            break;
        }
        row++;
    }

    col = 0;
    pos = visibile_row_index[row];
    while (pos < s) {
        char c = editor_get_char(pos);
        col++;        
        pos++;
    }

    while (pos < e && row < LINES - 1) {
        char c = editor_get_char(pos);        
        set_attr_at(col, row, a);
        if (c == NL) {
            row++;
            col = 0;
            pos += e_col_offset;
        } else {
            col++;            
        }         
        pos++;       
    }
}

void editor_redraw(void) MYCC {
    editor_update_scroll();

    if (e_redraw_mode == REDRAW_ALL) {
        editor_draw();
        prev_mark_start = e_mark_start;
        prev_mark_end   = e_gap_start;
        e_redraw_mode = REDRAW_NONE;
    }

    if (e_mark_start != -1) {
        // Normalize new selection to [new_start, new_end)
        int32_t new_start = e_mark_start < e_gap_start ? e_mark_start : e_gap_start;
        int32_t new_end   = e_mark_start > e_gap_start ? e_mark_start : e_gap_start;

        if (prev_mark_start == -1) {
            // No previous selection: just mark the new one
            if (new_start != new_end) {
                set_attr_region(new_start, new_end, 0b00010000);
            }
        } else {
            // Normalize previous selection to [old_start, old_end)
            int32_t old_start = prev_mark_start < prev_mark_end ? prev_mark_start : prev_mark_end;
            int32_t old_end   = prev_mark_start > prev_mark_end ? prev_mark_start : prev_mark_end;

            // Check if we changed direction
            if ((prev_mark_start < prev_mark_end && e_mark_start > e_gap_start) ||
                (prev_mark_start > prev_mark_end && e_mark_start < e_gap_start)) {
                e_redraw_mode = REDRAW_ALL;                
            }
            else {
                // --- LEFT SIDE DIFF ---
                // If the left boundary moved right: unmark what was lost on the left
                if (old_start < new_start) {
                    set_attr_region(old_start, new_start, DEFAULT_ATTR);
                }
                // If the left boundary moved left: mark what was gained on the left
                else if (new_start < old_start) {
                    set_attr_region(new_start, old_start, SELECT_ATTR);
                }

                // --- RIGHT SIDE DIFF ---
                // If the right boundary moved left: unmark what was lost on the right
                if (old_end > new_end) {
                    set_attr_region(new_end, old_end, DEFAULT_ATTR);
                }
                // If the right boundary moved right: mark what was gained on the right
                else if (new_end > old_end) {
                    set_attr_region(old_end, new_end, SELECT_ATTR);
                }
            }
        }
        prev_mark_start = e_mark_start;
        prev_mark_end   = e_gap_start;
    }

    if (e_redraw_mode == REDRAW_ALL) {
        editor_draw();    
    }
    else if (e_redraw_mode == REDRAW_LINE) {
        editor_draw_line();
    }

    int32_t cursor_row, cursor_col;
    editor_get_cursor_position(&cursor_row, &cursor_col);
    update_hardware_cursor(cursor_col, cursor_row);

    editor_highlight_current_line();
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
    size_t i = len;
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

int32_t editor_save_file(uint8_t temp) MYCC {
    e_last_save_tick = get_ticks();

    if (e_file_too_large) return 0;

    editor_message("Saving...");

#define SAVE_BUF_SIZE 256
    static char buf[SAVE_BUF_SIZE];

#ifdef __ZXNEXT
    errno = 0;
    strcpy(tmpbuffer, e_filename);
    if (temp)
        strcat(tmpbuffer, ".bak");
    else
        strcat(tmpbuffer, ".zed");
    static char f;
    f = esxdos_f_open(tmpbuffer, ESXDOS_MODE_W | ESXDOS_MODE_CT);
    if (errno) return errno;
    
    static int32_t i;
    static uint8_t j;
    static char ch;
    i = 0;
    while (i < e_length) {
        j = 0;        
        while (j < (SAVE_BUF_SIZE>>1) && i < e_length) {
            ch = editor_get_char(i++);
            buf[j++] = ch;
            if (ch == '\r') {
                buf[j++] = '\n';            
                editor_busy();
            }
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
#else
    errno = 0;
    strcpy(tmpbuffer, e_filename);
    if (temp)
        strcat(tmpbuffer, ".bak");
    else
        strcat(tmpbuffer, ".zed");
    //char f = esxdos_f_open(tmpbuffer, ESXDOS_MODE_W | ESXDOS_MODE_CT);
    if (errno) return errno;

    int32_t total = editor_length();
    int32_t i = 0;
    while (i < total) {
        char* p = get_text_ptr(i);
        int j = 0;
        while (j < sizeof(buf) >> 1 && i < total) {
            char ch = *p++; i++;
            buf[j++] = ch;
            if (ch == '\r') buf[j++] = '\n';
        }
        //esxdos_f_write(f, buf, j);
        if (errno) {
            //esxdos_f_close(f);
            return errno;
        }
    }
   
#endif //__ZXNEXT
    editor_message(NULL);
    return 0;
}

void editor_init_file(void) MYCC {
    static int32_t i, bytes_read, total_bytes_read;
    static char *pwork;
    static char ch;

    errno = 0;
    editor_message("Loading...");
#ifdef __ZXNEXT
    char f = esxdos_f_open(filename, ESXDOS_MODE_R | ESXDOS_MODE_OE);
    if (!errno) {
        total_bytes_read = 0;

        while (total_bytes_read < text_buffer_size) {
            pwork = get_text_ptr(total_bytes_read);
            bytes_read = esxdos_f_read(f, pwork, 8192);
            if (bytes_read <= 0) break;
            total_bytes_read += bytes_read;
            editor_busy();
        }
        e_file_too_large = (total_bytes_read >= text_buffer_size);

        typedef enum {
            LINE_ENDING_NONE = -1,
            LINE_ENDING_CR = 0,
            LINE_ENDING_LF = 1,
            LINE_ENDING_CRLF = 2
        } LINE_ENDING_STYLE;

        LINE_ENDING_STYLE line_ending_style = LINE_ENDING_NONE;
        if (total_bytes_read > 0) {
            // Check the line ending style
            for (i = 0; i < total_bytes_read; ++i) {
                ch = get_text_char(i);
                if (ch == '\r') {
                    line_ending_style = LINE_ENDING_CR;
                    if (i + 1 < total_bytes_read && get_text_char(i + 1) == '\n') {
                        line_ending_style = LINE_ENDING_CRLF;                        
                    }
                    break;
                }
                else if (ch == '\n') {
                    line_ending_style = LINE_ENDING_LF;
                    break; // LF found, no need to check further
                }
            }
        
            static int32_t src, dst, bytescopied;
            src = total_bytes_read - 1;
            dst = text_buffer_size - 1;
            bytescopied = 0;
            while (src >= 0) {
                ch = get_text_char(src);
                
                if (ch == '\t') {
                    if (dst < 1) {
                        e_file_too_large = 1;
                        break;
                    }
                    set_text_char(dst--, ' ');
                    set_text_char(dst--, ' ');
                    bytescopied += 2;
                }
                else if (ch == '\n' || ch == '\r') {
                    if (line_ending_style == LINE_ENDING_CRLF) {
                        --src;
                    }
                    set_text_char(dst--, NL);
                    ++bytescopied;
                    editor_busy();
                }
                else {
                    set_text_char(dst--, ch);
                    ++bytescopied;
                }
                --src;
            }
            e_length = bytescopied;
            e_gap_start = 0;
            e_gap_end = text_buffer_size - bytescopied;
        }
        esxdos_f_close(f);        
    }
    else if (errno != 5) {
        exit(errno);
    }
#else
    FILE* f = fopen(filename, "rb");
    if (!errno) {
        int32_t total_bytes_read = 0;

        while (total_bytes_read < text_buffer_size) {
            char* pwork = get_text_ptr(total_bytes_read);
            size_t bytes_read = fread(pwork, 1, 8192, f);
            if (bytes_read <= 0) break;
            total_bytes_read += bytes_read;
        }
        e_file_too_large = (total_bytes_read >= text_buffer_size);

        typedef enum {
            LINE_ENDING_NONE = -1,
            LINE_ENDING_CR = 0,
            LINE_ENDING_LF = 1,
            LINE_ENDING_CRLF = 2
        } LINE_ENDING_STYLE;

        LINE_ENDING_STYLE line_ending_style = LINE_ENDING_NONE;
        if (total_bytes_read > 0) {
            // Check the line ending style
            for (int32_t i = 0; i < total_bytes_read; ++i) {
                char ch = get_text_char(i);
                if (ch == '\r') {
                    line_ending_style = LINE_ENDING_CR;
                    if (i + 1 < total_bytes_read && get_text_char(i + 1) == '\n') {
                        line_ending_style = LINE_ENDING_CRLF;
                        break; // CRLF found, no need to check further
                    }
                }
                else if (ch == '\n') {
                    line_ending_style = LINE_ENDING_LF;
                    break; // LF found, no need to check further
                }
            }
        
            int32_t src = total_bytes_read - 1;
            int32_t dst = text_buffer_size - 1;
            int32_t bytescopied = 0;
            while (src >= 0) {
                char ch = get_text_char(src);
                
                if (ch == '\t') {
                    if (dst - 2 < 0) {
                        e_file_too_large = 1;
                        break;
                    }
                    set_text_char(dst--, ' ');
                    set_text_char(dst--, ' ');
                    bytescopied += 2;
                }
                else if (ch == '\n' || ch == '\r') {
                    if (line_ending_style == LINE_ENDING_CRLF) {
                        --src;
                    }
                    set_text_char(dst--, NL);
                    ++bytescopied;
                }
                else {
                    set_text_char(dst--, ch);
                    ++bytescopied;
                }
                --src;
            }
            e_gap_start = 0;
            e_gap_end = text_buffer_size - bytescopied;
        }
        fclose(f);
    }
    else if (errno != 5) {
        exit(errno);
    }
#endif
    editor_message(NULL);
}

CommandAction editor_save(void) MYCC {
    if (e_file_too_large) return COMMAND_ACTION_NONE;

    e_redraw_mode = REDRAW_CURSOR;
    set_cursor_pos(0, LINES);

    char* p = get_filename(filename);
    if (!edit_line("File name", NULL, p, MAX_FILENAME_LEN))
        return COMMAND_ACTION_CANCEL;

    e_filename = &filename[0];
    int32_t status = editor_save_file(0);
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
    int32_t status = editor_save_file(1);
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
        prev_mark_start = prev_mark_end = -1;
    }
    e_redraw_mode = REDRAW_ALL;

    return COMMAND_ACTION_NONE;
}

void editor_cutcopy(uint8_t cut, int32_t start, int32_t end) MYCC {
    int32_t len = end - start;

    // Copy marked text
    for (int32_t i = 0; i < len && i < SCRATCH_BUFFER_SIZE; ++i) {
        scratch_buffer[i] = editor_get_char(start + i);
    }
    scratch_buffer[len] = '\0';

    if (cut) {
        // Cut marked text by deleting character by character from
        // the end of the marker to the begining
        editor_move_cursor_to(end);
        for (int32_t i = 0; i < len; ++i) {
            editor_backspace();
        }
    }

    e_mark_start = -1;
    prev_mark_start = prev_mark_end = -1;
    e_redraw_mode = REDRAW_ALL;
}

CommandAction editor_copy(void) MYCC {
    if (e_mark_start == -1) return COMMAND_ACTION_NONE;
    int32_t start = e_mark_start < e_gap_start ? e_mark_start : e_gap_start;
    int32_t end = e_mark_start > e_gap_start ? e_mark_start : e_gap_start;

    editor_cutcopy(0, start, end);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_cut(void) MYCC {
    if (e_mark_start == -1) return COMMAND_ACTION_NONE;
    int32_t start = e_mark_start < e_gap_start ? e_mark_start : e_gap_start;
    int32_t end = e_mark_start > e_gap_start ? e_mark_start : e_gap_start;
    editor_cutcopy(1, start, end);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_paste(void) MYCC {
    char* src = scratch_buffer;
    for (int32_t i = 0; *src && i < SCRATCH_BUFFER_SIZE; ++i) {
        editor_insert(*src++);
    }
    e_mark_start = -1;
    prev_mark_start = prev_mark_end = -1;
    e_redraw_mode = REDRAW_ALL;
    return COMMAND_ACTION_NONE;
}

CommandAction editor_cutline(void) MYCC {
    int32_t start = editor_find_line_start(e_gap_start - 1);
    int32_t end = editor_find_line_end(e_gap_start);
    if (editor_get_char(end) == NL) ++end;
    editor_cutcopy(1, start, end);
    return COMMAND_ACTION_NONE;
}

int32_t editor_search(const char* str, int32_t startidx) MYCC {
    static int32_t len;
    static int32_t i, j, start;

    len = strlen(str);
    
    start = startidx;
    if (start < 0 || start >= e_length) {
        start = 0;
    }
    for (i = start; i < e_length - len; ++i) {
        for (j = 0; j < len; ++j) {
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
        int32_t len = strlen(input);
        
        int32_t i = editor_search(input, e_gap_start);
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
    if (e_mark_start != -1) editor_move_cursor_to(e_mark_start);
    e_mark_start = -1;
    prev_mark_start = prev_mark_end = -1;
    e_redraw_mode = REDRAW_CURSOR;
    return COMMAND_ACTION_NONE;
}
static int32_t editor_line_start_index(int32_t line) MYCC {
    /* line is 1-based */
    if (line <= 1) return 0;

    int32_t target_line = line - 1; /* zero-based */
    int32_t max_visible_lines = LINES - 1;

    /* If target is inside the visible window, return it directly */
    if (visibile_row_index[0] != INVALID_LINE &&
        target_line >= e_row_offset &&
        target_line < e_row_offset + max_visible_lines) {
        int32_t row = target_line - e_row_offset;
        return visibile_row_index[row];
    }

    /* Choose best anchor: file start (line 0), current cursor row, or top row */
    int32_t anchor_line = 0;
    int32_t anchor_index = 0;

    /* Current cursor row anchor */
    if (e_cursor_row >= 0) {
        int32_t dist_cursor = abs(e_cursor_row - target_line);
        int32_t dist_top = abs(e_row_offset - target_line);
        if (dist_cursor <= dist_top && (dist_cursor <= target_line)) {
            /* compute current line start from gap */
            anchor_line = e_cursor_row;
            anchor_index = editor_find_line_start(e_gap_start - 1);
        } else {
            anchor_line = e_row_offset;
            anchor_index = e_top_row_index;
        }
    } else {
        anchor_line = e_row_offset;
        anchor_index = e_top_row_index;
    }

    /* If top-of-window is invalid (not filled), fallback to file start */
    if (anchor_index == INVALID_LINE || anchor_index < 0) {
        anchor_line = 0;
        anchor_index = 0;
    }

    /* Move forward or backward from the anchor to the target line */
    if (anchor_line <= target_line) {
        int32_t i = anchor_line;
        int32_t idx = anchor_index;
        while (i < target_line && idx < e_length) {
            int32_t end = editor_find_line_end(idx);
            if (end >= e_length) {
                /* reached EOF */
                idx = e_length;
                break;
            }
            idx = end + 1;
            ++i;
            editor_busy();
        }
        return idx;
    } else {
        int32_t i = anchor_line;
        int32_t idx = anchor_index;
        while (i > target_line && idx > 0) {
           /* idx is a line-start; idx-1 is the newline, so search from idx-2
            * (inside the previous line) to get the prior line's start. */
            if (idx >= 2) {
                idx = editor_find_line_start(idx - 2);
            } else {
                idx = 0;
            }
            --i;
            editor_busy();
        }
        return idx;
    }
}

void editor_gotoline(int32_t line, int32_t col) MYCC {
    int32_t i = editor_line_start_index(line);

    /* Clamp i to e_length to avoid passing an out-of-range index */
    if (i > e_length) i = e_length;

    int32_t c = 1;
    while (c < col && i < e_length && editor_get_char(i) != NL) {
        ++c;
        ++i;
        editor_busy();
    }
    editor_move_cursor_to(i);
}

//void editor_gotoline(int32_t line, int32_t col) MYCC {
//    static int32_t g_line, g_col, i, j, c; 
//    g_line = line;
//    g_col = col;
//
//    i = 0;
//    for (j = 1; j < g_line && i < e_length; ++j) {
//        while (i < e_length && editor_get_char(i) != NL)
//            ++i;
//        /* Only advance past the newline if we're not at EOF */
//        if (i < e_length) {
//            ++i;
//            editor_busy();
//        } else {
//            /* Reached EOF before the requested line; stop here */
//            break;
//        }
//    }
//
//    /* Clamp i to e_length to avoid passing an out-of-range index */
//    if (i > e_length) i = e_length;
//
//    c = 1;
//    while (c < g_col && i < e_length && editor_get_char(i) != NL) {
//        ++c;
//        ++i;
//    }
//    editor_move_cursor_to(i);    
//}


CommandAction editor_goto(void) MYCC {
    char input[8] = { 0 };
    set_cursor_pos(0, LINES);
    if (edit_line("Line number", "1234567890", input, sizeof(input))) {
        int32_t lineno = to_int32(input, NULL);
        if (lineno < 1) lineno = 1;
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

void edit(char* filepath, int32_t line, int32_t col) MYCC {
    /* Initial the editor; the entire space is initially the gap. */
    e_filename = NULL;
    e_length = 0;
    e_gap_start = 0;
    e_gap_end = text_buffer_size;
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
    
    prev_mark_start = prev_mark_end = -1;
    prev_highlight_row = -1;

    memset(&visibile_row_index[0], 0xff, sizeof(visibile_row_index));

    filename[0] = '\0';

    cls();
    editor_show_hotkeys();

    if (filepath) {
#ifdef __ZXNEXT
        cat.filter = ESX_CAT_FILTER_SYSTEM | ESX_CAT_FILTER_LFN;
        p3dos_copy_cstr_to_pstr(filename, filepath);
        cat.filename = filename;
        cat.cat_sz = 2;

        if (esx_dos_catalog(&cat) == 1) {
            lfn.cat = &cat;
            esx_ide_get_lfn(&lfn, &cat.cat[1]);
            char* p = get_filename(filepath);
            strcpy(p, filename);
        }        
#endif //__ZXNEXT 
        strncpy(filename, filepath, MAX_FILENAME_LEN);
        e_filename = &filename[0];
        editor_init_file();
    }

    editor_update_filename();
    editor_gotoline(line, col);
    e_redraw_mode = REDRAW_ALL;

    char ch = 0;
    while (1) {
        editor_redraw();
        editor_update_status(ch);
        if (e_dirty & FLAG_AUTOSAVE) editor_autosave();
        editor_ready();

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
                for (int32_t i = 0; i < LINES - 2; ++i)
                    editor_move_up();
                break;
            case KEY_UP:
                editor_move_up();
                break;
            case KEY_PAGEDOWN:
                for (int32_t i = 0; i < LINES - 2; ++i)
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
                        if (ch >= 32 && ch <= 143) {
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
