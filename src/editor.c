#include "editor.h"
#include "hud.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "platform.h"
#include "dirent_compat.h"

static const Color C_PANEL   = {0.070f, 0.080f, 0.094f, 1.0f};
static const Color C_HEADER  = {0.120f, 0.136f, 0.158f, 1.0f};
static const Color C_BORDER  = {0.210f, 0.235f, 0.270f, 1.0f};
static const Color C_TEXT    = {0.840f, 0.870f, 0.900f, 1.0f};
static const Color C_DIM     = {0.470f, 0.510f, 0.560f, 1.0f};
static const Color C_ACCENT  = {0.980f, 0.600f, 0.140f, 1.0f};
static const Color C_BLUE    = {0.250f, 0.620f, 0.980f, 1.0f};
static const Color C_GREEN   = {0.390f, 0.820f, 0.430f, 1.0f};
static const Color C_WHITE   = {1.0f, 1.0f, 1.0f, 1.0f};

static const char* MAT_COLORS_HEX[16] = {
    "#5a6270", "#6b7484", "#d1a218", "#191b1f",
    "#8a7d6e", "#2bd0ff", "#3457a8", "#b06a3c",
    "#d8dde6", "#3d7a9e", "#7a8794", "#c23a2c",
    "#e0b21e", "#3f5c8a", "#2b7fc4", "#15171c"
};
static Color mat_color(int m) {
    uint32_t h = 0x5a6270;
    if (m >= 0 && m < 16) {
        const char* s = MAT_COLORS_HEX[m];
        h = (uint32_t)strtoul(s + 1, NULL, 16);
    }
    return col_hex(h);
}

/* ------------------------------------------------------------------ */
static void push_undo(Editor* ed) {
    if (!ed->undo) return;
    int slot = ed->undo_pos % ED_UNDO_MAX;
    EdUndoState* s = &ed->undo[slot];
    memcpy(s->actors, ed->world->actors, sizeof(Actor) * ed->world->count);
    s->count = ed->world->count;
    s->selected = ed->selected;
    ed->undo_pos++;
    if (ed->undo_count < ED_UNDO_MAX) ed->undo_count++;
    ed->undo_valid = true;
    ed->dirty = true;
}
static void restore_undo(Editor* ed, EdUndoState* s) {
    memcpy(ed->world->actors, s->actors, sizeof(Actor) * s->count);
    ed->world->count = s->count;
    ed->selected = s->selected;
    if (ed->selected >= ed->world->count) ed->selected = -1;
    ed->dirty = true;
}
static void do_undo(Editor* ed) {
    if (ed->undo_count <= 0) return;
    int slot = (ed->undo_pos - 1) % ED_UNDO_MAX;
    ed->undo_pos--;
    ed->undo_count--;
    restore_undo(ed, &ed->undo[slot]);
    editor_set_status(ed, "Undo");
}
static void do_redo(Editor* ed) {
    /* simple: redo re-applies current state snapshot impossible; keep last */
    editor_set_status(ed, "Redo unavailable in this build");
}

void editor_set_status(Editor* ed, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
    va_end(ap);
    ed->status_t = 4.0f;
}

void editor_refresh_maps(Editor* ed) {
    plat_ensure_dir("maps");
    ed->map_count = 0;
    DIR* d = opendir("maps");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) && ed->map_count < ED_MAPS_MAX) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcmp(e->d_name + l - 4, ".map")) continue;
            if (e->d_name[0] == '.') continue;
            str_copy(ed->map_list[ed->map_count], e->d_name, 64);
            size_t ll = strlen(ed->map_list[ed->map_count]);
            if (ll > 4) ed->map_list[ed->map_count][ll - 4] = 0;
            ed->map_count++;
        }
        closedir(d);
    }
    for (int i = 0; i < ed->map_count; i++)
        for (int j = i + 1; j < ed->map_count; j++)
            if (strcmp(ed->map_list[j], ed->map_list[i]) < 0) {
                char t[64]; str_copy(t, ed->map_list[i], 64);
                str_copy(ed->map_list[i], ed->map_list[j], 64);
                str_copy(ed->map_list[j], t, 64);
            }
}

bool editor_init(Editor* ed, World* world, int w, int h) {
    memset(ed, 0, sizeof(*ed));
    ed->world = world;
    ed->w = w; ed->h = h;
    ed->cam.pos = v3(0, 14, 36);
    ed->cam.yaw = 0;
    ed->cam.pitch = -0.28f;
    ed->cam.fov = 70;
    camera_update(&ed->cam);
    ed->selected = -1;
    ed->mode = 1;
    ed->place_type = ACT_BRUSH;
    ed->place_sub = 0;
    ed->placing = false;
    ed->grid = 2.0f;
    ed->snap = true;
    ed->show_grid = true;
    ed->show_helpers = true;
    ed->cam_speed = 14.0f;
    ed->undo = calloc(ED_UNDO_MAX, sizeof(EdUndoState));
    ed->undo_count = ed->undo_pos = 0;
    str_copy(ed->map_name, world->name, sizeof(ed->map_name));
    editor_refresh_maps(ed);
    editor_set_status(ed, "Editor initialized — %s (%d actors)", world->name, world->count);
    return ed->undo != NULL;
}

void editor_shutdown(Editor* ed) {
    free(ed->undo);
    ed->undo = NULL;
}

/* ------------------------------------------------------------------ */
static void ed_viewproj(Editor* ed, Mat4* out_vp) {
    Camera* c = &ed->cam;
    Mat4 view = mat4_look_at(c->pos, v3add(c->pos, c->fwd), v3(0, 1, 0));
    Mat4 proj = mat4_perspective(c->fov * DEG2RAD, (float)ed->vw / fmaxf((float)ed->vh, 1.0f), 0.06f, 400.0f);
    *out_vp = mat4_mul(proj, view);
}
static bool ed_project(Editor* ed, Vec3 p, float* sx, float* sy, float* depth) {
    Mat4 vp; ed_viewproj(ed, &vp);
    float x = p.x, y = p.y, z = p.z;
    float cx = vp.m[0] * x + vp.m[4] * y + vp.m[8] * z + vp.m[12];
    float cy = vp.m[1] * x + vp.m[5] * y + vp.m[9] * z + vp.m[13];
    float cw = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
    if (cw < 0.02f) return false;
    float nx = cx / cw, ny = cy / cw;
    *sx = ed->vx + (nx * 0.5f + 0.5f) * ed->vw;
    *sy = ed->vy + (1.0f - (ny * 0.5f + 0.5f)) * ed->vh;
    if (depth) *depth = cw;
    return true;
}
static Ray ed_mouse_ray(Editor* ed, float mx, float my) {
    return camera_ray(ed->cam.pos, ed->cam.fwd, ed->cam.right, ed->cam.up,
                      ed->cam.fov * DEG2RAD, (float)ed->vw / fmaxf((float)ed->vh, 1.0f),
                      mx - ed->vx, my - ed->vy, (float)ed->vw, (float)ed->vh);
}

static int pick_actor(Editor* ed, Ray r, float maxdist) {
    int best = -1; float bestt = maxdist;
    for (int i = 0; i < ed->world->count; i++) {
        Actor* a = &ed->world->actors[i];
        if (a->type == ACT_BRUSH || a->type == ACT_DECOR || a->type == ACT_STATICMESH) {
            AABB b = world_actor_aabb(ed->world, a);
            b.min = v3sub(b.min, v3s(0.02f));
            b.max = v3add(b.max, v3s(0.02f));
            float t;
            if (ray_aabb(r, b, &t, NULL) && t >= 0 && t < bestt) { bestt = t; best = i; }
        } else {
            float rad = fmaxf(0.45f, fmaxf(a->size.x, fmaxf(a->size.y, a->size.z)) * 0.5f);
            float t = ray_sphere(r.origin, r.dir, a->pos, rad);
            if (t >= 0 && t < bestt) { bestt = t; best = i; }
        }
    }
    return best;
}

static float gizmo_len(Editor* ed) {
    if (ed->selected < 0) return 2.0f;
    float d = v3len(v3sub(ed->world->actors[ed->selected].pos, ed->cam.pos));
    return clampf(d * 0.13f, 1.0f, 8.0f);
}
static int pick_axis(Editor* ed, float mx, float my) {
    if (ed->selected < 0) return -1;
    Actor* a = &ed->world->actors[ed->selected];
    float len = gizmo_len(ed);
    Vec3 axes[3] = { v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1) };
    int best = -1; float bestd = 11.0f;
    for (int i = 0; i < 3; i++) {
        Vec3 p0 = a->pos, p1 = v3add(a->pos, v3mul(axes[i], len));
        float x0, y0, x1, y1, d0, d1;
        if (!ed_project(ed, p0, &x0, &y0, &d0) || !ed_project(ed, p1, &x1, &y1, &d1)) continue;
        Vec2 ap = v2(x0, y0), bp = v2(x1, y1), mp = v2(mx, my);
        float dist = dist_point_line(v3(mp.x, mp.y, 0), v3(ap.x, ap.y, 0), v3(bp.x, bp.y, 0));
        /* only near the visible part */
        Vec2 ab = v2sub(bp, ap);
        float tproj = v2dot(v2sub(mp, ap), ab) / fmaxf(v2dot(ab, ab), 1e-4f);
        if (tproj < -0.1f || tproj > 1.15f) continue;
        if (dist < bestd) { bestd = dist; best = i; }
    }
    return best;
}
static void snap_vec(Editor* ed, Vec3* p) {
    if (!ed->snap) return;
    float g = ed->grid > 0.01f ? ed->grid : 1.0f;
    p->x = roundf(p->x / g) * g;
    p->y = roundf(p->y / g) * g;
    p->z = roundf(p->z / g) * g;
}

/* Jump the editor camera to an actor and look directly at it.  Used by the
 * outliner (single click selects, double click focuses). */
static void editor_focus_actor(Editor* ed, int idx) {
    if (!ed || idx < 0 || idx >= ed->world->count) return;
    Actor* a = &ed->world->actors[idx];
    AABB b = world_actor_aabb(ed->world, a);
    Vec3 c = aabb_center(b);
    Vec3 sz = aabb_size(b);
    float rad = fmaxf(1.0f, 0.5f * sqrtf(sz.x * sz.x + sz.y * sz.y + sz.z * sz.z));
    Vec3 dir = ed->cam.fwd;
    dir.y = clampf(dir.y, -0.55f, 0.55f);
    if (v3len(dir) < 0.01f) dir = v3(0.0f, -0.18f, -1.0f);
    dir = v3norm(dir);
    float dist = rad * 2.6f + 5.0f;
    Vec3 pos = v3sub(c, v3mul(dir, dist));
    if (pos.y < c.y + 1.2f) pos.y = c.y + 1.2f;
    Vec3 look = v3sub(c, pos);
    if (v3len(look) < 0.001f) look = v3(0, 0, -1);
    look = v3norm(look);
    ed->cam.pos = pos;
    ed->cam.yaw = atan2f(look.x, -look.z);
    ed->cam.pitch = clampf(asinf(clampf(look.y, -1.0f, 1.0f)), -1.52f, 1.52f);
    camera_update(&ed->cam);
    ed->selected = idx;
    editor_set_status(ed, "FOCUS: %s (%s)  X %.1f  Y %.1f  Z %.1f",
                      a->name[0] ? a->name : actor_type_name(a->type),
                      actor_type_name(a->type), a->pos.x, a->pos.y, a->pos.z);
}

static void place_actor(Editor* ed, Ray ray) {
    Actor a;
    world_actor_defaults(&a, ed->place_type, ed->place_sub);
    RayHit h;
    Vec3 p = v3(0, 0, 0);
    bool got = world_raycast(ed->world, ray, 200.0f, true, &h);
    if (got && h.normal.y > 0.4f) p = h.point;
    else if (got) p = h.point;
    else {
        /* intersect y=0 plane */
        if (fabsf(ray.dir.y) > 1e-4f) {
            float t = -ray.origin.y / ray.dir.y;
            if (t > 0 && t < 300) p = v3add(ray.origin, v3mul(ray.dir, t));
        }
    }
    if (ed->place_type == ACT_LIGHT) {
        p.y += 3.0f;
    } else if (ed->place_type == ACT_PLAYER_START) {
        p.y += 0.08f;
    } else if (ed->place_type == ACT_PICKUP) {
        p.y += 0.9f;
    } else if (ed->place_type == ACT_JUMPPAD) {
        p.y += 0.12f;
    } else if (ed->place_type == ACT_TRIGGER) {
        p.y += a.size.y * 0.5f;
    } else {
        p.y += a.size.y * 0.5f;
    }
    snap_vec(ed, &p);
    a.pos = p;
    push_undo(ed);
    int idx = world_add(ed->world, a);
    if (idx >= 0) {
        ed->selected = idx;
        editor_set_status(ed, "Placed %s", actor_type_name(a.type));
    }
}

void editor_get_camera(Editor* ed, Camera* cam) {
    if (ed->pie_active) {
        game_get_camera(&ed->pie_game, cam);
    } else {
        *cam = ed->cam;
    }
}

void editor_play(Editor* ed) {
    if (ed->pie_active) return;
    memcpy(&ed->pie_world, ed->world, sizeof(World));
    game_init(&ed->pie_game, &ed->pie_world, false, 3);
    ed->pie_game.players[0].bot = false;
    ed->pie_game.players[0].name[0] = 0;
    str_copy(ed->pie_game.players[0].name, "EditorPlayer", sizeof(ed->pie_game.players[0].name));
    game_reset(&ed->pie_game);
    ed->pie_game.networked = false;
    ed->pie_active = true;
    ed->pie_stop_request = false;
    ed->cam.yaw = ed->pie_game.players[0].yaw;
    ed->cam.pitch = 0;
    editor_set_status(ed, "Play in Editor started — ESC to stop");
    audio_music(MUS_GAME);
}

void editor_stop(Editor* ed) {
    if (!ed->pie_active) return;
    ed->pie_active = false;
    ed->pie_stop_request = false;
    editor_set_status(ed, "Play in Editor stopped — world unchanged");
    audio_music(MUS_EDITOR);
    /* game state is on the stack/heap inside editor; free nothing */
}

/* ------------------------------------------------------------------ */
void editor_update(Editor* ed, float dt, const EdInput* in, int w, int h) {
    ed->w = w; ed->h = h;
    ed->vx = 250; ed->vy = 66;
    ed->vw = w - 250 - 340;
    ed->vh = h - 66 - 26;
    if (ed->vw < 100) ed->vw = 100;
    if (ed->vh < 100) ed->vh = 100;
    if (ed->status_t > 0) ed->status_t -= dt;
    ed->ui_time += dt;

    if (ed->pie_active) {
        game_update(&ed->pie_game, dt);
        Player* p = &ed->pie_game.players[0];
        PlayerInput pin;
        memset(&pin, 0, sizeof(pin));
        float fwd = (in->kf ? 1.0f : 0) - (in->kb ? 1.0f : 0);
        float side = (in->kr ? 1.0f : 0) - (in->kl ? 1.0f : 0);
        pin.move_z = fwd;
        pin.move_x = side;
        Camera gc; game_get_camera(&ed->pie_game, &gc);
        pin.yaw = gc.yaw + in->dx * 0.0022f;
        pin.pitch = clampf(gc.pitch - in->dy * 0.0022f, -1.4f, 1.4f);
        pin.jump = in->ku;
        pin.crouch = in->kd;
        pin.fire = in->left;
        pin.alt_fire = in->right;
        pin.dodge = in->space;
        pin.switch_weapon = -1;
        p->input = pin;
        p->bot = false;
        if (in->escape || ed->pie_stop_request) editor_stop(ed);
        /* camera follows player yaw immediately to avoid drift */
        ed->cam.yaw = pin.yaw;
        ed->cam.pitch = pin.pitch;
        return;
    }

    /* --- look --- */
    if (in->right) {
        ed->cam.yaw += in->dx * 0.0032f;
        ed->cam.pitch -= in->dy * 0.0032f;
        ed->cam.pitch = clampf(ed->cam.pitch, -1.52f, 1.52f);
    }
    if (ed->top_view) ed->cam.pitch = -1.52f;
    bool over_viewport = in->mx >= ed->vx && in->mx <= ed->vx + ed->vw &&
                         in->my >= ed->vy && in->my <= ed->vy + ed->vh;
    if (in->wheel != 0 && !ed->top_view && over_viewport) {
        ed->cam.pos = v3add(ed->cam.pos, v3mul(ed->cam.fwd, in->wheel * 1.6f));
    }

    /* --- fly --- */
    float spd = ed->cam_speed;
    if (in->fast) spd *= 3.2f;
    Vec3 f = ed->cam.fwd, r = ed->cam.right;
    if (ed->top_view) { f = v3(0, 0, -1); r = v3(1, 0, 0); }
    Vec3 mv = v3(0, 0, 0);
    if (in->kf) mv = v3add(mv, f);
    if (in->kb) mv = v3sub(mv, f);
    if (in->kr) mv = v3add(mv, r);
    if (in->kl) mv = v3sub(mv, r);
    if (in->ku) mv.y += 1;
    if (in->kd) mv.y -= 1;
    if (v3len(mv) > 0.01f) ed->cam.pos = v3add(ed->cam.pos, v3mul(v3norm(mv), spd * dt));
    camera_update(&ed->cam);

    /* --- hover / picking --- */
    Ray ray = ed_mouse_ray(ed, in->mx, in->my);
    ed->cursor_valid = false;
    RayHit gh;
    if (world_raycast(ed->world, ray, 250.0f, true, &gh)) { ed->cursor_world = gh.point; ed->cursor_valid = true; }
    else if (fabsf(ray.dir.y) > 1e-4f) {
        float t = -ray.origin.y / ray.dir.y;
        if (t > 0) { ed->cursor_world = v3add(ray.origin, v3mul(ray.dir, t)); ed->cursor_valid = true; }
    }
    bool in_view = over_viewport;
    ed->hover_actor = -1;
    ed->hover_axis = -1;
    if (in_view) {
        ed->hover_actor = pick_actor(ed, ray, 200.0f);
        if (ed->selected >= 0 && !ed->placing) ed->hover_axis = pick_axis(ed, in->mx, in->my);
        ed->gizmo_hover = ed->hover_axis >= 0;
    } else ed->gizmo_hover = false;

    /* --- drag transform --- */
    if (ed->dragging) {
        Actor* a = &ed->world->actors[ed->selected];
        Vec3 axes[3] = { v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1) };
        float x0, y0, x1, y1;
        ed_project(ed, a->pos, &x0, &y0, NULL);
        ed_project(ed, v3add(ed->drag_origin, v3mul(axes[ed->drag_axis], gizmo_len(ed))), &x1, &y1, NULL);
        float sx = x1 - x0, sy = y1 - y0;
        float l2 = sx * sx + sy * sy;
        float px = in->mx - ed->drag_mx, py = in->my - ed->drag_my;
        float k = l2 > 1e-5f ? (px * sx + py * sy) / l2 : 0;
        float worldk = k * gizmo_len(ed);
        if (ed->mode == 1) {
            Vec3 p = v3add(ed->drag_start_pos, v3mul(axes[ed->drag_axis], worldk));
            if (ed->snap) {
                float g = ed->grid;
                if (ed->drag_axis == 0) p.x = roundf(p.x / g) * g;
                if (ed->drag_axis == 1) p.y = roundf(p.y / g) * g;
                if (ed->drag_axis == 2) p.z = roundf(p.z / g) * g;
            }
            a->pos = p;
        } else if (ed->mode == 3) {
            Vec3 s = ed->drag_start_size;
            if (ed->drag_axis == 0) s.x = fmaxf(0.2f, s.x + worldk * 2.0f);
            if (ed->drag_axis == 1) s.y = fmaxf(0.2f, s.y + worldk * 2.0f);
            if (ed->drag_axis == 2) s.z = fmaxf(0.2f, s.z + worldk * 2.0f);
            if (ed->snap) {
                s.x = roundf(s.x / (ed->grid * 0.5f)) * (ed->grid * 0.5f);
                s.y = roundf(s.y / (ed->grid * 0.5f)) * (ed->grid * 0.5f);
                s.z = roundf(s.z / (ed->grid * 0.5f)) * (ed->grid * 0.5f);
            }
            a->size = s;
        } else if (ed->mode == 2) {
            a->yaw = ed->drag_start_yaw + (px + py) * 0.45f;
            if (ed->snap) a->yaw = roundf(a->yaw / 15.0f) * 15.0f;
        }
        if (in->left_released || !in->left) ed->dragging = false;
    }

    /* --- click --- */
    if (in->left_pressed) {
        if (in_view) {
            if (ed->selected >= 0 && ed->hover_axis >= 0 && !ed->placing) {
                push_undo(ed);
                ed->dragging = true;
                ed->drag_axis = ed->hover_axis;
                ed->drag_origin = ed->world->actors[ed->selected].pos;
                ed->drag_start_pos = ed->world->actors[ed->selected].pos;
                ed->drag_start_size = ed->world->actors[ed->selected].size;
                ed->drag_start_yaw = ed->world->actors[ed->selected].yaw;
                ed->drag_mx = in->mx; ed->drag_my = in->my;
                /* keep placement of gizmo stable */
            } else if (ed->placing) {
                place_actor(ed, ray);
                if (!in->ctrl) ed->placing = false;
            } else if (ed->hover_actor >= 0) {
                ed->selected = ed->hover_actor;
                editor_set_status(ed, "Selected %s", ed->world->actors[ed->selected].name);
            } else {
                ed->selected = -1;
            }
        } else {
            /* clicks outside viewport are UI, don't deselect */
        }
    }

    /* --- keys --- */
    if (in->del && ed->selected >= 0) {
        push_undo(ed);
        editor_set_status(ed, "Deleted %s", ed->world->actors[ed->selected].name);
        world_delete(ed->world, ed->selected);
        ed->selected = -1;
    }
    if (in->duplicate && ed->selected >= 0 && ed->selected < ed->world->count) {
        push_undo(ed);
        Actor a = ed->world->actors[ed->selected];
        a.pos.x += ed->grid;
        a.pos.z += ed->grid;
        snprintf(a.name, MAX_NAME, "%s_copy", ed->world->actors[ed->selected].name);
        int ni = world_add(ed->world, a);
        if (ni >= 0) ed->selected = ni;
        editor_set_status(ed, "Duplicated actor");
    }
    if (in->undo && !in->ctrl) do_undo(ed);
    if (in->redo) do_redo(ed);
    if (in->escape) {
        if (ed->placing) { ed->placing = false; editor_set_status(ed, "Placement cancelled"); }
        else if (ed->selected >= 0) ed->selected = -1;
    }
    if (in->play) {
        if (ed->pie_active) editor_stop(ed); else editor_play(ed);
    }
    /* sync map name */
    if (ed->map_name[0] == 0) str_copy(ed->map_name, ed->world->name, sizeof(ed->map_name));
}

/* ------------------------------------------------------------------ */
void editor_draw_3d(Editor* ed, Renderer* r) {
    if (ed->pie_active) return;
    lines_begin(r);

    /* grid */
    if (ed->show_grid) {
        float ext = 42.0f;
        float g = ed->grid > 0.1f ? ed->grid : 1.0f;
        int n = (int)(ext / g);
        float y = 0.015f;
        for (int i = -n; i <= n; i++) {
            float p = i * g;
            bool major = (i % 5 == 0);
            Color c = major ? col(0.30f, 0.34f, 0.40f, 0.55f) : col(0.19f, 0.22f, 0.27f, 0.38f);
            line_add(r, v3(p, y, -ext), v3(p, y, ext), c);
            line_add(r, v3(-ext, y, p), v3(ext, y, p), c);
        }
        line_add(r, v3(-ext, y + 0.01f, 0), v3(ext, y + 0.01f, 0), col(0.85f, 0.22f, 0.20f, 0.85f));
        line_add(r, v3(0, y + 0.01f, -ext), v3(0, y + 0.01f, ext), col(0.25f, 0.45f, 0.95f, 0.85f));
    }
    /* helpers */
    if (ed->show_helpers) {
        for (int i = 0; i < ed->world->count; i++) {
            Actor* a = &ed->world->actors[i];
            bool sel = (i == ed->selected);
            Color c = col(0.35f, 0.62f, 0.95f, 0.55f);
            if (a->type == ACT_LIGHT) {
                c = col(a->color.r, a->color.g, a->color.b, 0.85f);
                line_add(r, v3(a->pos.x - 0.5f, a->pos.y, a->pos.z), v3(a->pos.x + 0.5f, a->pos.y, a->pos.z), c);
                line_add(r, v3(a->pos.x, a->pos.y - 0.5f, a->pos.z), v3(a->pos.x, a->pos.y + 0.5f, a->pos.z), c);
                line_add(r, v3(a->pos.x, a->pos.y, a->pos.z - 0.5f), v3(a->pos.x, a->pos.y, a->pos.z + 0.5f), c);
                line_circle(r, a->pos, v3(0, 1, 0), 0.75f, col(c.r, c.g, c.b, 0.5f), 16);
            } else if (a->type == ACT_PLAYER_START) {
                c = col(0.30f, 0.95f, 0.45f, 0.85f);
                line_circle(r, v3(a->pos.x, a->pos.y + 0.05f, a->pos.z), v3(0, 1, 0), 0.85f, c, 20);
                Vec3 dir = v3(sinf(a->yaw * DEG2RAD), 0, -cosf(a->yaw * DEG2RAD));
                line_add(r, a->pos, v3add(a->pos, v3mul(dir, 1.6f)), c);
                line_add(r, v3add(a->pos, v3mul(dir, 1.6f)), v3add(a->pos, v3add(v3mul(dir, 1.1f), v3(0.25f, 0.0f, 0))), c);
                line_add(r, v3add(a->pos, v3mul(dir, 1.6f)), v3add(a->pos, v3add(v3mul(dir, 1.1f), v3(-0.25f, 0.0f, 0))), c);
            } else if (a->type == ACT_PICKUP) {
                c = col(a->color.r, a->color.g, a->color.b, 0.9f);
                line_box(r, aabb_center_size(a->pos, v3(0.75f, 0.75f, 0.75f)), col(c.r, c.g, c.b, sel ? 1.0f : 0.45f));
                line_add(r, a->pos, v3add(a->pos, v3(0, 2.2f, 0)), col(c.r, c.g, c.b, 0.25f));
            } else if (a->type == ACT_JUMPPAD) {
                c = col(a->color.r, a->color.g, a->color.b, 0.9f);
                line_box(r, aabb_center_size(a->pos, v3(2.2f, 0.2f, 2.2f)), col(c.r, c.g, c.b, 0.8f));
                Vec3 dir = v3(sinf(a->yaw * DEG2RAD), 0, -cosf(a->yaw * DEG2RAD));
                Vec3 tip = v3add(a->pos, v3add(v3mul(dir, 2.4f), v3(0, 0.8f, 0)));
                line_add(r, a->pos, tip, c);
                line_add(r, tip, v3add(a->pos, v3add(v3mul(dir, 1.7f), v3(0, 0.8f, 0))), c);
            } else if (a->type == ACT_LAVA) {
                line_box(r, world_actor_aabb(ed->world, a), col(1.0f, 0.35f, 0.08f, 0.45f));
            } else if (a->type == ACT_NAV_POINT) {
                c = col(0.35f, 0.90f, 1.0f, 0.9f);
                line_circle(r, a->pos, v3(0, 1, 0), 0.55f, c, 16);
                line_add(r, v3add(a->pos, v3(0, -0.5f, 0)), v3add(a->pos, v3(0, 1.4f, 0)), c);
                line_add(r, v3add(a->pos, v3(-0.4f, 0.9f, 0)), v3add(a->pos, v3(0.4f, 0.9f, 0)), c);
                line_add(r, v3add(a->pos, v3(0, 0.9f, -0.4f)), v3add(a->pos, v3(0, 0.9f, 0.4f)), c);
            } else if (a->type == ACT_TRIGGER) {
                c = col(0.25f, 0.95f, 0.45f, 0.8f);
                line_box(r, world_actor_aabb(ed->world, a), col(c.r, c.g, c.b, sel ? 1.0f : 0.45f));
                Vec3 top = v3add(a->pos, v3(0, a->size.y * 0.5f + 0.3f, 0));
                line_add(r, a->pos, top, col(c.r, c.g, c.b, 0.55f));
            } else if (sel) {
                line_box(r, world_actor_aabb(ed->world, a), col(1.0f, 0.62f, 0.16f, 0.8f));
            }
        }
    }
    /* selection */
    if (ed->selected >= 0 && ed->selected < ed->world->count) {
        Actor* a = &ed->world->actors[ed->selected];
        AABB b = world_actor_aabb(ed->world, a);
        line_box_corners(r, b, col(1.0f, 0.66f, 0.18f, 1.0f), 0.9f);
        /* gizmo */
        if (!ed->placing) {
            float len = gizmo_len(ed);
            Vec3 axes[3] = { v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1) };
            Color cols[3] = { col(0.95f, 0.22f, 0.20f, 1.0f), col(0.30f, 0.95f, 0.35f, 1.0f), col(0.25f, 0.50f, 1.0f, 1.0f) };
            for (int i = 0; i < 3; i++) {
                bool hot = (ed->hover_axis == i || (ed->dragging && ed->drag_axis == i));
                Color c = hot ? col(1.0f, 0.90f, 0.35f, 1.0f) : cols[i];
                Vec3 tip = v3add(a->pos, v3mul(axes[i], len));
                line_add(r, a->pos, tip, c);
                if (ed->mode == 1 || ed->mode == 3) {
                    float s = len * 0.10f;
                    Vec3 u = i == 1 ? v3(1, 0, 0) : v3(0, 1, 0);
                    Vec3 v = i == 2 ? v3(0, 1, 0) : v3(0, 0, 1);
                    line_add(r, tip, v3add(v3sub(tip, v3mul(axes[i], s)), v3mul(u, s * 0.4f)), c);
                    line_add(r, tip, v3add(v3sub(tip, v3mul(axes[i], s)), v3mul(u, -s * 0.4f)), c);
                    line_add(r, tip, v3add(v3sub(tip, v3mul(axes[i], s)), v3mul(v, s * 0.4f)), c);
                    line_add(r, tip, v3add(v3sub(tip, v3mul(axes[i], s)), v3mul(v, -s * 0.4f)), c);
                } else {
                    line_circle(r, tip, axes[i], len * 0.14f, c, 18);
                }
            }
        }
    }
    lines_render(r, true, 1.4f);
}

/* ------------------------------------------------------------------ */
/* content browser palette                                             */
typedef struct { const char* label; int type; int sub; int icon; } PaletteEntry;
static const PaletteEntry PALETTE[] = {
    { "Static Brush (Box)", ACT_BRUSH, 0, ICON_CUBE },
    { "Ramp / Sloped Brush", ACT_BRUSH, 1, ICON_CUBE },
    { "Cylinder Brush", ACT_BRUSH, 2, ICON_CUBE },
    { "Static Mesh (Box)", ACT_STATICMESH, 0, ICON_CUBE },
    { "Trigger Volume / Event", ACT_TRIGGER, 0, ICON_LAVA },
    { "Decor Barrel", ACT_DECOR, 0, ICON_CUBE },
    { "Point Light", ACT_LIGHT, 0, ICON_LIGHT },
    { "Player Start", ACT_PLAYER_START, 0, ICON_SPAWN },
    { "Nav Point (bot hint)", ACT_NAV_POINT, 0, ICON_SPAWN },
    { "Jump Pad", ACT_JUMPPAD, 0, ICON_JUMP },
    { "Lava Volume", ACT_LAVA, 0, ICON_LAVA },
    { "Health Vial", ACT_PICKUP, PICK_HEALTH_SMALL, ICON_PICKUP },
    { "Health Pack", ACT_PICKUP, PICK_HEALTH_BIG, ICON_PICKUP },
    { "Shield Small", ACT_PICKUP, PICK_ARMOR_SMALL, ICON_PICKUP },
    { "Shield Pack", ACT_PICKUP, PICK_ARMOR_BIG, ICON_PICKUP },
    { "Ammo", ACT_PICKUP, PICK_AMMO, ICON_PICKUP },
    { "Assault Rifle", ACT_PICKUP, PICK_WEAPON_AR, ICON_PICKUP },
    { "Shock Rifle", ACT_PICKUP, PICK_WEAPON_SHOCK, ICON_PICKUP },
    { "Rocket Launcher", ACT_PICKUP, PICK_WEAPON_ROCKET, ICON_PICKUP },
    { "Flak Cannon", ACT_PICKUP, PICK_WEAPON_FLAK, ICON_PICKUP },
    { "Lightning Gun", ACT_PICKUP, PICK_WEAPON_LIGHTNING, ICON_PICKUP },
};
static const int PALETTE_COUNT = (int)(sizeof(PALETTE) / sizeof(PALETTE[0]));

static void label_row(Ui* ui, float x, float y, float w, const char* text, Color c) {
    ui_text(ui, x, y, text, 10, c);
    ui_rect(ui, x, y + 14, w, 1, col(0.20f, 0.23f, 0.27f, 0.8f));
}

void editor_draw_ui(Editor* ed, Ui* ui, int w, int h) {
    ed->w = w; ed->h = h;
    ed->vx = 250; ed->vy = 66;
    ed->vw = w - 250 - 340;
    ed->vh = h - 66 - 26;
    if (ed->vw < 100) ed->vw = 100;
    if (ed->vh < 100) ed->vh = 100;

    /* ================= TOP BAR ================= */
    ui_rect(ui, 0, 0, (float)w, 34, col(0.030f, 0.036f, 0.045f, 1));
    ui_rect(ui, 0, 33, (float)w, 1, col(0.24f, 0.27f, 0.32f, 1));
    ui_rect(ui, 14, 9, 16, 16, C_ACCENT);
    ui_tri(ui, v2(18, 13), v2(18, 22), v2(27, 17.5f), col(0.03f, 0.035f, 0.05f, 1));
    ui_text(ui, 40, 8, "ARENA 2004", 15, C_WHITE);
    ui_text(ui, 40 + ui_text_width(ui, "ARENA 2004", 15) + 10, 11, "WORLD EDITOR", 10, C_ACCENT);
    char stats[128];
    snprintf(stats, sizeof(stats), "%s   |   %d actors   |   %s", ed->world->name, ed->world->count, ed->dirty ? "MODIFIED" : "SAVED");
    ui_text_right(ui, (float)w - 16, 11, stats, 11, ed->dirty ? C_ACCENT : C_DIM);

    /* menu row */
    ui_rect(ui, 0, 34, (float)w, 32, col(0.055f, 0.064f, 0.076f, 1));
    ui_rect(ui, 0, 65, (float)w, 1, col(0.20f, 0.23f, 0.27f, 1));
    const char* menus[] = { "FILE", "EDIT", "VIEW", "BUILD", "HELP" };
    float mx = 16;
    for (int i = 0; i < 5; i++) {
        ui_text(ui, mx, 8 + 34, menus[i], 10, C_DIM);
        mx += ui_text_width(ui, menus[i], 10) + 26;
    }
    /* toolbar buttons */
    float bx = 250;
    if (ui_icon_button(ui, "ed.new", ICON_FOLDER, bx, 38, 26, 24, false)) {
        World* wd = ed->world;
        world_init(wd);
        str_copy(wd->name, "Untitled", sizeof(wd->name));
        Actor a; world_actor_defaults(&a, ACT_BRUSH, 0);
        a.pos = v3(0, -0.5f, 0); a.size = v3(80, 1, 80); a.material = 4; a.color = col3(0.30f, 0.31f, 0.33f);
        world_add(wd, a);
        for (int i = 0; i < 4; i++) {
            Actor l; world_actor_defaults(&l, ACT_LIGHT, 0);
            l.pos = v3((i & 1 ? 12.0f : -12.0f), 10, (i & 2 ? 12.0f : -12.0f));
            l.power = 26; l.extra = 1.6f; l.color = col3(0.6f, 0.75f, 1.0f);
            world_add(wd, l);
        }
        for (int i = 0; i < 4; i++) {
            Actor s; world_actor_defaults(&s, ACT_PLAYER_START, 0);
            s.pos = v3((i & 1 ? 14.0f : -14.0f), 0.08f, (i & 2 ? 14.0f : -14.0f));
            world_add(wd, s);
        }
        ed->selected = -1; ed->undo_count = ed->undo_pos = 0; ed->dirty = true;
        str_copy(ed->map_name, "Untitled", sizeof(ed->map_name));
        editor_set_status(ed, "New empty map created");
    }
    bx += 30;
    if (ui_icon_button(ui, "ed.open", ICON_FOLDER, bx, 38, 26, 24, false)) {
        editor_refresh_maps(ed);
        ed->browser_tab = 2;
        editor_set_status(ed, "Map browser — select a map and press LOAD");
    }
    bx += 30;
    if (ui_icon_button(ui, "ed.save", ICON_SAVE, bx, 38, 26, 24, false)) {
        str_copy(ed->world->name, ed->map_name, sizeof(ed->world->name));
        char path[128]; snprintf(path, sizeof(path), "maps/%s.map", ed->map_name);
        if (world_save(ed->world, path)) { ed->dirty = false; editor_set_status(ed, "Saved %s", path); editor_refresh_maps(ed); }
        else editor_set_status(ed, "Save failed: %s", path);
    }
    bx += 38;
    if (ui_icon_button(ui, "ed.undo", ICON_UNDO, bx, 38, 26, 24, false)) do_undo(ed);
    bx += 30;
    if (ui_icon_button(ui, "ed.redo", ICON_REDO, bx, 38, 26, 24, false)) do_redo(ed);
    bx += 38;
    if (ui_button(ui, "ed.play", ed->pie_active ? "STOP (PIE)" : "PLAY IN EDITOR", bx, 38, ed->pie_active ? 110 : 128, 24, ed->pie_active ? 2 : 1)) {
        if (ed->pie_active) editor_stop(ed); else editor_play(ed);
    }
    bx += (ed->pie_active ? 118 : 136);
    /* tool buttons */
    if (ui_icon_button(ui, "ed.sel", ICON_SELECT, bx, 38, 28, 24, ed->mode == 0)) { ed->mode = 0; ed->placing = false; }
    bx += 31;
    if (ui_icon_button(ui, "ed.mov", ICON_MOVE, bx, 38, 28, 24, ed->mode == 1)) { ed->mode = 1; ed->placing = false; }
    bx += 31;
    if (ui_icon_button(ui, "ed.rot", ICON_ROTATE, bx, 38, 28, 24, ed->mode == 2)) { ed->mode = 2; ed->placing = false; }
    bx += 31;
    if (ui_icon_button(ui, "ed.scl", ICON_SCALE, bx, 38, 28, 24, ed->mode == 3)) { ed->mode = 3; ed->placing = false; }
    bx += 38;
    if (ui_icon_button(ui, "ed.grid", ICON_GRID, bx, 38, 28, 24, ed->show_grid)) ed->show_grid = !ed->show_grid;
    bx += 31;
    if (ui_button(ui, "ed.snap", ed->snap ? "SNAP" : "FREE", bx, 38, 52, 24, ed->snap ? 1 : 0)) ed->snap = !ed->snap;
    bx += 57;
    if (ui_button(ui, "ed.top", ed->top_view ? "TOP" : "PERSP", bx, 38, 58, 24, ed->top_view ? 1 : 0)) ed->top_view = !ed->top_view;
    bx += 62;
    /* map name */
    float namew = fminf(210.0f, w - bx - 24);
    if (namew > 80) {
        ui_text(ui, bx, 43, "MAP", 9, C_DIM);
        ui_textbox(ui, "ed.mapname", bx + 34, 38, namew - 34, 24, ed->map_name, sizeof(ed->map_name), "MapName");
    }

    /* ================= LEFT PANEL ================= */
    float lx = 0, ly = 66, lw = 250, lh = (float)h - 66 - 26;
    ui_rect(ui, lx, ly, lw, lh, C_PANEL);
    ui_rect(ui, lx + lw - 1, ly, 1, lh, col(0.18f, 0.21f, 0.25f, 1));
    ui_rect(ui, lx, ly, lw, 28, C_HEADER);
    ui_text(ui, lx + 12, ly + 7, "CONTENT BROWSER", 11, C_WHITE);
    ui_text_right(ui, lx + lw - 10, ly + 8, "ARENA", 9, C_ACCENT);
    /* tabs */
    float ty = ly + 28;
    const char* tabs[] = { "ACTORS", "OUTLINER", "MAPS", "MATS" };
    for (int i = 0; i < 4; i++) {
        float tw = lw / 4.0f;
        bool sel = ed->browser_tab == i;
        ui_rect(ui, lx + i * tw, ty, tw, 24, sel ? col(0.14f, 0.20f, 0.28f, 1) : col(0.08f, 0.09f, 0.10f, 1));
        if (sel) ui_rect(ui, lx + i * tw, ty + 21, tw, 3, C_BLUE);
        ui_text_center(ui, lx + i * tw + tw * 0.5f, ty + 7, tabs[i], 9, sel ? C_WHITE : C_DIM);
        char id[32]; snprintf(id, sizeof(id), "ed.tab%d", i);
        if (ui_mouse_in(ui, lx + i * tw, ty, tw, 24) && ui->in.pressed) ed->browser_tab = i;
    }
    float cy = ty + 30;
    if (ed->browser_tab == 0) {
        ui_text(ui, lx + 12, cy, "PLACE ACTORS", 9, C_ACCENT);
        cy += 20;
        float row = 22;
        ui_push_clip(ui, (int)lx + 4, (int)cy, (int)lw - 8, (int)(lh - (cy - ly) - 34));
        for (int i = 0; i < PALETTE_COUNT; i++) {
            bool selplaced = ed->placing && ed->place_type == PALETTE[i].type &&
                             (PALETTE[i].type != ACT_BRUSH || ed->place_sub == PALETTE[i].sub) &&
                             (PALETTE[i].type != ACT_PICKUP || ed->place_sub == PALETTE[i].sub);
            bool hov = ui_mouse_in(ui, lx + 6, cy, lw - 12, row);
            ui_rect(ui, lx + 6, cy, lw - 12, row - 2, selplaced ? col(0.20f, 0.30f, 0.42f, 1) : (hov ? col(0.13f, 0.15f, 0.18f, 1) : col(0.075f, 0.085f, 0.10f, 0)));
            ui_icon(ui, PALETTE[i].icon, lx + 12, cy + 3, 15, selplaced ? C_ACCENT : C_DIM);
            ui_text(ui, lx + 34, cy + 4, PALETTE[i].label, 11, selplaced ? C_WHITE : C_TEXT);
            char cid[64]; snprintf(cid, sizeof(cid), "ed.pal%d", i);
            bool over = ui_mouse_in(ui, lx + 6, cy, lw - 12, row);
            if (over) { ui->mouse_over_ui = true; if (ui->in.pressed) { ui->active = 0xED0000u + i; } }
            bool clicked = over && ui->in.released && ui->active == 0xED0000u + i;
            if (over && ui->in.released) ui->active = 0;
            if (clicked) {
                ed->place_type = PALETTE[i].type;
                ed->place_sub = PALETTE[i].sub;
                ed->placing = true;
                ed->mode = 1;
                if (PALETTE[i].type == ACT_BRUSH) ed->world->actors[0].shape = ed->world->actors[0].shape; /* no-op */
                editor_set_status(ed, "Placing %s — click in viewport (CTRL to keep placing)", PALETTE[i].label);
            }
            cy += row;
        }
        ui_pop_clip(ui);
        cy = ly + lh - 28;
        ui_rect(ui, lx, cy, lw, 28, col(0.055f, 0.064f, 0.076f, 1));
        char ph[128]; snprintf(ph, sizeof(ph), "NEXT: %s", ed->placing ? actor_type_name(ed->place_type) : "Select an actor");
        ui_text(ui, lx + 10, cy + 8, ph, 10, ed->placing ? C_ACCENT : C_DIM);
    } else if (ed->browser_tab == 1) {
        ui_text(ui, lx + 12, cy, "WORLD OUTLINER", 9, C_ACCENT);
        cy += 20;
        float list_h = lh - (cy - ly) - 30;
        if (list_h < 24) list_h = 24;
        int rows = (int)(list_h / 24);
        int max_scroll = ed->world->count > rows ? ed->world->count - rows : 0;
        if (ui_mouse_in(ui, lx + 4, cy, lw - 8, list_h)) {
            ui->mouse_over_ui = true;
            if (ui->in.wheel != 0) ed->outline_scroll -= ui->in.wheel * 3;
        }
        ed->outline_scroll = (int)clampf((float)ed->outline_scroll, 0.0f, (float)max_scroll);
        ui_push_clip(ui, (int)lx + 4, (int)cy, (int)lw - 8, (int)list_h);
        for (int i = 0; i < ed->world->count; i++) {
            int vi = i - ed->outline_scroll;
            if (vi < 0 || vi >= rows) continue;
            Actor* a = &ed->world->actors[i];
            float ry2 = cy + vi * 24;
            bool hov = ui_mouse_in(ui, lx + 6, ry2, lw - 12, 24);
            bool sel = ed->selected == i;
            ui_rect(ui, lx + 6, ry2, lw - 12, 22, sel ? col(0.18f, 0.27f, 0.38f, 1) : (hov ? col(0.13f, 0.15f, 0.18f, 1) : col(0, 0, 0, 0)));
            int ic = ICON_CUBE;
            if (a->type == ACT_LIGHT) ic = ICON_LIGHT;
            else if (a->type == ACT_PLAYER_START || a->type == ACT_NAV_POINT) ic = ICON_SPAWN;
            else if (a->type == ACT_PICKUP) ic = ICON_PICKUP;
            else if (a->type == ACT_JUMPPAD) ic = ICON_JUMP;
            else if (a->type == ACT_LAVA) ic = ICON_LAVA;
            else if (a->type == ACT_TRIGGER) ic = ICON_LAVA;
            ui_icon(ui, ic, lx + 12, ry2 + 3, 14, sel ? C_ACCENT : C_DIM);
            char label[96]; snprintf(label, sizeof(label), "%s", a->name[0] ? a->name : actor_type_name(a->type));
            ui_text(ui, lx + 32, ry2 + 4, label, 10, sel ? C_WHITE : C_TEXT);
            if (hov) {
                ui->mouse_over_ui = true;
                if (ui->in.pressed) {
                    ed->selected = i;
                    if (ed->outline_last == i && ed->ui_time - ed->outline_click_t < 0.35f) {
                        editor_focus_actor(ed, i);
                        ed->outline_last = -1;
                    } else {
                        ed->outline_last = i;
                        ed->outline_click_t = ed->ui_time;
                    }
                }
            }
        }
        ui_pop_clip(ui);
        cy = ly + lh - 26;
        char oc[64]; snprintf(oc, sizeof(oc), "%d ACTORS  //  1-CLICK SELECT  2-CLICK FOCUS", ed->world->count);
        ui_text(ui, lx + 10, cy + 7, oc, 8, C_DIM);
    } else if (ed->browser_tab == 2) {
        ui_text(ui, lx + 12, cy, "MAPS", 9, C_ACCENT);
        cy += 20;
        ui_push_clip(ui, (int)lx + 4, (int)cy, (int)lw - 8, (int)(lh - (cy - ly) - 68));
        for (int i = 0; i < ed->map_count; i++) {
            bool hov = ui_mouse_in(ui, lx + 6, cy, lw - 12, 24);
            bool sel = ed->map_sel == i;
            ui_rect(ui, lx + 6, cy, lw - 12, 22, sel ? col(0.18f, 0.27f, 0.38f, 1) : (hov ? col(0.13f, 0.15f, 0.18f, 1) : col(0, 0, 0, 0)));
            ui_icon(ui, ICON_FOLDER, lx + 12, cy + 3, 14, sel ? C_ACCENT : C_DIM);
            ui_text(ui, lx + 32, cy + 4, ed->map_list[i], 11, sel ? C_WHITE : C_TEXT);
            if (hov && ui->in.pressed) ed->map_sel = i;
            if (hov && ui->in.released && ui->in.pressed) ed->map_sel = i;
            cy += 24;
        }
        ui_pop_clip(ui);
        cy = ly + lh - 62;
        if (ui_button(ui, "ed.load", "LOAD MAP", lx + 10, cy, lw - 20, 24, 1)) {
            if (ed->map_sel >= 0 && ed->map_sel < ed->map_count) {
                char path[128]; snprintf(path, sizeof(path), "maps/%s.map", ed->map_list[ed->map_sel]);
                if (world_load(ed->world, path)) {
                    ed->selected = -1; ed->undo_count = ed->undo_pos = 0;
                    str_copy(ed->map_name, ed->world->name, sizeof(ed->map_name));
                    ed->dirty = false;
                    editor_set_status(ed, "Loaded %s (%d actors)", path, ed->world->count);
                    ed->browser_tab = 0;
                } else editor_set_status(ed, "Failed to load %s", path);
            }
        }
        if (ui_button(ui, "ed.reload", "REFRESH LIST", lx + 10, cy + 28, lw - 20, 22, 0)) editor_refresh_maps(ed);
    } else {
        ui_text(ui, lx + 12, cy, "MATERIALS", 9, C_ACCENT);
        cy += 22;
        for (int i = 0; i < 16; i++) {
            float gw = (lw - 24) / 4.0f;
            float gx = lx + 12 + (i % 4) * gw;
            float gy = cy + (i / 4) * gw;
            char cid[48]; snprintf(cid, sizeof(cid), "ed.mat%d", i);
            bool sel = ed->selected >= 0 && ed->world->actors[ed->selected].material == i;
            if (ui_color_swatch(ui, cid, gx, gy, gw - 4, gw - 4, mat_color(i), sel)) {
                if (ed->selected >= 0) {
                    push_undo(ed);
                    ed->world->actors[ed->selected].material = i;
                    ed->world->actors[ed->selected].color = mat_color(i);
                    editor_set_status(ed, "Material: %s", material_name(i));
                }
            }
            ui_text(ui, gx + 2, gy + gw + 1, material_name(i), 7, C_DIM);
        }
        cy = ly + lh - 58;
        ui_text(ui, lx + 12, cy, "Material swatches apply to the", 9, C_DIM);
        ui_text(ui, lx + 12, cy + 13, "selected actor.", 9, C_DIM);
    }

    /* ================= RIGHT PANEL (DETAILS) ================= */
    float rx = (float)w - 340, ry = 66, rw = 340, rh = (float)h - 66 - 26;
    ui_rect(ui, rx, ry, rw, rh, C_PANEL);
    ui_rect(ui, rx, ry, 1, rh, col(0.18f, 0.21f, 0.25f, 1));
    ui_rect(ui, rx, ry, rw, 28, C_HEADER);
    ui_text(ui, rx + 12, ry + 7, "DETAILS", 11, C_WHITE);
    char selcount[64]; snprintf(selcount, sizeof(selcount), "%s", ed->selected >= 0 ? "1 SELECTED" : "NOTHING SELECTED");
    ui_text_right(ui, rx + rw - 10, ry + 8, selcount, 9, ed->selected >= 0 ? C_ACCENT : C_DIM);
    float dy = ry + 34;

    ui_push_clip(ui, (int)rx + 4, (int)ry + 28, (int)rw - 8, (int)rh - 32);
    if (ed->selected < 0 || ed->selected >= ed->world->count) {
        ui_text(ui, rx + 14, dy + 10, "No actor selected", 13, C_DIM);
        ui_text(ui, rx + 14, dy + 32, "Click an actor in the viewport", 10, C_DIM);
        ui_text(ui, rx + 14, dy + 46, "or place one from the content", 10, C_DIM);
        ui_text(ui, rx + 14, dy + 60, "browser on the left.", 10, C_DIM);
        ui_rect(ui, rx + 14, dy + 86, rw - 28, 1, col(0.2f, 0.23f, 0.27f, 1));
        ui_text(ui, rx + 14, dy + 98, "VIEWPORT", 9, C_ACCENT);
        const char* hints[] = {
            "RMB + drag   look around",
            "W A S D      fly",
            "Q / E        down / up",
            "Wheel        zoom",
            "LMB          select / place",
            "Gizmo drag   move / rotate / scale",
            "DEL          delete actor",
            "CTRL+D       duplicate",
            "CTRL+Z       undo",
        };
        for (int i = 0; i < 9; i++) ui_text(ui, rx + 14, dy + 118 + i * 16, hints[i], 10, C_TEXT);
        ui_text(ui, rx + 14, dy + 286, "PLAY IN EDITOR runs the map", 9, C_DIM);
        ui_text(ui, rx + 14, dy + 299, "with bots; ESC returns.", 9, C_DIM);
    } else {
        Actor* a = &ed->world->actors[ed->selected];
        /* header card */
        ui_rect(ui, rx + 10, dy, rw - 20, 52, col(0.09f, 0.11f, 0.13f, 1));
        ui_rect(ui, rx + 10, dy, 4, 52, C_ACCENT);
        int ic = ICON_CUBE;
        if (a->type == ACT_LIGHT) ic = ICON_LIGHT;
        else if (a->type == ACT_PLAYER_START || a->type == ACT_NAV_POINT) ic = ICON_SPAWN;
        else if (a->type == ACT_PICKUP) ic = ICON_PICKUP;
        else if (a->type == ACT_JUMPPAD) ic = ICON_JUMP;
        else if (a->type == ACT_LAVA) ic = ICON_LAVA;
        else if (a->type == ACT_TRIGGER) ic = ICON_LAVA;
        ui_icon(ui, ic, rx + 22, dy + 12, 28, C_ACCENT);
        char nm[64]; snprintf(nm, sizeof(nm), "%s", a->name);
        ui_text(ui, rx + 58, dy + 9, nm, 13, C_WHITE);
        char tn[96]; snprintf(tn, sizeof(tn), "%s%s%s", actor_type_name(a->type),
                              a->type == ACT_PICKUP ? "  //  " : "",
                              a->type == ACT_PICKUP ? pickup_name(a->subtype) : "");
        ui_text(ui, rx + 58, dy + 28, tn, 9, C_DIM);
        dy += 62;

        label_row(ui, rx + 14, dy, rw - 28, "TRANSFORM", C_ACCENT);
        dy += 22;
        float fw = (rw - 28 - 8) / 3.0f;
        ui_dragf(ui, "ed.px", "X", rx + 14, dy, fw, 22, &a->pos.x, 0.05f, -200, 200, 2);
        ui_dragf(ui, "ed.py", "Y", rx + 14 + fw + 4, dy, fw, 22, &a->pos.y, 0.05f, -50, 100, 2);
        ui_dragf(ui, "ed.pz", "Z", rx + 14 + (fw + 4) * 2, dy, fw, 22, &a->pos.z, 0.05f, -200, 200, 2);
        dy += 26;
        ui_text(ui, rx + 14, dy, "LOCATION (X / Y / Z)", 8, C_DIM);
        dy += 18;
        ui_dragf(ui, "ed.sx", "SX", rx + 14, dy, fw, 22, &a->size.x, 0.05f, 0.1f, 120, 2);
        ui_dragf(ui, "ed.sy", "SY", rx + 14 + fw + 4, dy, fw, 22, &a->size.y, 0.05f, 0.1f, 120, 2);
        ui_dragf(ui, "ed.sz", "SZ", rx + 14 + (fw + 4) * 2, dy, fw, 22, &a->size.z, 0.05f, 0.1f, 120, 2);
        dy += 26;
        ui_text(ui, rx + 14, dy, "SIZE (X / Y / Z)", 8, C_DIM);
        dy += 18;
        ui_dragf(ui, "ed.yaw", "ROTATION Y", rx + 14, dy, rw - 28, 22, &a->yaw, 0.6f, -360, 360, 1);
        dy += 28;
        ui_dragf(ui, "ed.power", "POWER / RADIUS", rx + 14, dy, (rw - 28 - 8) / 2, 22, &a->power, 0.1f, 0, 200, 1);
        ui_dragf(ui, "ed.extra", "INTENSITY / UP", rx + 14 + (rw - 28 - 8) / 2 + 8, dy, (rw - 28 - 8) / 2, 22, &a->extra, 0.1f, 0, 200, 1);
        dy += 28;
        ui_dragf(ui, "ed.respawn", "RESPAWN (S)", rx + 14, dy, rw - 28, 22, &a->respawn, 0.5f, 0, 120, 1);
        dy += 30;

        label_row(ui, rx + 14, dy, rw - 28, "MATERIAL", C_ACCENT);
        dy += 22;
        ui_rect(ui, rx + 14, dy, 34, 26, mat_color(a->material));
        ui_rect_outline(ui, rx + 14, dy, 34, 26, C_BORDER, 1);
        ui_text(ui, rx + 56, dy + 2, material_name(a->material), 12, C_TEXT);
        char mi[32]; snprintf(mi, sizeof(mi), "#%02d", a->material);
        ui_text(ui, rx + 56, dy + 15, mi, 8, C_DIM);
        if (ui_button(ui, "ed.matprev", "<", rx + rw - 74, dy, 26, 26, 0)) { a->material = (a->material + 15) % 16; a->color = mat_color(a->material); ed->dirty = true; }
        if (ui_button(ui, "ed.matnext", ">", rx + rw - 44, dy, 26, 26, 0)) { a->material = (a->material + 1) % 16; a->color = mat_color(a->material); ed->dirty = true; }
        dy += 34;

        label_row(ui, rx + 14, dy, rw - 28, "TINT / COLOR", C_ACCENT);
        dy += 22;
        static const Color tints[8] = {
            {0.30f, 0.32f, 0.36f, 1}, {0.55f, 0.57f, 0.62f, 1}, {0.85f, 0.75f, 0.42f, 1}, {0.90f, 0.35f, 0.28f, 1},
            {0.28f, 0.55f, 0.90f, 1}, {0.30f, 0.85f, 0.45f, 1}, {0.80f, 0.35f, 0.85f, 1}, {0.20f, 0.22f, 0.25f, 1}
        };
        for (int i = 0; i < 8; i++) {
            float sw = (rw - 28 - 12) / 8.0f;
            char cid[32]; snprintf(cid, sizeof(cid), "ed.tint%d", i);
            if (ui_color_swatch(ui, cid, rx + 14 + i * (sw + 1), dy, sw, 22, tints[i], false)) {
                a->color = tints[i];
                ed->dirty = true;
            }
        }
        dy += 30;

        label_row(ui, rx + 14, dy, rw - 28, "FLAGS", C_ACCENT);
        dy += 22;
        bool solid = (a->flags & FLAG_SOLID) != 0;
        bool emissive = (a->flags & FLAG_EMISSIVE) != 0;
        if (ui_toggle(ui, "ed.solid", "Solid (collision)", rx + 14, dy, (rw - 28 - 8) / 2, 24, &solid))
            { if (solid) a->flags |= FLAG_SOLID; else a->flags &= ~FLAG_SOLID; ed->dirty = true; }
        if (ui_toggle(ui, "ed.emis", "Emissive", rx + 14 + (rw - 28 - 8) / 2 + 8, dy, (rw - 28 - 8) / 2, 24, &emissive))
            { if (emissive) a->flags |= FLAG_EMISSIVE; else a->flags &= ~FLAG_EMISSIVE; ed->dirty = true; }
        dy += 30;

        if (a->type == ACT_TRIGGER) {
            label_row(ui, rx + 14, dy, rw - 28, "EVENT / CONSOLE COMMAND", C_ACCENT);
            dy += 22;
            if (ui_textbox(ui, "ed.triggername", rx + 14, dy, rw - 28, 26, a->name, MAX_NAME, "say TRIGGER"))
                ed->dirty = true;
            dy += 32;
            char rep[64]; snprintf(rep, sizeof(rep), "Repeat every %.1fs (0 = once)", a->extra);
            ui_text(ui, rx + 14, dy, rep, 9, C_DIM);
            if (ui_button(ui, "ed.trigonce", "ONCE", rx + 14, dy + 14, 70, 22, a->extra <= 0.0f ? 1 : 0)) { a->extra = 0; ed->dirty = true; }
            if (ui_button(ui, "ed.trigrep", "REPEAT 2S", rx + 90, dy + 14, 96, 22, a->extra > 0.0f ? 1 : 0)) { a->extra = 2.0f; ed->dirty = true; }
            dy += 44;
        }

        if (a->type == ACT_BRUSH) {
            label_row(ui, rx + 14, dy, rw - 28, "BRUSH SHAPE", C_ACCENT);
            dy += 22;
            const char* shapes[] = { "BOX", "RAMP", "CYLINDER" };
            for (int i = 0; i < 3; i++) {
                char sid[32]; snprintf(sid, sizeof(sid), "ed.shape%d", i);
                if (ui_button(ui, sid, shapes[i], rx + 14 + i * ((rw - 28) / 3.0f), dy, (rw - 28) / 3.0f - 3, 22, a->shape == i ? 1 : 0)) {
                    push_undo(ed);
                    a->shape = i;
                    ed->dirty = true;
                }
            }
            dy += 30;
        } else if (a->type == ACT_PICKUP) {
            label_row(ui, rx + 14, dy, rw - 28, "PICKUP TYPE", C_ACCENT);
            dy += 22;
            ui_text(ui, rx + 14, dy + 3, pickup_name(a->subtype), 12, C_TEXT);
            if (ui_button(ui, "ed.pickprev", "<", rx + rw - 74, dy, 26, 22, 0)) { a->subtype = (a->subtype + PICKUP_TYPE_COUNT - 1) % PICKUP_TYPE_COUNT; ed->dirty = true; }
            if (ui_button(ui, "ed.picknext", ">", rx + rw - 44, dy, 26, 22, 0)) { a->subtype = (a->subtype + 1) % PICKUP_TYPE_COUNT; ed->dirty = true; }
            dy += 28;
        }
        dy += 4;
        if (ui_button(ui, "ed.dup", "DUPLICATE", rx + 14, dy, (rw - 28 - 8) / 2, 26, 0)) {
            push_undo(ed);
            Actor c = *a; c.pos.x += ed->grid; c.pos.z += ed->grid;
            snprintf(c.name, MAX_NAME, "%s_copy", a->name);
            int ni = world_add(ed->world, c);
            if (ni >= 0) { ed->selected = ni; editor_set_status(ed, "Duplicated actor"); }
        }
        if (ui_button(ui, "ed.del", "DELETE", rx + 14 + (rw - 28 - 8) / 2 + 8, dy, (rw - 28 - 8) / 2, 26, 2)) {
            push_undo(ed);
            editor_set_status(ed, "Deleted %s", a->name);
            world_delete(ed->world, ed->selected);
            ed->selected = -1;
        }
        dy += 36;
    }
    ui_pop_clip(ui);

    /* viewport toolbar overlay */
    float oy = (float)ed->vy + 8, ox = (float)ed->vx + 8;
    ui_rect(ui, ox, oy, 214, 30, col(0.03f, 0.04f, 0.05f, 0.78f));
    ui_text(ui, ox + 8, oy + 9, ed->top_view ? "TOP ORTHO" : "PERSPECTIVE", 9, C_ACCENT);
    ui_text(ui, ox + 92, oy + 9, ed->mode == 0 ? "SELECT" : ed->mode == 1 ? "MOVE" : ed->mode == 2 ? "ROTATE" : "SCALE", 9, C_WHITE);
    ui_text(ui, ox + 150, oy + 9, ed->placing ? "PLACING" : (ed->snap ? "SNAP ON" : "SNAP OFF"), 9, ed->placing ? C_GREEN : C_DIM);

    /* browser label overlay (top right of viewport) */
    char vs[128];
    snprintf(vs, sizeof(vs), "GRID %.1f   |   %d ACTORS   |   FPS %.0f", ed->grid, ed->world->count, ed->fps);
    float vsw = ui_text_width(ui, vs, 9) + 16;
    ui_rect(ui, ed->vx + ed->vw - vsw - 8, oy, vsw, 22, col(0.03f, 0.04f, 0.05f, 0.70f));
    ui_text(ui, ed->vx + ed->vw - vsw, oy + 6, vs, 9, C_DIM);

    if (ed->cursor_valid) {
        char cb[128];
        snprintf(cb, sizeof(cb), "X %.1f   Y %.1f   Z %.1f", ed->cursor_world.x, ed->cursor_world.y, ed->cursor_world.z);
        float cw2 = ui_text_width(ui, cb, 9) + 16;
        ui_rect(ui, ed->vx + 8, ed->vy + ed->vh - 24, cw2, 18, col(0.03f, 0.04f, 0.05f, 0.7f));
        ui_text(ui, ed->vx + 14, ed->vy + ed->vh - 21, cb, 9, C_DIM);
    }

    /* PIE banner */
    if (ed->pie_active) {
        ui_rect(ui, ed->vx + ed->vw * 0.5f - 170, ed->vy + 4, 340, 28, col(0.05f, 0.12f, 0.08f, 0.92f));
        ui_rect_outline(ui, ed->vx + ed->vw * 0.5f - 170, ed->vy + 4, 340, 28, C_GREEN, 1);
        ui_text_center(ui, ed->vx + ed->vw * 0.5f, ed->vy + 11, "PLAY IN EDITOR  //  ESC TO STOP", 11, C_GREEN);
        Player* p = &ed->pie_game.players[0];
        char pb[128];
        snprintf(pb, sizeof(pb), "HEALTH %.0f    AMMO %d    FRAGS %d", p->health, p->ammo[p->weapon], p->frags);
        ui_rect(ui, ed->vx + 8, ed->vy + ed->vh - 48, ui_text_width(ui, pb, 10) + 18, 20, col(0.03f, 0.04f, 0.05f, 0.75f));
        ui_text(ui, ed->vx + 16, ed->vy + ed->vh - 44, pb, 10, C_WHITE);
    }

    /* ================= STATUS BAR ================= */
    float sy = (float)h - 26;
    ui_rect(ui, 0, sy, (float)w, 26, col(0.030f, 0.036f, 0.045f, 1));
    ui_rect(ui, 0, sy, (float)w, 1, col(0.20f, 0.23f, 0.27f, 1));
    Color sc = ed->status_t > 0 ? C_ACCENT : C_DIM;
    ui_text(ui, 12, sy + 7, ed->status[0] ? ed->status : "Ready", 10, sc);
    char sb[192];
    snprintf(sb, sizeof(sb), "GRID %.1f   SNAP %s   MODE %s   %d ACTORS   SELECTED %d",
             ed->grid, ed->snap ? "ON" : "OFF",
             ed->mode == 0 ? "SELECT" : ed->mode == 1 ? "MOVE" : ed->mode == 2 ? "ROTATE" : "SCALE",
             ed->world->count, ed->selected);
    ui_text_right(ui, (float)w - 12, sy + 7, sb, 9, C_DIM);
}
