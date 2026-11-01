/* Arena2004 - GL utilities: shaders, meshes, framebuffers, textures */
#ifndef GL_UTIL_H
#define GL_UTIL_H
#include <GL/glew.h>
#include <stdbool.h>
#include "math3d.h"

typedef struct { Vec3 pos; Vec3 nrm; Vec2 uv; } Vertex;

typedef struct {
    GLuint vao, vbo, ebo;
    int count;      /* index count */
    int vertex_count;
} Mesh;

typedef struct {
    GLuint color_tex;   /* HDR RGBA16F */
    GLuint depth_rb;
    GLuint fbo;
    int w, h;
} Framebuffer;

GLuint shader_create(const char* vs_src, const char* fs_src, const char* name);
void shader_uniform1i(GLuint p, const char* n, int v);
void shader_uniform1f(GLuint p, const char* n, float v);
void shader_uniform2f(GLuint p, const char* n, float a, float b);
void shader_uniform3f(GLuint p, const char* n, float a, float b, float c);
void shader_uniform4f(GLuint p, const char* n, float a, float b, float c, float d);
void shader_uniform_mat4(GLuint p, const char* n, Mat4 m);
void shader_uniform_vec3(GLuint p, const char* n, Vec3 v);
void shader_uniform_color(GLuint p, const char* n, Color c);

Mesh mesh_make(const Vertex* verts, int vcount, const uint32_t* idx, int icount);
Mesh mesh_cube(void);
Mesh mesh_sphere(int segments, int rings);
Mesh mesh_cylinder(int segments, bool caps);
Mesh mesh_cone(int segments);
Mesh mesh_ramp(void);       /* unit ramp: x in [-.5,.5], y 0..1 along -z? see impl */
Mesh mesh_quad(void);
void mesh_draw(Mesh* m);
void mesh_free(Mesh* m);

Framebuffer fbo_create(int w, int h, bool hdr, bool depth);
void fbo_resize(Framebuffer* fb, int w, int h, bool hdr);
void fbo_bind(Framebuffer* fb);
void fbo_free(Framebuffer* fb);

GLuint texture_create_rgba(int w, int h, const unsigned char* pixels, bool repeat, bool mipmap);
GLuint texture_create_float(int w, int h, const float* pixels, bool repeat, bool mipmap);
void texture_free(GLuint* t);

/* procedural texture atlases */
GLuint make_world_atlas(void);
GLuint make_ui_atlas(int* white_x, int* white_y, int* glow_x, int* glow_y, int* ring_x, int* ring_y);
bool save_png(const char* path, int w, int h, const unsigned char* rgba);

#define GL_CHECK(x) do{ x; GLenum err=glGetError(); if(err!=GL_NO_ERROR) fprintf(stderr,"[GL] %s -> 0x%x (%s:%d)\n",#x,err,__FILE__,__LINE__); }while(0)

#endif
