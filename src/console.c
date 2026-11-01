#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const Color C_BG    = {0.012f, 0.016f, 0.022f, 0.95f};
static const Color C_LINE  = {0.20f, 0.24f, 0.30f, 1.0f};
static const Color C_TEXT  = {0.82f, 0.87f, 0.93f, 1.0f};
static const Color C_DIM   = {0.48f, 0.55f, 0.64f, 1.0f};
static const Color C_ACC   = {0.98f, 0.62f, 0.16f, 1.0f};
static const Color C_BLUE  = {0.32f, 0.70f, 1.00f, 1.0f};
static const Color C_GOOD  = {0.42f, 0.88f, 0.48f, 1.0f};

void console_init(Console* c) {
    memset(c, 0, sizeof(*c));
    c->history_pos = 0;
    console_print(c, "Arena2004 console ready.");
    console_print(c, "Tippe 'help' fuer alle Befehle. 'opencode' startet OpenCode im Terminal.");
    console_print(c, "Alles ist per Konsole, Tastatur und GUI steuerbar.");
}

void console_set_open(Console* c, bool open) {
    c->open = open;
    c->scroll = 0;
    if (open) c->hint[0] = 0;
}

void console_toggle(Console* c) { console_set_open(c, !c->open); }

static void console_push(Console* c, const char* text) {
    if (c->line_count >= CON_LINES_MAX) {
        memmove(c->lines[0], c->lines[1], sizeof(c->lines[0]) * (CON_LINES_MAX - 1));
        c->line_count = CON_LINES_MAX - 1;
    }
    char* dst = c->lines[c->line_count++];
    str_copy(dst, text, CON_INPUT_MAX);
    c->scroll = 0;
}

void console_print(Console* c, const char* text) {
    if (!text) return;
    const char* p = text;
    while (*p) {
        const char* nl = strchr(p, '\n');
        int n = nl ? (int)(nl - p) : (int)strlen(p);
        if (n <= 0) {
            console_push(c, "");
        } else {
            while (n > 0) {
                int take = n;
                if (take > 150) {
                    take = 150;
                    for (int i = take; i > 40; i--) {
                        if (p[i] == ' ') { take = i; break; }
                    }
                }
                char buf[CON_INPUT_MAX];
                if (take >= (int)sizeof(buf)) take = (int)sizeof(buf) - 1;
                memcpy(buf, p, (size_t)take);
                buf[take] = 0;
                console_push(c, buf);
                p += take;
                n -= take;
                while (*p == ' ') { p++; n--; }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

void console_printf(Console* c, const char* fmt, ...) {
    char buf[CON_INPUT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_print(c, buf);
}

void console_clear(Console* c) {
    c->line_count = 0;
    c->scroll = 0;
}

void console_add_text(Console* c, const char* text, int len) {
    if (!text) return;
    for (int i = 0; i < len && c->input_len < CON_INPUT_MAX - 1; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch == 127) continue;
        if (ch == '`' || ch == '~') continue;
        c->input[c->input_len++] = (char)ch;
    }
    c->input[c->input_len] = 0;
}

void console_backspace(Console* c) {
    if (c->input_len > 0) c->input[--c->input_len] = 0;
}

void console_history_move(Console* c, int dir) {
    if (c->history_count <= 0) return;
    if (c->history_pos < 0) c->history_pos = 0;
    if (c->history_pos > c->history_count) c->history_pos = c->history_count;
    c->history_pos += dir;
    if (c->history_pos < 0) c->history_pos = 0;
    if (c->history_pos > c->history_count) c->history_pos = c->history_count;
    if (c->history_pos == c->history_count) {
        c->input[0] = 0;
        c->input_len = 0;
    } else {
        str_copy(c->input, c->history[c->history_pos], CON_INPUT_MAX);
        c->input_len = (int)strlen(c->input);
    }
}

bool console_submit(Console* c, char* out, int cap) {
    if (c->input_len <= 0) return false;
    char cmd[CON_INPUT_MAX];
    str_copy(cmd, c->input, sizeof(cmd));
    char logged[CON_INPUT_MAX + 8];
    snprintf(logged, sizeof(logged), "> %s", cmd);
    console_push(c, logged);
    if (c->history_count == 0 || strcmp(c->history[c->history_count - 1], cmd) != 0) {
        if (c->history_count >= CON_HISTORY_MAX) {
            memmove(c->history[0], c->history[1], sizeof(c->history[0]) * (CON_HISTORY_MAX - 1));
            c->history_count = CON_HISTORY_MAX - 1;
        }
        str_copy(c->history[c->history_count++], cmd, CON_INPUT_MAX);
    }
    c->history_pos = c->history_count;
    c->input[0] = 0;
    c->input_len = 0;
    c->hint[0] = 0;
    if (out && cap > 0) str_copy(out, cmd, cap);
    return true;
}

void console_scroll(Console* c, int delta) {
    c->scroll += delta;
    if (c->scroll < 0) c->scroll = 0;
    int maxs = c->line_count > 1 ? c->line_count - 1 : 0;
    if (c->scroll > maxs) c->scroll = maxs;
}

void console_draw(Console* c, Ui* ui, int w, int h) {
    if (!c->open) return;
    float y0 = h * 0.30f;
    float ph = h - y0;
    ui_rect(ui, 0, y0, (float)w, ph, C_BG);
    ui_rect(ui, 0, y0, (float)w, 2, C_ACC);
    ui_rect(ui, 0, y0 + 2, (float)w, 1, C_LINE);

    /* header */
    ui_rect(ui, 0, y0, (float)w, 28, col(0.045f, 0.055f, 0.070f, 1));
    ui_text(ui, 16, y0 + 8, "ARENA 2004  //  CONSOLE", 12, C_ACC);
    ui_text_right(ui, (float)w - 16, y0 + 9, "TILDE/F8 SCHLIESSEN   ENTER AUSFUEHREN   TAB VERVOLLSTAENDIGEN   ESC ZURUECK", 9, C_DIM);
    ui_rect(ui, 0, y0 + 28, (float)w, 1, C_LINE);

    float in_h = 34;
    float hint_h = 22;
    float out_top = y0 + 34;
    float out_bot = (float)h - in_h - hint_h - 8;
    if (out_bot < out_top + 20) out_bot = out_top + 20;
    float line_h = 16;
    int visible = (int)((out_bot - out_top) / line_h);
    if (visible < 1) visible = 1;

    int start = c->line_count - visible - c->scroll;
    if (start < 0) start = 0;
    int end = start + visible;
    if (end > c->line_count) end = c->line_count;

    ui_push_clip(ui, 0, (int)out_top, w, (int)(out_bot - out_top));
    for (int i = start; i < end; i++) {
        const char* ln = c->lines[i];
        Color colr = C_TEXT;
        if (ln[0] == '>') colr = C_BLUE;
        else if (!strncmp(ln, "error", 5) || !strncmp(ln, "Error", 5) || !strncmp(ln, "FEHLER", 6)) colr = col(1.0f, 0.45f, 0.35f, 1);
        else if (!strncmp(ln, "ok", 2) || !strncmp(ln, "OK", 2)) colr = C_GOOD;
        ui_text(ui, 16, out_top + (i - start) * line_h + 1, ln, 11, colr);
    }
    ui_pop_clip(ui);

    if (c->scroll > 0) {
        char sb[64];
        snprintf(sb, sizeof(sb), "SCROLL %d", c->scroll);
        ui_text_right(ui, (float)w - 16, out_bot - 14, sb, 9, C_ACC);
    }

    /* hint / completion */
    float hy = (float)h - in_h - hint_h + 2;
    if (c->hint[0]) {
        ui_text(ui, 18, hy, c->hint, 10, C_ACC);
    } else {
        ui_text(ui, 18, hy, "help  |  commands  |  set/get  |  menu  |  play  |  editor  |  spawn  |  opencode", 9, C_DIM);
    }

    /* input line */
    float iy = (float)h - in_h + 4;
    ui_rect(ui, 8, iy - 5, (float)w - 16, in_h - 6, col(0.02f, 0.025f, 0.033f, 1));
    ui_rect_outline(ui, 8, iy - 5, (float)w - 16, in_h - 6, C_LINE, 1);
    ui_text(ui, 16, iy + 5, "]", 15, C_ACC);
    float tx = 32;
    /* show the tail of long input lines */
    const char* shown = c->input;
    float avail = (float)w - tx - 40;
    while (*shown && ui_text_width(ui, shown, 13) > avail) shown++;
    ui_text(ui, tx, iy + 3, shown, 13, C_TEXT);
    float cx = tx + ui_text_width(ui, shown, 13);
    ui_rect(ui, cx + 1, iy + 3, 2, 18, C_ACC);
    char cnt[64];
    snprintf(cnt, sizeof(cnt), "%d", c->input_len);
    ui_text_right(ui, (float)w - 16, iy + 6, cnt, 9, C_DIM);
}
