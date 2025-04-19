#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef __ZXNEXT
#include <arch/zxn/esxdos.h>
#endif
#include <errno.h>

#include "buffers.h"
#include "crtio.h"
#include "editor.h"

#define VERSION "0.1e"

#define HOTKEY_ITEM_WIDTH 12
#define HOTKEY_ITEMS_PER_LINE 6

#define LINES SCREEN_HEIGHT-5
#define COLS 80

#define TEXT_BUFFER_SIZE 22528

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

typedef struct {
    char* filename;         // current filename (null if no filename)
    char* buffer;           // the edit buffer (text + gap)    
    int gap_start;          // the index of the beginning of the gap (cursor position)
    int gap_end;            // one past the end of the gap
    int row_offset;         // vertical scrolling offset (first displayed line)
    int col_offset;         // horizontal scrolling offset (first displayed column)
    int top_row_index;      // top row of the window (in the displayed text)      
    int cursor_row;         // current cursor row (in the gap buffer)
    int cursor_col;         // current cursor column (in the gap buffer)
    int mark_start;         // start of marked text (-1 if none)
    uint8_t dirty;          // dirty flag
    uint8_t file_too_large; // indicates that the file is too large (disable save)
    RedrawMode redraw_mode; // what to redraw    
} EditorState;

typedef struct {
    const char* short_cut_key;
    const char* description;
    char key;
    CommandAction(*action)(EditorState* editor);
} Command;

CommandAction editor_save(EditorState* editor);
CommandAction editor_mark(EditorState* editor);
CommandAction editor_copy(EditorState* editor);
CommandAction editor_cut(EditorState* editor);
CommandAction editor_paste(EditorState* editor);
CommandAction editor_find(EditorState* editor);
CommandAction editor_goto(EditorState* editor);
CommandAction editor_quit(EditorState* editor);

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

uint8_t is_whitespace(char c) {
    return c == ' ' || c == NL;
}

/* Returns the logical length (number of characters) of the text. */
int editor_length(EditorState* editor) {
    return editor->gap_start + (TEXT_BUFFER_SIZE - editor->gap_end);
}

/* Returns the i-th character of the text (ignoring the gap). */
char editor_get_char(EditorState* editor, int index) {
    if (index < editor->gap_start)
        return editor->buffer[index];
    else
        return editor->buffer[(index - editor->gap_start) + editor->gap_end];
}

/* Insert a character at the current cursor position (i.e. at gap_start). */
void editor_insert(EditorState* editor, char c) {
    if (editor->gap_start == editor->gap_end) {
        return;  // No space to insert.
    }
    editor->buffer[editor->gap_start] = c;
    ++editor->gap_start;
    editor->redraw_mode = (c == NL ? REDRAW_ALL : REDRAW_LINE);
    if (c != NL) {
        editor->cursor_col++;
    }
    else {
        editor->cursor_row++;
        editor->cursor_col = 0;
    }
    editor->dirty = 1;
}

/* Insert 2 spaces the current cursor position. */
void editor_insert_tab(EditorState* editor) {
    uint8_t spaces = editor->cursor_col % 2;
    if (spaces == 0) spaces = 2;
    while (spaces--)
        editor_insert(editor, '\t');
}

/* Delete the character to the left of the cursor (if any). */
void editor_backspace(EditorState* editor) {
    if (editor->gap_start > 0) {
        char c = editor->buffer[--editor->gap_start];
        editor->redraw_mode = (c == NL ? REDRAW_ALL : REDRAW_LINE);
        if (c != NL) {
            editor->cursor_col--;
        }
        else {
            int i = editor->gap_start - 1;
            editor->cursor_col = 0;
            while (i >= 0 && editor->buffer[i] != NL) {
                i--;
                editor->cursor_col++;
            }
            editor->cursor_row--;
        }
        editor->dirty = 1;
    }
}

void editor_update_mark(EditorState* editor) {
    if (editor->mark_start == -1) return;
    int marklen = abs(editor->gap_start - editor->mark_start);

    while (marklen > SCRATCH_BUFFER_SIZE && editor->mark_start > editor->gap_start) {
        --editor->mark_start;
        --marklen;
    }
    while (marklen > SCRATCH_BUFFER_SIZE && editor->mark_start < editor->gap_start) {
        ++editor->mark_start;
        --marklen;
    }
}

/*
 * Move the gap one character to the left.
 * (That is, move one character from before the gap to after it.)
 */
void editor_move_left(EditorState* editor) {
    if (editor->gap_start > 0) {
        --editor->gap_start;
        --editor->gap_end;
        editor->buffer[editor->gap_end] = editor->buffer[editor->gap_start];

        editor->redraw_mode = REDRAW_CURSOR;
        if (editor->buffer[editor->gap_start] != NL) {
            editor->cursor_col--;
        }
        else {
            int i = editor->gap_start - 1;
            editor->cursor_col = 0;
            while (i >= 0 && editor->buffer[i] != NL) {
                i--;
                editor->cursor_col++;
            }
            editor->cursor_row--;
        }

        if (editor->col_offset > 0) {
            uint8_t cx, cy;
            get_cursor_pos(&cx, &cy);
            if (cx < 3) {
                editor->col_offset--;
                editor->redraw_mode = REDRAW_ALL;
            }
        }
        editor_update_mark(editor);
    }
}

/*
 * Move the gap one character to the right.
 * (That is, move one character from after the gap into the gap.)
 */
void editor_move_right(EditorState* editor) {
    if (editor->gap_end < TEXT_BUFFER_SIZE) {
        editor->buffer[editor->gap_start] = editor->buffer[editor->gap_end];
        editor->gap_start++;
        editor->gap_end++;

        editor->redraw_mode = REDRAW_CURSOR;
        if (editor->buffer[editor->gap_start - 1] != NL) {
            editor->cursor_col++;
        }
        else {
            editor->cursor_col = 0;
            editor->cursor_row++;
        }

        if (editor->col_offset > 0) {
            uint8_t cx, cy;
            get_cursor_pos(&cx, &cy);
            if (cx > COLS - 3) {
                editor->col_offset++;
                editor->redraw_mode = REDRAW_ALL;
            }
        }
        editor_update_mark(editor);
    }
}

/*
 * Reposition the gap (i.e. the cursor) to a given logical text index.
 * This is done by repeated left/right moves.
 */
void editor_move_cursor_to(EditorState* editor, int pos) {
    while (editor->gap_start > pos) editor_move_left(editor);
    while (editor->gap_start < pos) editor_move_right(editor);
}

/*
 * Reposition the gap to the next character after a space
 */
void editor_move_word(EditorState* editor, int8_t direction) {
    int pos = editor->gap_start;
    int len = editor_length(editor);

    if (direction > 0) {
        // if in the middle of a word move to the end
        while (pos < len && !is_whitespace(editor_get_char(editor, pos))) {
            ++pos;
        }
        // move to the start of the next word
        while (pos < len && is_whitespace(editor_get_char(editor, pos))) {
            ++pos;
        }
    } else if (direction < 0 && pos > 0) {
        // If left character is not a space move left until we hit a space
        // at the begining of the current word
        if (pos > 0 && !is_whitespace(editor_get_char(editor, pos-1))) {
            while (pos > 0 && !is_whitespace(editor_get_char(editor, pos-1))) {
                --pos;
            }
        } else {
            // If we are in a white space region move left past the white space
            while(pos > 0 && is_whitespace(editor_get_char(editor, pos-1))) {
                --pos;
            }
            // then move left through the previous work
            while (pos > 0 && !is_whitespace(editor_get_char(editor, pos-1))) {
                --pos;
            }
        }
    }

    // Finally updat the cursor to the new possition
    editor_move_cursor_to(editor, pos);
}

/*
 * Compute the cursor's current (row, col) in the text by scanning
 * the characters up to gap_start. (This ignores the text after the gap.)
 */
void editor_get_cursor_position(EditorState* editor, int* row, int* col) {
    *row = editor->cursor_row;
    *col = editor->cursor_col;
}

/*
 * Move the cursor one line up.
 * This finds the start of the previous line and positions the cursor in that line
 * at the same column as the current cursor (or the end of the line, whichever comes first).
 */
void editor_move_up(EditorState* editor) {
    int cur_row, cur_col;
    editor_get_cursor_position(editor, &cur_row, &cur_col);
    if (cur_row == 0)
        return;  // Already on the first line.

    // Find the start of the current line.
    int pos = editor->gap_start - 1;
    while (pos >= 0 && editor->buffer[pos] != NL)
        pos--;
    int current_line_start = pos + 1;

    // Find the start of the previous line.
    pos = current_line_start - 2;
    while (pos >= 0 && editor->buffer[pos] != NL)
        pos--;
    int prev_line_start = pos + 1;

    // Compute previous line’s length.
    int prev_line_length = current_line_start - 1 - prev_line_start;
    int target_col = (cur_col < prev_line_length ? cur_col : prev_line_length);
    int target_pos = prev_line_start + target_col; ;
    editor_move_cursor_to(editor, target_pos);
}

/*
 * Move the cursor one line down.
 * This finds the next line and moves to the same column (or as far as is available).
 */
void editor_move_down(EditorState* editor) {
    int cur_row, cur_col;
    editor_get_cursor_position(editor, &cur_row, &cur_col);
    int total = editor_length(editor);

    // Find the end of the current line.
    int pos = editor->gap_start;
    while (pos < total) {
        char c = editor_get_char(editor, pos);
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
        editor_get_char(editor, next_line_start + next_line_length) != NL) {
        next_line_length++;
    }
    int target_col = (cur_col < next_line_length ? cur_col : next_line_length);
    int target_pos = next_line_start + target_col;
    editor_move_cursor_to(editor, target_pos);
}

/*
 * Update the vertical (row_offset) and horizontal (col_offset) scrolling
 * based on the current cursor position so that the cursor stays visible.
 */
void editor_update_scroll(EditorState* editor) {
    int cursor_row, cursor_col;
    editor_get_cursor_position(editor, &cursor_row, &cursor_col);

    int old_row_offset = editor->row_offset;
    int old_col_offset = editor->col_offset;
    // Vertical scrolling:
    if (cursor_row < editor->row_offset)
        editor->row_offset = cursor_row;
    else if (cursor_row >= editor->row_offset + LINES - 1)
        editor->row_offset = cursor_row - (LINES - 2); // -1 reserved for status line

    // Horizontal scrolling:
    if (cursor_col < editor->col_offset)
        editor->col_offset = cursor_col;
    else if (cursor_col >= editor->col_offset + COLS)
        editor->col_offset = cursor_col - COLS + 1;

    // If the offsets have changed, redraw the screen.
    if (editor->row_offset != old_row_offset || editor->col_offset != old_col_offset) {
        int lines_to_scroll = abs(editor->row_offset - old_row_offset);
        if (editor->row_offset > old_row_offset) {
            while (lines_to_scroll && editor->top_row_index < editor->gap_start) {
                char c = editor->buffer[editor->top_row_index];
                if (c == NL) {
                    --lines_to_scroll;
                }
                ++editor->top_row_index;
            }
        }
        else if (editor->row_offset < old_row_offset) {
            while (lines_to_scroll && editor->top_row_index > 0) {
                char c = editor->buffer[editor->top_row_index - 1];
                if (c == NL) {
                    --lines_to_scroll;
                }
                --editor->top_row_index;
            }
            // move to start of the line
            while (editor->top_row_index > 0 && editor->buffer[editor->top_row_index - 1] != NL) {
                --editor->top_row_index;
            }
        }

        editor->redraw_mode = REDRAW_ALL;
    }
}

void update_hardware_cursor(EditorState* editor, int cursor_col, int cursor_row)
{
    // Place the hardware cursor in the proper on-screen position.
    int screen_cursor_row = cursor_row - editor->row_offset;
    int screen_cursor_col = cursor_col - editor->col_offset;
    if (screen_cursor_row >= 0 && screen_cursor_row < LINES - 1 &&
        screen_cursor_col >= 0 && screen_cursor_col < COLS)
        set_cursor_pos(screen_cursor_col, screen_cursor_row);
    else
        set_cursor_pos(0, LINES - 2);
}

void editor_draw_line(EditorState* editor) {
    int i = editor->gap_start;
    while (i > 0 && editor->buffer[i - 1] != NL)
        i--;

    uint8_t cx, cy; // cursor position
    get_cursor_pos(&cx, &cy);
    (cx);

    int total = editor_length(editor);
    set_cursor_pos(0, cy);
    int col_offset = editor->col_offset;
    for (int col = 0; col < COLS + col_offset && i < total; ++col, ++i) {
        char c = editor_get_char(editor, i);
        if (c == NL) break;
        if (col >= col_offset) putch(c);
    }
    clreol();

    int cursor_row, cursor_col;
    editor_get_cursor_position(editor, &cursor_row, &cursor_col);
    update_hardware_cursor(editor, cursor_col, cursor_row);
    editor->redraw_mode = REDRAW_NONE;
}

/*
 * Draw the contents of the gap buffer on the screen.
 * We “reassemble” the text (ignoring the gap) into lines. A reserved status line
 * is drawn at the bottom.
 */
void editor_draw(EditorState* editor) {
    int total = editor_length(editor);
    int i = editor->top_row_index;

    set_cursor_pos(0, 0);
    int row;
    int col_offset = editor->col_offset;

    if (editor->mark_start != -1) editor_update_mark(editor);
    
    standard();
    for (row = 0; row < LINES - 1 && i < total; ++row, ++i) {
        for (int col = 0; i < total; ++col, ++i) {
            char c = editor_get_char(editor, i);
            if (c == NL) {
                clreol();
                putch(NL);
                break;
            }

            if (col >= col_offset && col < COLS + col_offset) {
                if (editor->mark_start != -1) {                    
                    if ((editor->mark_start < editor->gap_start && i >= editor->mark_start && i < editor->gap_start) ||
                        (editor->mark_start > editor->gap_start && i >= editor->gap_start && i < editor->mark_start))
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
    editor_get_cursor_position(editor, &cursor_row, &cursor_col);
    update_hardware_cursor(editor, cursor_col, cursor_row);
    editor->redraw_mode = REDRAW_NONE;
}

void editor_message(const char* msg) {
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);
    set_cursor_pos(0, LINES);
    print("%s", msg);
    clreol();
    set_cursor_pos(ox, oy);
}

void editor_update_filename(EditorState* editor) {
    set_cursor_pos(0, SCREEN_HEIGHT - 1);
    highlight();
    print("Filename: %s%c", editor->filename ? editor->filename : "Untitled", editor->dirty ? '*' : ' ');
    standard();
    clreol();
}

void editor_print_hotkey(const char* short_cut_key, const char* description) {
    int len = HOTKEY_ITEM_WIDTH - (strlen(short_cut_key) + strlen(description));
    highlight();print(short_cut_key); standard();
    print(" %s", description);
    while (len-- > 0) putch(' ');
}

void editor_show_hotkeys(EditorState* editor) {
    set_cursor_pos(0, LINES + 1);
    int i = 0;
    for (Command* cmd = &commands[0]; cmd->short_cut_key != NULL; ++cmd) {
        editor_print_hotkey(cmd->short_cut_key, cmd->description);
        if (++i % HOTKEY_ITEMS_PER_LINE == 0) putch(NL);
    }
    print("Version: %s", VERSION);
    if (editor->file_too_large) editor_message("File too large, save is disabled");
}

void editor_update_status(EditorState* editor, char key) {
    static uint8_t wasdirty = 0;
    int total = editor_length(editor);

    int cursor_row, cursor_col;
    uint8_t ox, oy;
    get_cursor_pos(&ox, &oy);
    editor_get_cursor_position(editor, &cursor_row, &cursor_col);

    if (wasdirty != editor->dirty) {
        wasdirty = editor->dirty;
        editor_update_filename(editor);        
    }
    set_cursor_pos(45, SCREEN_HEIGHT - 1);
    print("Mem: %d Ln %d, Col %d Key: %d", TEXT_BUFFER_SIZE - total, cursor_row + 1, cursor_col + 1, key);
    clreol();
    set_cursor_pos(ox, oy);
}

void editor_redraw(EditorState* editor) {
    editor_update_scroll(editor);

    if (editor->mark_start != -1) {
        editor->redraw_mode = REDRAW_ALL;
    }

    if (editor->redraw_mode == REDRAW_ALL) {
        editor_draw(editor);
    }
    else if (editor->redraw_mode == REDRAW_LINE) {
        editor_draw_line(editor);
    }
    else if (editor->redraw_mode == REDRAW_CURSOR) {
        int cursor_row, cursor_col;
        editor_get_cursor_position(editor, &cursor_row, &cursor_col);
        update_hardware_cursor(editor, cursor_col, cursor_row);
    }
}

CommandAction confirm(const char *prompt) {
    CommandAction retval;

    set_cursor_pos(0, LINES);
    print("%s (y/n) ", prompt);
    char ch = getch();
    
    switch(ch) {
        case 'Y':
        case 'y': retval = COMMAND_ACTION_YES; break;
        case KEY_ESC: retval  = COMMAND_ACTION_CANCEL; break;
        default: retval = COMMAND_ACTION_NO; break;
    }
    set_cursor_pos(0, LINES);
    clreol();
    return retval;
}

// Edit a line of text restricting to the given alphabet.
uint8_t edit_line(const char* prompt, const char* alphabet, char* buffer, uint8_t maxlen) {
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
                        memmove(buffer+i+1, buffer+i, len-i);
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

int editor_save_file(EditorState* editor) {
    if (editor->file_too_large) return 0;

    char buf[32];
#ifdef __ZXNEXT
    errno = 0;
    strcpy(tmpbuffer, editor->filename);
    strcat(tmpbuffer, ".zed");
    char f = esxdos_f_open(tmpbuffer, ESXDOS_MODE_W | ESXDOS_MODE_CT);
    if (errno) return errno;

    int total = editor_length(editor);
    int i = 0;
    while (i < total) {
        int j = 0;
        while (j < sizeof(buf) >> 1 && i < total) {
            char ch = editor_get_char(editor, i++);
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
    esx_f_unlink(editor->filename);
    if (esx_f_rename(tmpbuffer, editor->filename)) return errno;
    esx_f_unlink(tmpbuffer);
#endif //__ZXNEXT
    return 0;
}

void editor_init_file(EditorState* editor, const char* filepath) {
    editor->filename = NULL;
#ifdef __ZXNEXT
    if (filepath) {
        strcpy(filename, filepath);         // copy the file path the filename
        editor->filename = &filename[0];
        errno = 0;
        char f = esxdos_f_open(filename, ESXDOS_MODE_R | ESXDOS_MODE_OE);
        if (!errno) {
            size_t bytes_read = esxdos_f_read(f, editor->buffer, TEXT_BUFFER_SIZE);

            editor->file_too_large = (bytes_read == TEXT_BUFFER_SIZE);
            if (bytes_read > 0) {
                char* start = &editor->buffer[0];

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
                            editor->file_too_large = 1;
                            break;
                        }
                        *dst-- = ' ';
                        *dst-- = ' ';
                        bytescopied += 2;
                    }
                    else if (ch != skip_eol_char) {
                        if (dst - 1 < start) {
                            editor->file_too_large = 1;
                            break;
                        }
                        if (lead_eol_char && ch == lead_eol_char) ch = NL;
                        *dst-- = ch;
                        ++bytescopied;
                    }
                    --src;
                }
                editor->gap_start = 0;
                editor->gap_end = TEXT_BUFFER_SIZE - bytescopied;
            }
            esxdos_f_close(f);
        }
    }
#endif
}

CommandAction editor_save(EditorState* editor) {
    if (editor->file_too_large) return COMMAND_ACTION_NONE;
    
    editor->redraw_mode = REDRAW_CURSOR;
    set_cursor_pos(0, LINES);

    if (!edit_line("File name", NULL, filename, MAX_FILENAME_LEN))
        return COMMAND_ACTION_CANCEL;

    editor->filename = &filename[0];
    int status = editor_save_file(editor);
    if (status) {
#ifdef __ZXNEXT
        esx_m_geterr(status, tmpbuffer);
        editor_message(tmpbuffer);
#endif
        return COMMAND_ACTION_FAILED;
    }

    editor->dirty = 0;
    editor_update_filename(editor);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_mark(EditorState* editor) {
    if (editor->mark_start == -1)
        editor->mark_start = editor->gap_start;
    else {
        editor->mark_start = -1;
        editor->redraw_mode = REDRAW_ALL;
    }
    return COMMAND_ACTION_NONE;
}

void editor_cutcopy(EditorState* editor, uint8_t cut) {
    if (editor->mark_start == -1) return;

    int start = editor->mark_start < editor->gap_start ? editor->mark_start : editor->gap_start;
    int end = editor->mark_start > editor->gap_start ? editor->mark_start : editor->gap_start;
    int len = end - start;

    // Copy marked text
    for (int i = 0; i < len && i < SCRATCH_BUFFER_SIZE; ++i) {
        scratch_buffer[i] = editor_get_char(editor, start + i);
    }
    scratch_buffer[len] = '\0';

    if (cut) {
        // Cut marked text by deleting character by character from
        // the end of the marker to the begining
        editor_move_cursor_to(editor, end);
        for (int i = 0; i < len; ++i) {
            editor_backspace(editor);
        }
    }

    editor->mark_start = -1;
    editor->redraw_mode = REDRAW_ALL;
}

CommandAction editor_copy(EditorState* editor) {
    editor_cutcopy(editor, 0);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_cut(EditorState* editor) {
    editor_cutcopy(editor, 1);
    return COMMAND_ACTION_NONE;
}

CommandAction editor_paste(EditorState* editor) {
    char* src = scratch_buffer;
    for (int i = 0; *src && i < SCRATCH_BUFFER_SIZE; ++i) {
        editor_insert(editor, *src++);
    }
    editor->mark_start = -1;
    editor->redraw_mode = REDRAW_ALL;
    return COMMAND_ACTION_NONE;
}

int editor_search(EditorState* editor, const char* str, int start) {
    int len = strlen(str);
    int total = editor_length(editor);
    if (start < 0 || start >= total) {
        start = 0;
    }
    for (int i = start; i < total - len; ++i) {
        for (int j = 0; j < len; ++j) {
            if (editor_get_char(editor, i + j) != str[j]) {
                break;
            }
            if (j == len - 1) {
                return i;
            }
        }
    }
    return -1;
}

CommandAction editor_find(EditorState* editor) {
    static char input[32] = { 0 };
    set_cursor_pos(0, LINES);
    while (edit_line("Find", NULL, input, sizeof(input))) {
        int len = strlen(input);
        int total = editor_length(editor);

        int i = editor_search(editor, input, editor->gap_start);
        if (i == -1) i = editor_search(editor, input, 0);

        if (i != -1) {
            editor_move_cursor_to(editor, i + len);
            editor->mark_start = i;            
            editor->redraw_mode = REDRAW_ALL;
            editor_redraw(editor);
            editor_update_status(editor, 0);
        }
        set_cursor_pos(0, LINES);        
    }
    editor->mark_start = -1;
    editor->redraw_mode = REDRAW_CURSOR;
    return COMMAND_ACTION_NONE;
}

CommandAction editor_goto(EditorState* editor) {
    char input[8] = { 0 };
    set_cursor_pos(0, LINES);
    if (edit_line("Line number", "1234567890", input, sizeof(input))) {
        int lineno = atoi(input);
        int total = editor_length(editor);
        int i = 0;
        for (int j = 1; j < lineno && i < total; ++j) {
            while (i < total && editor_get_char(editor, i) != NL)
                ++i;
            ++i;
        }
        if (i < total) editor_move_cursor_to(editor, i);
    }
    return COMMAND_ACTION_NONE;
}

CommandAction editor_quit(EditorState* editor) {
    editor->redraw_mode = REDRAW_CURSOR;
    if (editor->dirty && !editor->file_too_large) {
        CommandAction action = confirm("File modified. Save?");
        switch(action) {
            case COMMAND_ACTION_YES:        
                if (editor_save(editor) != COMMAND_ACTION_NONE) {
                    return COMMAND_ACTION_NONE;
                }
                break;
            case COMMAND_ACTION_CANCEL:
                return COMMAND_ACTION_NONE;
        }                
        return COMMAND_ACTION_QUIT;                                    
    } else if (confirm("Quit?") == COMMAND_ACTION_YES) {
        return COMMAND_ACTION_QUIT;        
    }

    return COMMAND_ACTION_NONE;
}

void edit(const char* filepath) {
    EditorState editor;

    /* Initial the editor; the entire space is initially the gap. */
    editor.buffer = &text_buffer[0];
    editor.filename = NULL;
    editor.gap_start = 0;
    editor.gap_end = TEXT_BUFFER_SIZE;
    editor.row_offset = 0;
    editor.col_offset = 0;
    editor.top_row_index = 0;
    editor.cursor_row = 0;
    editor.cursor_col = 0;
    editor.dirty = 0;
    editor.mark_start = -1;
    editor.file_too_large = 0;
    editor.redraw_mode = REDRAW_ALL;

    filename[0] = '\0';

    if (filepath) {
        strncpy(filename, filepath, MAX_FILENAME_LEN); // copy the file path the filename
        editor.filename = &filename[0];
        editor_init_file(&editor, filepath);
    }

    char ch = 0;
    cls();
    editor_show_hotkeys(&editor);
    editor_update_filename(&editor);
    while (1) {
        editor_redraw(&editor);
        editor_update_status(&editor, ch);

        ch = getch();
        switch (ch) {
            case KEY_WORDLEFT:
                editor_move_word(&editor, -1);
                break;
            case KEY_WORDRIGHT:
                editor_move_word(&editor, 1);
                break;
            case KEY_LEFT:
                editor_move_left(&editor);
                break;
            case KEY_RIGHT:
                editor_move_right(&editor);
                break;
            case KEY_PAGEUP:
                for (int i = 0; i < LINES - 2; ++i)
                    editor_move_up(&editor);
                break;
            case KEY_UP:
                editor_move_up(&editor);
                break;
            case KEY_PAGEDOWN:
                for (int i = 0; i < LINES - 2; ++i)
                    editor_move_down(&editor);
                break;
            case KEY_DOWN:                
                editor_move_down(&editor);
                break;            
            default:
                switch (ch) {
                    case KEY_BACKSPACE:
                        editor_backspace(&editor);
                        break;
                    case KEY_TAB:
                        editor_insert_tab(&editor);
                        break;
                    case KEY_ENTER:
                        editor_insert(&editor, NL);
                        break;
                    default:
                        if (ch >= 32 && ch <= 127) {
                            editor_insert(&editor, (char)ch);
                        } else {
                            for (Command* cmd = (Command*)commands; cmd->short_cut_key != NULL; ++cmd) {
                                if (ch == cmd->key) {
                                    if (cmd->action) {
                                        CommandAction action = cmd->action(&editor);
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

