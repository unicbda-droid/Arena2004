/* Arena2004 - in-game console (Quake/Gothic style) */
#ifndef CONSOLE_H
#define CONSOLE_H
#include <stdbool.h>
#include "ui.h"

#define CON_INPUT_MAX 512
#define CON_HISTORY_MAX 64
#define CON_LINES_MAX 256

typedef struct {
    bool open;
    char input[CON_INPUT_MAX];
    int input_len;
    char history[CON_HISTORY_MAX][CON_INPUT_MAX];
    int history_count;
    int history_pos;
    char lines[CON_LINES_MAX][CON_INPUT_MAX];
    int line_count;
    int scroll;
    float anim;
    char hint[CON_INPUT_MAX];
} Console;

void console_init(Console* c);
void console_set_open(Console* c, bool open);
void console_toggle(Console* c);
void console_printf(Console* c, const char* fmt, ...);
void console_print(Console* c, const char* text);
void console_clear(Console* c);
void console_add_text(Console* c, const char* text, int len);
void console_backspace(Console* c);
void console_history_move(Console* c, int dir);
bool console_submit(Console* c, char* out, int cap);
void console_scroll(Console* c, int delta);
void console_draw(Console* c, Ui* ui, int w, int h);

#endif
