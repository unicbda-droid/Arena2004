/* Arena2004 - immediate mode UI + font rendering */
#ifndef UI_H
#define UI_H
#include <GL/glew.h>
#include "math3d.h"

typedef struct {
    float mx, my;
    bool down, pressed, released;
    int wheel;
    bool shift, ctrl, alt;
    bool enter, backspace, escape, tab, del;
    bool up, down_nav, left, right;
    bool home, end, page_up, page_down;
    bool minus, plus;
    char text[64];
    int text_len;
} UiInput;

/* keyboard focus registry */
enum { UI_NAV_BUTTON = 0, UI_NAV_SLIDER = 1, UI_NAV_TEXT = 2, UI_NAV_CUSTOM = 3 };

typedef struct {
    unsigned int id;
    float x, y, w, h;
    unsigned char kind;
} UiNavItem;

typedef struct {
    float x,y,u,v,r,g,b,a;
} UiVert;

typedef struct {
    /* font */
    GLuint tex;
    int tex_w, tex_h;
    float ascent, descent, line_gap; /* in baked pixels, bake = 40px */
    int font_bake;
    /* atlas regions */
    float white_u,white_v, white_du,white_dv;
    float glow_u,glow_v, glow_du,glow_dv;
    float ring_u,ring_v, ring_du,ring_dv;
    float bar_u,bar_v, bar_du,bar_dv;
    float stripe_u,stripe_v, stripe_du,stripe_dv;
    /* batch */
    GLuint prog, vao, vbo;
    int vbo_cap;
    UiVert* verts;
    int vcount;
    int w,h;
    int clip[4];
    bool clipping;
    /* interaction */
    UiInput in;
    unsigned int hot, active, focus;
    bool claimed;
    bool mouse_over_ui;
    /* keyboard cursor focus */
    UiNavItem nav_items[256];
    int nav_count;
    UiNavItem nav_prev[256];
    int nav_prev_count;
    unsigned int nav_focus;
    bool nav_engaged;
    bool nav_tab_cycle;
    bool nav_warp;
    float nav_warp_x, nav_warp_y;
    float last_mx, last_my;
    bool have_mouse;
    char edit_buf[256];
    int edit_len;
    bool edit_commit;
    const char* edit_id;
    float drag_start_x;
    float drag_start_val;
    bool drag_moved;
} Ui;

typedef struct {
    Color panel, panel2, header, border, text, dim, accent, accent2, good, bad, black, white;
} UiTheme;

extern UiTheme ui_theme;

bool ui_init(Ui* ui, const char* font_path, int w, int h);
void ui_destroy(Ui* ui);
void ui_frame_begin(Ui* ui, int w, int h, UiInput in);
void ui_frame_end(Ui* ui);
void ui_flush(Ui* ui);

void ui_rect(Ui* ui, float x,float y,float w,float h, Color c);
void ui_rect_grad(Ui* ui, float x,float y,float w,float h, Color top, Color bottom);
void ui_rect_outline(Ui* ui, float x,float y,float w,float h, Color c, float t);
void ui_tri(Ui* ui, Vec2 a, Vec2 b, Vec2 c, Color col);
void ui_line(Ui* ui, Vec2 a, Vec2 b, Color c, float t);
void ui_glow(Ui* ui, float x,float y,float w,float h, Color c);
void ui_ring(Ui* ui, float x,float y,float w,float h, Color c);
void ui_push_clip(Ui* ui, int x,int y,int w,int h);
void ui_pop_clip(Ui* ui);

void ui_text(Ui* ui, float x,float top,const char* text,float size,Color c);
void ui_text_shadow(Ui* ui, float x,float top,const char* text,float size,Color c, Color sh);
void ui_text_center(Ui* ui, float cx,float top,const char* text,float size,Color c);
void ui_text_right(Ui* ui, float rx,float top,const char* text,float size,Color c);
float ui_text_width(Ui* ui, const char* text, float size);

bool ui_mouse_in(Ui* ui, float x,float y,float w,float h);
bool ui_nav_take_warp(Ui* ui, float* x, float* y);
bool ui_button(Ui* ui, const char* id, const char* label, float x,float y,float w,float h, int style);
bool ui_icon_button(Ui* ui, const char* id, int icon, float x,float y,float w,float h, bool active);
void ui_icon(Ui* ui, int icon, float x,float y,float size, Color c);
bool ui_toggle(Ui* ui, const char* id, const char* label, float x,float y,float w,float h, bool* value);
bool ui_dragf(Ui* ui, const char* id, const char* label, float x,float y,float w,float h, float* value, float step, float lo, float hi, int decimals);
bool ui_slider(Ui* ui, const char* id, const char* label, float x,float y,float w,float h, float* value, float lo, float hi);
bool ui_textbox(Ui* ui, const char* id, float x,float y,float w,float h, char* buf, int cap, const char* hint);
bool ui_color_swatch(Ui* ui, const char* id, float x,float y,float w,float h, Color c, bool selected);
int ui_list(Ui* ui, const char* id, const char** items, int count, int selected, float x,float y,float w,float row_h);
void ui_tooltip(Ui* ui, const char* text, float x,float y);

/* icons */
enum {
    ICON_PLAY, ICON_STOP, ICON_SAVE, ICON_FOLDER, ICON_CUBE, ICON_LIGHT,
    ICON_PICKUP, ICON_SPAWN, ICON_JUMP, ICON_LAVA, ICON_MOVE, ICON_ROTATE,
    ICON_SCALE, ICON_SELECT, ICON_GRID, ICON_UNDO, ICON_REDO, ICON_TRASH,
    ICON_COPY, ICON_CLOSE, ICON_SEARCH, ICON_MUSIC, ICON_COUNT
};

#endif
