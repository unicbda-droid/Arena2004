#include "gl_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <setjmp.h>

static GLuint compile_stage(GLenum type, const char* src, const char* name){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[4096]; glGetShaderInfoLog(s,sizeof(log),NULL,log); fprintf(stderr,"[shader] %s compile failed:\n%s\n",name,log); glDeleteShader(s); return 0; }
    return s;
}
GLuint shader_create(const char* vs_src, const char* fs_src, const char* name){
    GLuint vs=compile_stage(GL_VERTEX_SHADER,vs_src,name);
    GLuint fs=compile_stage(GL_FRAGMENT_SHADER,fs_src,name);
    if(!vs||!fs) return 0;
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char log[4096]; glGetProgramInfoLog(p,sizeof(log),NULL,log); fprintf(stderr,"[shader] %s link failed:\n%s\n",name,log); glDeleteProgram(p); p=0; }
    glDeleteShader(vs); glDeleteShader(fs); return p;
}
void shader_uniform1i(GLuint p,const char*n,int v){ glUniform1i(glGetUniformLocation(p,n),v); }
void shader_uniform1f(GLuint p,const char*n,float v){ glUniform1f(glGetUniformLocation(p,n),v); }
void shader_uniform2f(GLuint p,const char*n,float a,float b){ glUniform2f(glGetUniformLocation(p,n),a,b); }
void shader_uniform3f(GLuint p,const char*n,float a,float b,float c){ glUniform3f(glGetUniformLocation(p,n),a,b,c); }
void shader_uniform4f(GLuint p,const char*n,float a,float b,float c,float d){ glUniform4f(glGetUniformLocation(p,n),a,b,c,d); }
void shader_uniform_mat4(GLuint p,const char*n,Mat4 m){ glUniformMatrix4fv(glGetUniformLocation(p,n),1,GL_FALSE,m.m); }
void shader_uniform_vec3(GLuint p,const char*n,Vec3 v){ glUniform3f(glGetUniformLocation(p,n),v.x,v.y,v.z); }
void shader_uniform_color(GLuint p,const char*n,Color c){ glUniform4f(glGetUniformLocation(p,n),c.r,c.g,c.b,c.a); }

Mesh mesh_make(const Vertex* verts,int vcount,const uint32_t* idx,int icount){
    Mesh m; memset(&m,0,sizeof(m));
    glGenVertexArrays(1,&m.vao); glGenBuffers(1,&m.vbo); glGenBuffers(1,&m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER,m.vbo); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)vcount*sizeof(Vertex),verts,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)icount*sizeof(uint32_t),idx,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,pos));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,nrm));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,uv));
    glBindVertexArray(0);
    m.count=icount; m.vertex_count=vcount; return m;
}
void mesh_draw(Mesh* m){ glBindVertexArray(m->vao); glDrawElements(GL_TRIANGLES,m->count,GL_UNSIGNED_INT,0); glBindVertexArray(0); }
void mesh_free(Mesh* m){ if(m->vao){glDeleteVertexArrays(1,&m->vao);glDeleteBuffers(1,&m->vbo);glDeleteBuffers(1,&m->ebo);} memset(m,0,sizeof(*m)); }

Mesh mesh_cube(void){
    Vertex v[24]; uint32_t idx[36]; int vi=0,ii=0;
    const float p[6][3]={{.5f,0,0},{-.5f,0,0},{0,.5f,0},{0,-.5f,0},{0,0,.5f},{0,0,-.5f}};
    const float u[6][4][3]={
        {{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f}},
        {{-.5f,-.5f,-.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f}},
        {{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f}},
        {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}},
        {{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},
        {{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f}}
    };
    for(int f=0;f<6;f++){
        for(int k=0;k<4;k++){ v[vi].pos=v3(u[f][k][0],u[f][k][1],u[f][k][2]); v[vi].nrm=v3(p[f][0],p[f][1],p[f][2]); v[vi].uv=v2(k==1||k==2?1.f:0.f, k>=2?1.f:0.f); vi++; }
        int b=f*4; idx[ii++]=b; idx[ii++]=b+1; idx[ii++]=b+2; idx[ii++]=b; idx[ii++]=b+2; idx[ii++]=b+3;
    }
    return mesh_make(v,vi,idx,ii);
}
Mesh mesh_quad(void){
    Vertex v[4]={{v3(-.5f,-.5f,0),v3(0,0,1),v2(0,1)},{v3(.5f,-.5f,0),v3(0,0,1),v2(1,1)},{v3(.5f,.5f,0),v3(0,0,1),v2(1,0)},{v3(-.5f,.5f,0),v3(0,0,1),v2(0,0)}};
    uint32_t idx[6]={0,1,2,0,2,3};
    return mesh_make(v,4,idx,6);
}
Mesh mesh_sphere(int seg,int rings){
    int vc=(rings+1)*(seg+1); Vertex* v=malloc(sizeof(Vertex)*vc); int ic=rings*seg*6; uint32_t* idx=malloc(sizeof(uint32_t)*ic);
    int vi=0;
    for(int r=0;r<=rings;r++){
        float ph=PI*(float)r/rings;
        for(int s=0;s<=seg;s++){
            float th=2*PI*(float)s/seg;
            Vec3 n=v3(sinf(ph)*cosf(th),cosf(ph),sinf(ph)*sinf(th));
            v[vi].pos=v3mul(n,0.5f); v[vi].nrm=n; v[vi].uv=v2((float)s/seg,(float)r/rings); vi++;
        }
    }
    int ii=0;
    for(int r=0;r<rings;r++) for(int s=0;s<seg;s++){
        int a=r*(seg+1)+s, b=a+seg+1;
        idx[ii++]=a; idx[ii++]=b; idx[ii++]=a+1; idx[ii++]=a+1; idx[ii++]=b; idx[ii++]=b+1;
    }
    Mesh m=mesh_make(v,vi,idx,ii); free(v); free(idx); return m;
}
Mesh mesh_cylinder(int seg,bool caps){
    int vc=(seg+1)*4+(seg*2)*2; Vertex* v=malloc(sizeof(Vertex)*vc); int ic=seg*12+seg*6; uint32_t* idx=malloc(sizeof(uint32_t)*ic);
    int vi=0,ii=0;
    for(int s=0;s<=seg;s++){
        float a=2*PI*s/seg, c=cosf(a), sn=sinf(a);
        v[vi++]=(Vertex){v3(c*.5f,-.5f,sn*.5f),v3(c,0,sn),v2((float)s/seg,1)};
        v[vi++]=(Vertex){v3(c*.5f,.5f,sn*.5f),v3(c,0,sn),v2((float)s/seg,0)};
    }
    for(int s=0;s<seg;s++){ int a=s*2,b=a+1,c=a+2,d=a+3; idx[ii++]=a;idx[ii++]=c;idx[ii++]=b; idx[ii++]=b;idx[ii++]=c;idx[ii++]=d; }
    if(caps){
        int top=vi; for(int s=0;s<=seg;s++){ float a=2*PI*s/seg; v[vi++]=(Vertex){v3(cosf(a)*.5f,.5f,sinf(a)*.5f),v3(0,1,0),v2(cosf(a)*.5f+.5f,sinf(a)*.5f+.5f)}; }
        int tcenter=vi; v[vi++]=(Vertex){v3(0,.5f,0),v3(0,1,0),v2(.5f,.5f)};
        for(int s=0;s<seg;s++){ idx[ii++]=tcenter; idx[ii++]=top+s; idx[ii++]=top+s+1; }
        int bot=vi; for(int s=0;s<=seg;s++){ float a=2*PI*s/seg; v[vi++]=(Vertex){v3(cosf(a)*.5f,-.5f,sinf(a)*.5f),v3(0,-1,0),v2(cosf(a)*.5f+.5f,sinf(a)*.5f+.5f)}; }
        int bcenter=vi; v[vi++]=(Vertex){v3(0,-.5f,0),v3(0,-1,0),v2(.5f,.5f)};
        for(int s=0;s<seg;s++){ idx[ii++]=bcenter; idx[ii++]=bot+s+1; idx[ii++]=bot+s; }
    }
    Mesh m=mesh_make(v,vi,idx,ii); free(v); free(idx); return m;
}
Mesh mesh_cone(int seg){
    int vc=seg*3+seg+2; Vertex* v=malloc(sizeof(Vertex)*vc); int ic=seg*3+seg*3; uint32_t* idx=malloc(sizeof(uint32_t)*ic);
    int vi=0,ii=0;
    for(int s=0;s<seg;s++){
        float a0=2*PI*s/seg,a1=2*PI*(s+1)/seg;
        Vec3 p0=v3(cosf(a0)*.5f,-.5f,sinf(a0)*.5f),p1=v3(cosf(a1)*.5f,-.5f,sinf(a1)*.5f),tip=v3(0,.5f,0);
        Vec3 n0=v3(cosf(a0),.5f,sinf(a0)),n1=v3(cosf(a1),.5f,sinf(a1));
        int b=vi;
        v[vi++]=(Vertex){p0,n0,v2((float)s/seg,1)}; v[vi++]=(Vertex){p1,n1,v2((float)(s+1)/seg,1)}; v[vi++]=(Vertex){tip,v3norm(v3add(n0,n1)),v2((float)(s+.5f)/seg,0)};
        idx[ii++]=b;idx[ii++]=b+1;idx[ii++]=b+2;
    }
    int bot=vi; for(int s=0;s<=seg;s++){ float a=2*PI*s/seg; v[vi++]=(Vertex){v3(cosf(a)*.5f,-.5f,sinf(a)*.5f),v3(0,-1,0),v2(cosf(a)*.5f+.5f,sinf(a)*.5f+.5f)}; }
    int cen=vi; v[vi++]=(Vertex){v3(0,-.5f,0),v3(0,-1,0),v2(.5f,.5f)};
    for(int s=0;s<seg;s++){ idx[ii++]=cen; idx[ii++]=bot+s+1; idx[ii++]=bot+s; }
    Mesh m=mesh_make(v,vi,idx,ii); free(v); free(idx); return m;
}
Mesh mesh_ramp(void){
    Vertex v[30]; uint32_t idx[36]; int vi=0,ii=0;
    /* top slope rising towards +Z: y = z (local), normal (0,1,-1) */
    Vertex top[4]={{v3(-.5f,-.5f,-.5f),v3(0,.7071f,-.7071f),v2(0,0)},{v3(-.5f,.5f,.5f),v3(0,.7071f,-.7071f),v2(0,1)},{v3(.5f,.5f,.5f),v3(0,.7071f,-.7071f),v2(1,1)},{v3(.5f,-.5f,-.5f),v3(0,.7071f,-.7071f),v2(1,0)}};
    for(int i=0;i<4;i++) v[vi++]=top[i];
    uint32_t ti[6]={0,1,2,0,2,3}; for(int i=0;i<6;i++) idx[ii++]=ti[i];
    /* bottom */
    { int b=vi; v[vi++]=(Vertex){v3(-.5f,-.5f,.5f),v3(0,-1,0),v2(0,0)}; v[vi++]=(Vertex){v3(.5f,-.5f,.5f),v3(0,-1,0),v2(1,0)}; v[vi++]=(Vertex){v3(.5f,-.5f,-.5f),v3(0,-1,0),v2(1,1)}; v[vi++]=(Vertex){v3(-.5f,-.5f,-.5f),v3(0,-1,0),v2(0,1)}; idx[ii++]=b;idx[ii++]=b+1;idx[ii++]=b+2;idx[ii++]=b;idx[ii++]=b+2;idx[ii++]=b+3; }
    /* back wall z=+0.5 */
    { int b=vi; v[vi++]=(Vertex){v3(-.5f,-.5f,.5f),v3(0,0,1),v2(0,1)}; v[vi++]=(Vertex){v3(.5f,-.5f,.5f),v3(0,0,1),v2(1,1)}; v[vi++]=(Vertex){v3(.5f,.5f,.5f),v3(0,0,1),v2(1,0)}; v[vi++]=(Vertex){v3(-.5f,.5f,.5f),v3(0,0,1),v2(0,0)}; idx[ii++]=b;idx[ii++]=b+1;idx[ii++]=b+2;idx[ii++]=b;idx[ii++]=b+2;idx[ii++]=b+3; }
    /* sides */
    { int b=vi; v[vi++]=(Vertex){v3(-.5f,-.5f,.5f),v3(-1,0,0),v2(0,0)}; v[vi++]=(Vertex){v3(-.5f,-.5f,-.5f),v3(-1,0,0),v2(1,0)}; v[vi++]=(Vertex){v3(-.5f,.5f,.5f),v3(-1,0,0),v2(0,1)}; idx[ii++]=b;idx[ii++]=b+1;idx[ii++]=b+2; }
    { int b=vi; v[vi++]=(Vertex){v3(.5f,-.5f,.5f),v3(1,0,0),v2(0,0)}; v[vi++]=(Vertex){v3(.5f,.5f,.5f),v3(1,0,0),v2(0,1)}; v[vi++]=(Vertex){v3(.5f,-.5f,-.5f),v3(1,0,0),v2(1,0)}; idx[ii++]=b;idx[ii++]=b+1;idx[ii++]=b+2; }
    return mesh_make(v,vi,idx,ii);
}

Framebuffer fbo_create(int w,int h,bool hdr,bool depth){
    Framebuffer fb; memset(&fb,0,sizeof(fb));
    glGenFramebuffers(1,&fb.fbo); glBindFramebuffer(GL_FRAMEBUFFER,fb.fbo);
    glGenTextures(1,&fb.color_tex); glBindTexture(GL_TEXTURE_2D,fb.color_tex);
    glTexImage2D(GL_TEXTURE_2D,0,hdr?GL_RGBA16F:GL_RGBA8,w,h,0,GL_RGBA,hdr?GL_FLOAT:GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,fb.color_tex,0);
    if(depth){ glGenRenderbuffers(1,&fb.depth_rb); glBindRenderbuffer(GL_RENDERBUFFER,fb.depth_rb); glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h); glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,fb.depth_rb); }
    fb.w=w; fb.h=h;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    return fb;
}
void fbo_resize(Framebuffer* fb,int w,int h,bool hdr){
    if(fb->w==w&&fb->h==h) return;
    fbo_free(fb); *fb=fbo_create(w,h,hdr,true);
}
void fbo_bind(Framebuffer* fb){ glBindFramebuffer(GL_FRAMEBUFFER,fb?fb->fbo:0); glViewport(0,0,fb?fb->w:0,fb?fb->h:0); }
void fbo_free(Framebuffer* fb){ if(fb->fbo)glDeleteFramebuffers(1,&fb->fbo); if(fb->color_tex)glDeleteTextures(1,&fb->color_tex); if(fb->depth_rb)glDeleteRenderbuffers(1,&fb->depth_rb); memset(fb,0,sizeof(*fb)); }

GLuint texture_create_rgba(int w,int h,const unsigned char* pixels,bool repeat,bool mipmap){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    if(mipmap) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0); return t;
}
GLuint texture_create_float(int w,int h,const float* pixels,bool repeat,bool mipmap){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    if(mipmap) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0); return t;
}
void texture_free(GLuint* t){ if(*t)glDeleteTextures(1,t); *t=0; }

/* ---------------- procedural assets ---------------- */
static float vnoise(float x,float y){
    int xi=(int)floorf(x), yi=(int)floorf(y); float fx=x-xi, fy=y-yi;
    fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
    uint32_t ux=(uint32_t)xi, uy=(uint32_t)yi;
    float a=hash01(ux*374761393u+uy*668265263u), b=hash01((ux+1u)*374761393u+uy*668265263u);
    float c=hash01(ux*374761393u+(uy+1u)*668265263u), d=hash01((ux+1u)*374761393u+(uy+1u)*668265263u);
    return lerpf(lerpf(a,b,fx),lerpf(c,d,fx),fy);
}
static float fbm(float x,float y,int oct){ float s=0,a=.5f; for(int i=0;i<oct;i++){ s+=vnoise(x,y)*a; x*=2.03f; y*=2.01f; a*=.5f; } return s; }
static void px(unsigned char* d,int w,int x,int y,float r,float g,float b,float a){
    if(x<0 || y<0 || x>=w || y>=w) return;
    unsigned char* p=d+(y*w+x)*4;
    p[0]=(unsigned char)(clampf(r,0,1)*255);
    p[1]=(unsigned char)(clampf(g,0,1)*255);
    p[2]=(unsigned char)(clampf(b,0,1)*255);
    p[3]=(unsigned char)(clampf(a,0,1)*255);
}
GLuint make_world_atlas(void){
    enum {S=1024,T=256,N=4};
    unsigned char* d=calloc(S*S,4);
    /* tile 0: dark metal panels */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        float n=fbm(x*0.035f,y*0.035f,4);
        float base=0.20f+n*0.10f;
        float panel=fmodf(x/64.0f,1.0f), panel2=fmodf(y/64.0f,1.0f);
        if(panel<0.05f||panel2<0.05f) base*=0.55f;
        if(panel>0.95f||panel2>0.95f) base*=1.25f;
        float rivet=0;
        for(int rx=0;rx<2;rx++)for(int ry=0;ry<2;ry++){ float dx=x-(16+rx*32), dy=y-(16+ry*32); rivet=fmaxf(rivet,1.0f-clampf(sqrtf(dx*dx+dy*dy)/4.0f,0,1)); }
        base += rivet*0.12f;
        float scratch=(fbm(x*0.6f,y*0.15f,2)>0.72f)?0.03f:0;
        px(d,S,x,y, base*0.92f+scratch, base*0.95f+scratch, base+0.02f+scratch,1);
    }
    /* tile 1: floor plates with diamond tread */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+T, fy=y;
        float n=fbm(fx*0.04f,fy*0.04f,4); float base=0.17f+n*0.09f;
        float gx=fmodf(x/32.0f,1.0f), gy=fmodf(y/32.0f,1.0f);
        if(gx<0.06f||gy<0.06f) base*=0.55f;
        float dia=fabsf(fmodf((x+y)/22.0f,1.0f)-0.5f);
        if(dia<0.10f) base+=0.035f;
        float dia2=fabsf(fmodf((x-y+1000)/22.0f,1.0f)-0.5f);
        if(dia2<0.08f) base+=0.025f;
        px(d,S,fx,fy,base*0.95f,base,base*1.06f,1);
    }
    /* tile 2: hazard stripes */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+2*T, fy=y;
        float band=fmodf((x+y)*0.09f,1.0f);
        float n=fbm(fx*0.05f,fy*0.05f,3);
        float r=0.55f,g=0.42f,b=0.05f;
        if(band<0.5f){ r=0.08f;g=0.08f;b=0.09f; }
        r*=0.8f+n*0.3f; g*=0.8f+n*0.3f; b*=0.8f+n*0.3f;
        if(fmodf(y/32.0f,1.0f)<0.05f){ r*=0.6f;g*=0.6f;b*=0.6f; }
        px(d,S,fx,fy,r,g,b,1);
    }
    /* tile 3: grate / dark */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+3*T, fy=y;
        float base=0.055f+fbm(fx*0.05f,fy*0.05f,3)*0.035f;
        float cellx=fmodf(x/16.0f,1.0f), celly=fmodf(y/16.0f,1.0f);
        if(cellx>0.2f&&cellx<0.8f&&celly>0.2f&&celly<0.8f) base=0.015f;
        px(d,S,fx,fy,base*0.9f,base,base*1.1f,1);
    }
    /* tile 4: concrete */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x, fy=y+T;
        float n=fbm(fx*0.02f,fy*0.02f,5);
        float base=0.16f+n*0.14f;
        if(fmodf(x/128.0f,1.0f)<0.02f||fmodf(y/128.0f,1.0f)<0.02f) base*=0.65f;
        if(fbm(fx*0.2f,fy*0.2f,2)>0.7f) base*=0.85f;
        px(d,S,fx,fy,base*1.02f,base*0.98f,base*0.92f,1);
    }
    /* tile 5: tech circuit (emissive-ish) */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+T, fy=y+T;
        float cx=fmodf(x/64.0f,1.0f), cy=fmodf(y/64.0f,1.0f);
        float line=0;
        if(fabsf(cx-0.5f)<0.02f&&cy<0.5f) line=1;
        if(fabsf(cy-0.5f)<0.02f&&cx>0.5f) line=1;
        if(fabsf(cx-0.2f)<0.015f&&cy>0.2f) line=1;
        float dot=1.0f-clampf(sqrtf((cx-0.5f)*(cx-0.5f)+(cy-0.5f)*(cy-0.5f))/0.06f,0,1);
        float r=0.02f+line*0.05f, g=0.12f+line*0.55f+dot*0.4f, b=0.20f+line*0.75f+dot*0.6f;
        px(d,S,fx,fy,r,g,b,1);
    }
    /* tile 6: blue panels */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+2*T, fy=y+T;
        float n=fbm(fx*0.03f,fy*0.03f,3);
        float base=0.05f+n*0.05f;
        if(fmodf(x/64.0f,1.0f)<0.03f||fmodf(y/64.0f,1.0f)<0.03f) base=0.01f;
        float hi=0.0f; if(fmodf(x/64.0f,1.0f)>0.9f&&fmodf(x/64.0f,1.0f)<0.93f) hi=0.3f;
        px(d,S,fx,fy,base*0.5f,base*1.2f+hi*0.2f,base*2.0f+hi,1);
    }
    /* tile 7: rust */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+3*T, fy=y+T;
        float n=fbm(fx*0.05f,fy*0.05f,5);
        float base=0.2f+n*0.12f;
        float rust=fbm(fx*0.12f+9,fy*0.12f,3);
        float r=base*(1.0f+rust*0.9f), g=base*0.72f, b=base*0.5f;
        if(fmodf(y/26.0f,1.0f)<0.1f) r*=0.7f,g*=0.7f,b*=0.7f;
        px(d,S,fx,fy,r,g,b,1);
    }
    /* tile 8: clean white panels */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x, fy=y+2*T;
        float n=fbm(fx*0.04f,fy*0.04f,3);
        float base=0.66f+n*0.12f;
        if(fmodf(x/64.0f,1.0f)<0.03f||fmodf(y/64.0f,1.0f)<0.03f) base*=0.75f;
        px(d,S,fx,fy,base,base*1.01f,base*1.03f,1);
    }
    /* tile 9: hex grid */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+T, fy=y+2*T;
        float base=0.05f+fbm(fx*0.06f,fy*0.06f,3)*0.04f;
        float hx=x*0.06f, hy=y*0.06f;
        float h=fabsf(fmodf(hx+hy*0.577f,1.0f)-0.5f);
        float h2=fabsf(fmodf(hx-hy*0.577f,1.0f)-0.5f);
        if(h>0.44f||h2>0.44f) base+=0.04f;
        px(d,S,fx,fy,base*0.8f,base*1.05f,base*1.3f,1);
    }
    /* tile 10: ribbed trim */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+2*T, fy=y+2*T;
        float base=0.13f+fbm(fx*0.02f,fy*0.02f,3)*0.08f;
        float r=fmodf(x/12.0f,1.0f); base+= (r<0.4f?0.06f:0.0f);
        px(d,S,fx,fy,base*0.95f,base,base*1.08f,1);
    }
    /* tile 11: red armor panels */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+3*T, fy=y+2*T;
        float base=0.10f+fbm(fx*0.04f,fy*0.04f,4)*0.08f;
        if(fmodf(x/48.0f,1.0f)<0.04f||fmodf(y/48.0f,1.0f)<0.04f) base*=0.6f;
        px(d,S,fx,fy,base*2.2f,base*0.5f,base*0.4f,1);
    }
    /* tile 12: yellow tech */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x, fy=y+3*T;
        float base=0.03f;
        float gx=fmodf(x/32.0f,1.0f),gy=fmodf(y/32.0f,1.0f);
        float line=(gx<0.06f||gy<0.06f)?1.0f:0;
        px(d,S,fx,fy,base+line*0.5f,base+line*0.35f,base+line*0.05f,1);
    }
    /* tile 13: dark blue metal */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+T, fy=y+3*T;
        float base=0.07f+fbm(fx*0.03f,fy*0.03f,4)*0.06f;
        if(fmodf(x/64.0f,1.0f)<0.03f) base*=0.6f;
        px(d,S,fx,fy,base*0.7f,base*0.95f,base*1.5f,1);
    }
    /* tile 14: hazard blue */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+2*T, fy=y+3*T;
        float band=fmodf((x+y)*0.09f,1.0f);
        float n=fbm(fx*0.05f,fy*0.05f,3);
        float r=0.05f,g=0.3f,b=0.6f;
        if(band<0.5f){ r=0.02f;g=0.05f;b=0.09f; }
        px(d,S,fx,fy,r*(0.8f+n*0.3f),g*(0.8f+n*0.3f),b*(0.8f+n*0.3f),1);
    }
    /* tile 15: black glossy */
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int fx=x+3*T, fy=y+3*T;
        float base=0.02f+fbm(fx*0.03f,fy*0.03f,3)*0.02f;
        px(d,S,fx,fy,base,base*1.05f,base*1.15f,1);
    }
    GLuint t=texture_create_rgba(S,S,d,true,true); free(d); return t;
}

GLuint make_ui_atlas(int* white_x,int* white_y,int* glow_x,int* glow_y,int* ring_x,int* ring_y){
    const int S=512; unsigned char* d=calloc(S*S,4);
    /* white block 16x16 */
    for(int y=0;y<16;y++)for(int x=0;x<16;x++) px(d,S,x,y,1,1,1,1);
    *white_x=0; *white_y=0;
    /* radial glow 128x128 */
    int gx=32,gy=0;
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        float dx=(x-63.5f)/63.5f, dy=(y-63.5f)/63.5f; float r=sqrtf(dx*dx+dy*dy);
        float a=powf(saturate(1-r),2.0f);
        px(d,S,gx+x,gy+y,a,a,a,a);
    }
    *glow_x=gx; *glow_y=gy;
    /* ring 128x128 */
    int rx=176,ry=0;
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        float dx=(x-63.5f)/63.5f, dy=(y-63.5f)/63.5f; float r=sqrtf(dx*dx+dy*dy);
        float a=saturate(1-fabsf(r-0.82f)/0.06f);
        a*=a;
        px(d,S,rx+x,ry+y,a,a,a,a);
    }
    *ring_x=rx; *ring_y=ry;
    /* soft vertical gradient 64 wide (for bars) */
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){ float a=1.0f-(float)y/64.0f; px(d,S,320+x,0+y,a,a,a,a); }
    /* diagonal stripe tile 64 (hazard UI) */
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){ float b=fmodf((x+y)*0.12f,1.0f); float a=(b<0.5f)?1.0f:0.0f; px(d,S,320+x,72+y,a,a,a,a); }
    GLuint t=texture_create_rgba(S,S,d,true,true); free(d); return t;
}

bool save_png(const char* path,int w,int h,const unsigned char* rgba){
    FILE* fp=fopen(path,"wb"); if(!fp) return false;
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if(!png){fclose(fp);return false;}
    png_infop info=png_create_info_struct(png);
    if(!info){png_destroy_write_struct(&png,NULL);fclose(fp);return false;}
    if(setjmp(png_jmpbuf(png))){ png_destroy_write_struct(&png,&info); fclose(fp); return false; }
    png_init_io(png,fp);
    png_set_IHDR(png,info,w,h,8,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    for(int y=0;y<h;y++) png_write_row(png,(png_bytep)(rgba+(size_t)y*w*4));
    png_write_end(png,NULL);
    png_destroy_write_struct(&png,&info);
    fclose(fp); return true;
}
