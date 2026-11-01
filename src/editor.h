/* Arena2004 - UE3-style level editor */
#ifndef EDITOR_H
#define EDITOR_H
#include "world.h"
#include "render.h"
#include "ui.h"
#include "game.h"

#define ED_UNDO_MAX 24
#define ED_MAPS_MAX 48

typedef struct {
    float mx, my, dx, dy, wheel;
    bool left, right, middle;
    bool left_pressed, left_released, right_pressed;
    bool kf, kb, kl, kr, ku, kd, fast;
    bool shift, ctrl, alt;
    bool del, undo, redo, escape, duplicate, focus, play;
    bool enter, backspace, space;
    char text[64];
    int text_len;
} EdInput;

typedef struct {
    Actor actors[MAX_ACTORS];
    int count;
    int selected;
} EdUndoState;

typedef struct {
    World* world;
    Camera cam;
    float cam_speed;
    int selected;
    int hover_actor;
    int mode; /* 0 select, 1 move, 2 rotate, 3 scale */
    int place_type;
    int place_sub;
    bool placing;
    float grid;
    bool snap;
    bool show_grid;
    bool show_helpers;
    bool top_view;
    bool pie_active;
    bool pie_stop_request;
    World pie_world;
    Game pie_game;
    int pie_target;

    /* layout */
    int vx, vy, vw, vh;
    int w, h;

    /* drag */
    bool dragging;
    int drag_axis;
    Vec3 drag_origin;
    Vec3 drag_start_pos;
    Vec3 drag_start_size;
    float drag_start_yaw;
    float drag_mx, drag_my;
    float drag_scale;
    bool gizmo_hover;
    int hover_axis;

    /* undo */
    EdUndoState* undo;
    int undo_count, undo_pos;
    bool undo_valid;
    int undo_bytes;

    /* browser */
    char map_list[ED_MAPS_MAX][64];
    int map_count;
    int map_sel;
    int browser_tab;
    char map_name[64];
    char status[192];
    float status_t;
    bool dirty;

    /* msaa? no */
    Vec3 cursor_world;
    bool cursor_valid;
    float fps;
} Editor;

bool editor_init(Editor* ed, World* world, int w, int h);
void editor_shutdown(Editor* ed);
void editor_update(Editor* ed, float dt, const EdInput* in, int w, int h);
void editor_get_camera(Editor* ed, Camera* cam);
void editor_draw_3d(Editor* ed, Renderer* r);
void editor_draw_ui(Editor* ed, Ui* ui, int w, int h);
void editor_refresh_maps(Editor* ed);
void editor_set_status(Editor* ed, const char* fmt, ...);
void editor_play(Editor* ed);
void editor_stop(Editor* ed);

#endif
