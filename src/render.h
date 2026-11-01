/* Arena2004 - OpenGL renderer */
#ifndef RENDER_H
#define RENDER_H
#include "gl_util.h"
#include "world.h"
#include "ui.h"

#define MAX_RENDER_LIGHTS 16

typedef struct {
    Vec3 pos, fwd, right, up;
    float yaw, pitch, fov;
} Camera;

void camera_update(Camera* c);
Ray camera_unproject(Camera* c, int vx,int vy,int vw,int vh,float mx,float my);

typedef WorldLight Light;

typedef struct {
    GLuint prog_scene, prog_sky, prog_fx, prog_line, prog_bright, prog_blur, prog_composite;
    Mesh cube, sphere, cylinder, cone, ramp, quad;
    GLuint world_tex, fx_tex;
    Framebuffer hdr, bloomA, bloomB;
    int w, h;
    int vx, vy, vw, vh;
    Camera cam;
    Mat4 view, proj;
    Light lights[MAX_RENDER_LIGHTS];
    int nlights;
    Color fog; float fog_density;
    Color ambient_sky, ambient_ground;
    Vec3 sun_dir; Color sun_color; float sun_intensity;
    float bloom_strength;
    float exposure;
    float time;
    bool viewmodel;
    /* fx batch */
    GLuint fx_vao, fx_vbo;
    int fx_cap;
    struct FxVert* fx_verts;
    int fx_count;
    int fx_alpha_count;
    int fx_alpha_cap;
    struct FxVert* fx_alpha_verts;
    /* line batch */
    GLuint line_vao, line_vbo;
    int line_cap, line_count;
    struct LineVert* line_verts;
    GLuint fs_vao;
    bool in_scene;
} Renderer;

bool renderer_init(Renderer* r, int w, int h);
void renderer_destroy(Renderer* r);
void renderer_resize(Renderer* r, int w, int h);
void renderer_begin(Renderer* r, Camera cam, World* w, int vx,int vy,int vw,int vh,float time);
void renderer_draw_world_static(Renderer* r, World* w);
void renderer_draw_actor_ghost(Renderer* r, World* w, int actor, Color tint, bool wire);
void renderer_begin_viewmodel(Renderer* r);
void renderer_draw_model(Renderer* r, Mesh* m, Mat4 model, Color color, int material, float emissive, float uvscale, bool world_uv);
void renderer_draw_weapon_part(Renderer* r, Mesh* m, Vec3 pos, Vec3 scale, Vec3 rot, Color color, int material, float emissive);
void renderer_end(Renderer* r);

/* fx */
void fx_begin(Renderer* r);
void fx_sprite(Renderer* r, Vec3 pos, float size, Color c, bool additive);
void fx_beam(Renderer* r, Vec3 a, Vec3 b, Color c, float width);
void fx_render(Renderer* r);
void fx_render_split(Renderer* r, bool additive);

/* 3d debug lines */
void lines_begin(Renderer* r);
void line_add(Renderer* r, Vec3 a, Vec3 b, Color c);
void line_box(Renderer* r, AABB b, Color c);
void line_box_corners(Renderer* r, AABB b, Color c, float len);
void line_axis(Renderer* r, Vec3 o, Vec3 dir, float len, Color c);
void line_circle(Renderer* r, Vec3 center, Vec3 axis, float radius, Color c, int seg);
void lines_render(Renderer* r, bool depth_test, float width);

/* sky/background */
void renderer_sky(Renderer* r);

#endif
