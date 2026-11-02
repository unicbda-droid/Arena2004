#include "render.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void camera_update(Camera* c){
    float cp=cosf(c->pitch), sp=sinf(c->pitch), sy=sinf(c->yaw), cy=cosf(c->yaw);
    c->fwd=v3(cp*sy, sp, -cp*cy);
    c->right=v3(cy,0,sy);
    c->up=v3cross(c->right,c->fwd);
}
Ray camera_unproject(Camera* c,int vx,int vy,int vw,int vh,float mx,float my){
    return camera_ray(c->pos,c->fwd,c->right,c->up,c->fov*DEG2RAD,(float)vw/fmaxf((float)vh,1.0f),mx-vx,my-vy,(float)vw,(float)vh);
}

static const char* SCENE_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uView,uProj,uModel;\n"
"out vec3 vWorld; out vec3 vN; out vec2 vUV;\n"
"void main(){ vec4 wp=uModel*vec4(aPos,1.0); vWorld=wp.xyz; vN=mat3(uModel)*aNrm; vUV=aUV; gl_Position=uProj*uView*wp; }\n";

static const char* SCENE_FS =
"#version 330 core\n"
"in vec3 vWorld; in vec3 vN; in vec2 vUV; out vec4 frag;\n"
"uniform sampler2D uTex;\n"
"uniform vec3 uColor;\n"
"uniform vec2 uTile;\n"
"uniform float uUVScale;\n"
"uniform int uWorldUV;\n"
"uniform float uEmissive;\n"
"uniform int uViewmodel;\n"
"uniform vec3 uCamPos;\n"
"uniform int uLightCount;\n"
"uniform vec3 uLightPos[16];\n"
"uniform vec3 uLightColor[16];\n"
"uniform float uLightRadius[16];\n"
"uniform vec3 uSunDir, uSunColor;\n"
"uniform float uSunIntensity;\n"
"uniform vec3 uAmbientSky, uAmbientGround, uFogColor;\n"
"uniform float uFogDensity;\n"
"void main(){\n"
"  vec3 N=normalize(vN);\n"
"  vec2 uv;\n"
"  if(uWorldUV==1){\n"
"    vec2 p = abs(N.y)>0.65 ? vWorld.xz : (abs(N.x)>0.65 ? vWorld.zy : vWorld.xy);\n"
"    uv = uTile + fract(p*uUVScale)*0.25;\n"
"  } else { uv = uTile + fract(vUV*uUVScale)*0.25; }\n"
"  vec3 tex=texture(uTex,uv).rgb;\n"
"  vec3 albedo=tex*uColor;\n"
"  vec3 V=normalize(uCamPos-vWorld);\n"
"  if(uViewmodel==1){ V=vec3(0,0,1); }\n"
"  vec3 amb=mix(uAmbientGround,uAmbientSky,clamp(N.y*0.5+0.5,0.0,1.0));\n"
"  vec3 lit=albedo*amb*0.8;\n"
"  vec3 L=normalize(uSunDir);\n"
"  float ndl=max(dot(N,L),0.0);\n"
"  lit += albedo*uSunColor*uSunIntensity*(ndl*0.85+0.15);\n"
"  if(uViewmodel==1){\n"
"    vec3 VL=normalize(vec3(-0.45,0.6,0.8));\n"
"    float d=max(dot(N,VL),0.0); lit += albedo*vec3(0.9,0.95,1.0)*(d*0.8+0.25);\n"
"    vec3 H=normalize(V+VL); lit += vec3(0.25,0.28,0.32)*pow(max(dot(N,H),0.0),40.0)*0.5;\n"
"  } else {\n"
"    for(int i=0;i<16;i++){ if(i>=uLightCount) break;\n"
"      vec3 d=uLightPos[i]-vWorld; float dist=length(d); if(dist>uLightRadius[i]) continue;\n"
"      vec3 l=normalize(d); float att=pow(max(1.0-dist/uLightRadius[i],0.0),2.0);\n"
"      float nl=max(dot(N,l),0.0);\n"
"      vec3 H=normalize(V+l); float spec=pow(max(dot(N,H),0.0),48.0)*0.28;\n"
"      lit += uLightColor[i]*att*(albedo*(nl*1.15+0.05)+spec);\n"
"    }\n"
"  }\n"
"  float fres=pow(1.0-max(dot(N,V),0.0),3.0);\n"
"  lit += uAmbientSky*fres*0.10;\n"
"  vec3 emc=uColor*(0.7+tex*0.8)*uEmissive*2.6;\n"
"  lit += emc;\n"
"  if(uViewmodel==0){\n"
"    float dist=length(vWorld-uCamPos);\n"
"    float fog=1.0-exp(-dist*dist*uFogDensity*uFogDensity);\n"
"    lit=mix(lit,uFogColor,fog*0.92);\n"
"  }\n"
"  frag=vec4(lit,1.0);\n"
"}\n";

static const char* SKY_VS =
"#version 330 core\n"
"out vec2 vUV;\n"
"void main(){ vec2 p=vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV=p; gl_Position=vec4(p*2.0-1.0,0.999,1.0); }\n";
static const char* SKY_FS =
"#version 330 core\n"
"in vec2 vUV; out vec4 frag;\n"
"uniform vec3 uFwd,uRight,uUp; uniform float uTan,uAspect,uTime;\n"
"float hash(vec3 p){ p=fract(p*0.3183099+vec3(0.1,0.2,0.3)); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }\n"
"float noise3(vec3 p){ vec3 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);\n"
"  float a=mix(mix(mix(hash(i+vec3(0,0,0)),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z); return a; }\n"
"float fbm(vec3 p){ float s=0.0,a=0.5; for(int i=0;i<5;i++){ s+=noise3(p)*a; p*=2.03; a*=0.5; } return s; }\n"
"void main(){\n"
"  vec2 ndc=vUV*2.0-1.0;\n"
"  vec3 dir=normalize(uFwd+uRight*ndc.x*uAspect*uTan+uUp*ndc.y*uTan);\n"
"  float h=clamp(dir.y*0.5+0.5,0.0,1.0);\n"
"  vec3 horizon=vec3(0.055,0.075,0.13);\n"
"  vec3 zenith=vec3(0.006,0.012,0.032);\n"
"  vec3 col=mix(horizon,zenith,pow(h,0.55));\n"
"  float neb=fbm(dir*3.0+vec3(0.0,0.0,uTime*0.005));\n"
"  float neb2=fbm(dir*6.0-vec3(3.0,1.0,2.0));\n"
"  col += vec3(0.12,0.05,0.22)*pow(neb,2.5)*0.8;\n"
"  col += vec3(0.02,0.09,0.15)*pow(neb2,3.0)*0.9;\n"
"  vec3 cell=floor(dir*420.0);\n"
"  float st=hash(cell);\n"
"  float star=smoothstep(0.9955,1.0,st);\n"
"  col += vec3(0.8,0.88,1.0)*star*(0.6+0.4*sin(uTime*2.0+st*30.0));\n"
"  vec3 pdir=normalize(vec3(-0.55,0.32,-0.72));\n"
"  float pd=dot(dir,pdir);\n"
"  float pr=0.30;\n"
"  if(pd>cos(pr)){\n"
"    float ang=acos(clamp(pd,-1.0,1.0))/pr;\n"
"    vec3 n=normalize(dir-pdir*pd);\n"
"    float bands=fbm(n*4.0+vec3(0.4,1.2,0.2));\n"
"    vec3 pc=mix(vec3(0.10,0.12,0.24),vec3(0.42,0.35,0.55),bands*0.7);\n"
"    pc=mix(pc,vec3(0.75,0.55,0.42),pow(fbm(n*8.0+vec3(2.0)),3.0)*0.5);\n"
"    pc*=0.35+0.65*sqrt(max(1.0-ang*ang,0.0));\n"
"    col=mix(col,pc,smoothstep(1.0,0.75,ang));\n"
"    float rim=smoothstep(0.65,1.0,ang)*(1.0-smoothstep(0.97,1.0,ang));\n"
"    col += vec3(0.25,0.45,0.9)*rim*0.55;\n"
"  }\n"
"  float sd=dot(dir,normalize(vec3(0.42,0.55,-0.42)));\n"
"  col += vec3(1.0,0.88,0.62)*pow(max(sd,0.0),900.0)*2.2;\n"
"  col += vec3(0.5,0.55,0.8)*pow(max(sd,0.0),18.0)*0.06;\n"
"  col += vec3(0.03,0.045,0.07)*smoothstep(0.0,0.4,dir.y);\n"
"  frag=vec4(col,1.0);\n"
"}\n";

static const char* FX_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol;\n"
"uniform mat4 uView,uProj; out vec2 vUV; out vec4 vCol;\n"
"void main(){ vUV=aUV; vCol=aCol; gl_Position=uProj*uView*vec4(aPos,1.0); }\n";
static const char* FX_FS =
"#version 330 core\n"
"in vec2 vUV; in vec4 vCol; out vec4 frag;\n"
"uniform sampler2D uTex;\n"
"void main(){ vec4 t=texture(uTex,vUV); frag=vec4(vCol.rgb*t.rgb, vCol.a*t.a); }\n";

static const char* LINE_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol;\n"
"uniform mat4 uView,uProj; out vec4 vCol;\n"
"void main(){ vCol=aCol; gl_Position=uProj*uView*vec4(aPos,1.0); }\n";
static const char* LINE_FS =
"#version 330 core\n"
"in vec4 vCol; out vec4 frag;\n"
"void main(){ frag=vCol; }\n";

static const char* FS_VS =
"#version 330 core\n"
"out vec2 vUV;\n"
"void main(){ vec2 p=vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
static const char* BRIGHT_FS =
"#version 330 core\n"
"in vec2 vUV; out vec4 frag; uniform sampler2D uTex;\n"
"void main(){ vec3 c=texture(uTex,vUV).rgb; float l=max(max(c.r,c.g),c.b); float k=smoothstep(0.55,1.7,l); frag=vec4(c*k,1.0); }\n";
static const char* BLUR_FS =
"#version 330 core\n"
"in vec2 vUV; out vec4 frag; uniform sampler2D uTex; uniform vec2 uDir;\n"
"void main(){ vec3 s=texture(uTex,vUV).rgb*0.227027; s+=texture(uTex,vUV+uDir*1.3846).rgb*0.316216; s+=texture(uTex,vUV-uDir*1.3846).rgb*0.316216; s+=texture(uTex,vUV+uDir*3.2307).rgb*0.070270; s+=texture(uTex,vUV-uDir*3.2307).rgb*0.070270; frag=vec4(s,1.0); }\n";
static const char* COMP_FS =
"#version 330 core\n"
"in vec2 vUV; out vec4 frag; uniform sampler2D uTex,uBloomTex; uniform float uTime; uniform vec2 uRes;\n"
"uniform float uBloomStrength; uniform float uExposure;\n"
"vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }\n"
"float hash(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }\n"
"void main(){\n"
"  vec3 c=texture(uTex,vUV).rgb; vec3 b=texture(uBloomTex,vUV).rgb;\n"
"  c += b*uBloomStrength;\n"
"  c *= uExposure;\n"
"  c = aces(c);\n"
"  vec2 q=vUV-0.5; float vig=1.0-smoothstep(0.28,0.8,length(q))*0.45;\n"
"  c *= vig;\n"
"  c = pow(c, vec3(1.0/2.2));\n"
"  float grain=hash(vUV*uRes+uTime*60.0)-0.5; c += grain*0.016;\n"
"  c += vec3(0.004,0.007,0.012);\n"
"  frag=vec4(c,1.0);\n"
"}\n";

typedef struct FxVert { float x,y,z,u,v,r,g,b,a; } FxVert;
typedef struct LineVert { float x,y,z,r,g,b,a; } LineVert;

static GLuint make_fx_texture(void){
    const int S=128; unsigned char* d=malloc(S*S*4);
    for(int y=0;y<S;y++)for(int x=0;x<S;x++){
        float dx=(x-63.5f)/63.5f, dy=(y-63.5f)/63.5f; float r=sqrtf(dx*dx+dy*dy);
        float a=powf(saturate(1.0f-r),2.6f); float core=powf(saturate(1.0f-r*3.5f),2.0f);
        unsigned char* p=d+(y*S+x)*4; p[0]=255; p[1]=255; p[2]=255; p[3]=(unsigned char)(clampf(a+core*0.6f,0,1)*255);
    }
    GLuint t=texture_create_rgba(S,S,d,false,false); free(d); return t;
}

bool renderer_init(Renderer* r,int w,int h){
    memset(r,0,sizeof(*r)); r->w=w; r->h=h;
    r->bloom_strength=0.55f;
    r->exposure=1.08f;
    r->prog_scene=shader_create(SCENE_VS,SCENE_FS,"scene");
    r->prog_sky=shader_create(SKY_VS,SKY_FS,"sky");
    r->prog_fx=shader_create(FX_VS,FX_FS,"fx");
    r->prog_line=shader_create(LINE_VS,LINE_FS,"line");
    r->prog_bright=shader_create(FS_VS,BRIGHT_FS,"bright");
    r->prog_blur=shader_create(FS_VS,BLUR_FS,"blur");
    r->prog_composite=shader_create(FS_VS,COMP_FS,"composite");
    if(!r->prog_scene||!r->prog_sky||!r->prog_fx||!r->prog_line||!r->prog_bright||!r->prog_blur||!r->prog_composite){ fprintf(stderr,"[render] shader init failed\n"); return false; }
    r->cube=mesh_cube(); r->sphere=mesh_sphere(20,14); r->cylinder=mesh_cylinder(20,true); r->cone=mesh_cone(20); r->ramp=mesh_ramp(); r->quad=mesh_quad();
    r->world_tex=make_world_atlas(); r->fx_tex=make_fx_texture();
    r->hdr=fbo_create(w,h,true,true); r->bloomA=fbo_create(w/2,h/2,true,false); r->bloomB=fbo_create(w/2,h/2,true,false);
    glGenVertexArrays(1,&r->fs_vao);
    r->fx_cap=65536; r->fx_alpha_cap=32768; r->fx_verts=malloc(sizeof(FxVert)*r->fx_cap); r->fx_alpha_verts=malloc(sizeof(FxVert)*r->fx_alpha_cap);
    glGenVertexArrays(1,&r->fx_vao); glGenBuffers(1,&r->fx_vbo);
    glBindVertexArray(r->fx_vao); glBindBuffer(GL_ARRAY_BUFFER,r->fx_vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)r->fx_cap*sizeof(FxVert),NULL,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(FxVert),(void*)offsetof(FxVert,x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(FxVert),(void*)offsetof(FxVert,u));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(FxVert),(void*)offsetof(FxVert,r));
    glBindVertexArray(0);
    r->line_cap=131072; r->line_verts=malloc(sizeof(LineVert)*r->line_cap);
    glGenVertexArrays(1,&r->line_vao); glGenBuffers(1,&r->line_vbo);
    glBindVertexArray(r->line_vao); glBindBuffer(GL_ARRAY_BUFFER,r->line_vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)r->line_cap*sizeof(LineVert),NULL,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(LineVert),(void*)offsetof(LineVert,x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(LineVert),(void*)offsetof(LineVert,r));
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    return true;
}
void renderer_destroy(Renderer* r){
    mesh_free(&r->cube); mesh_free(&r->sphere); mesh_free(&r->cylinder); mesh_free(&r->cone); mesh_free(&r->ramp); mesh_free(&r->quad);
    fbo_free(&r->hdr); fbo_free(&r->bloomA); fbo_free(&r->bloomB);
    texture_free(&r->world_tex); texture_free(&r->fx_tex);
    if(r->prog_scene) glDeleteProgram(r->prog_scene);
    if(r->prog_sky) glDeleteProgram(r->prog_sky);
    if(r->prog_fx) glDeleteProgram(r->prog_fx);
    if(r->prog_line) glDeleteProgram(r->prog_line);
    if(r->prog_bright) glDeleteProgram(r->prog_bright);
    if(r->prog_blur) glDeleteProgram(r->prog_blur);
    if(r->prog_composite) glDeleteProgram(r->prog_composite);
    if(r->fx_vao) glDeleteVertexArrays(1,&r->fx_vao);
    if(r->fx_vbo) glDeleteBuffers(1,&r->fx_vbo);
    if(r->line_vao) glDeleteVertexArrays(1,&r->line_vao);
    if(r->line_vbo) glDeleteBuffers(1,&r->line_vbo);
    if(r->fs_vao) glDeleteVertexArrays(1,&r->fs_vao);
    free(r->fx_verts); free(r->fx_alpha_verts); free(r->line_verts); memset(r,0,sizeof(*r));
}
void renderer_resize(Renderer* r,int w,int h){
    if(w<1) w=1;
    if(h<1) h=1;
    if(w==r->w&&h==r->h) return;
    r->w=w; r->h=h; fbo_resize(&r->hdr,w,h,true); fbo_resize(&r->bloomA,w/2,h/2,true); fbo_resize(&r->bloomB,w/2,h/2,true);
}

void renderer_begin(Renderer* r,Camera cam,World* w,int vx,int vy,int vw,int vh,float time){
    r->vx=vx; r->vy=vy; r->vw=vw; r->vh=vh; r->cam=cam; r->time=time; r->viewmodel=false;
    int win_h=r->hdr.h;
    glBindFramebuffer(GL_FRAMEBUFFER,r->hdr.fbo);
    glViewport(vx,win_h-(vy+vh),vw,vh);
    glEnable(GL_SCISSOR_TEST); glScissor(vx,win_h-(vy+vh),vw,vh);
    glClearColor(0.006f,0.008f,0.012f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
    r->view=mat4_look_at(cam.pos,v3add(cam.pos,cam.fwd),v3(0,1,0));
    r->proj=mat4_perspective(cam.fov*DEG2RAD,(float)vw/fmaxf((float)vh,1),0.03f,240.0f);
    r->fog=w->fog_color; r->fog_density=w->fog_density; r->ambient_sky=w->ambient_sky; r->ambient_ground=w->ambient_ground;
    r->sun_dir=w->sun_dir; r->sun_color=w->sun_color; r->sun_intensity=w->sun_intensity;
    world_collect_lights(w,cam.pos,r->lights,MAX_RENDER_LIGHTS,&r->nlights,time);
    renderer_sky(r);
    glUseProgram(r->prog_scene);
    shader_uniform_mat4(r->prog_scene,"uView",r->view); shader_uniform_mat4(r->prog_scene,"uProj",r->proj);
    shader_uniform1i(r->prog_scene,"uTex",0);
    shader_uniform1i(r->prog_scene,"uWorldUV",1);
    shader_uniform1i(r->prog_scene,"uViewmodel",0);
    shader_uniform_vec3(r->prog_scene,"uCamPos",cam.pos);
    shader_uniform_vec3(r->prog_scene,"uSunDir",r->sun_dir);
    shader_uniform_vec3(r->prog_scene,"uSunColor",v3(r->sun_color.r,r->sun_color.g,r->sun_color.b));
    shader_uniform1f(r->prog_scene,"uSunIntensity",r->sun_intensity);
    shader_uniform_vec3(r->prog_scene,"uAmbientSky",v3(r->ambient_sky.r,r->ambient_sky.g,r->ambient_sky.b));
    shader_uniform_vec3(r->prog_scene,"uAmbientGround",v3(r->ambient_ground.r,r->ambient_ground.g,r->ambient_ground.b));
    shader_uniform_vec3(r->prog_scene,"uFogColor",v3(r->fog.r,r->fog.g,r->fog.b));
    shader_uniform1f(r->prog_scene,"uFogDensity",r->fog_density);
    float lp[16*3], lc[16*3], lr[16];
    for(int i=0;i<MAX_RENDER_LIGHTS;i++){ if(i<r->nlights){ lp[i*3]=r->lights[i].pos.x; lp[i*3+1]=r->lights[i].pos.y; lp[i*3+2]=r->lights[i].pos.z; lc[i*3]=r->lights[i].color.r*r->lights[i].intensity; lc[i*3+1]=r->lights[i].color.g*r->lights[i].intensity; lc[i*3+2]=r->lights[i].color.b*r->lights[i].intensity; lr[i]=r->lights[i].radius; } else { lp[i*3]=lp[i*3+1]=lp[i*3+2]=0; lc[i*3]=lc[i*3+1]=lc[i*3+2]=0; lr[i]=0; } }
    glUniform1i(glGetUniformLocation(r->prog_scene,"uLightCount"),r->nlights);
    glUniform3fv(glGetUniformLocation(r->prog_scene,"uLightPos"),16,lp);
    glUniform3fv(glGetUniformLocation(r->prog_scene,"uLightColor"),16,lc);
    glUniform1fv(glGetUniformLocation(r->prog_scene,"uLightRadius"),16,lr);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->world_tex);
    r->in_scene=true;
}
void renderer_sky(Renderer* r){
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glUseProgram(r->prog_sky);
    shader_uniform_vec3(r->prog_sky,"uFwd",r->cam.fwd); shader_uniform_vec3(r->prog_sky,"uRight",r->cam.right); shader_uniform_vec3(r->prog_sky,"uUp",r->cam.up);
    shader_uniform1f(r->prog_sky,"uTan",tanf(r->cam.fov*DEG2RAD*0.5f)); shader_uniform1f(r->prog_sky,"uAspect",(float)r->vw/fmaxf((float)r->vh,1)); shader_uniform1f(r->prog_sky,"uTime",r->time);
    glBindVertexArray(r->fs_vao); glDrawArrays(GL_TRIANGLES,0,3); glBindVertexArray(0);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
    glUseProgram(r->prog_scene);
}
void renderer_draw_model(Renderer* r,Mesh* m,Mat4 model,Color color,int material,float emissive,float uvscale,bool world_uv){
    glUseProgram(r->prog_scene);
    shader_uniform_mat4(r->prog_scene,"uModel",model);
    shader_uniform_vec3(r->prog_scene,"uColor",v3(color.r,color.g,color.b));
    float tx=(float)(material%4)*0.25f, ty=(float)(material/4)*0.25f;
    shader_uniform2f(r->prog_scene,"uTile",tx,ty);
    shader_uniform1f(r->prog_scene,"uUVScale",uvscale);
    shader_uniform1i(r->prog_scene,"uWorldUV",world_uv?1:0);
    shader_uniform1f(r->prog_scene,"uEmissive",emissive);
    mesh_draw(m);
}
void renderer_draw_world_static(Renderer* r,World* w){
    glUseProgram(r->prog_scene);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->world_tex);
    shader_uniform1i(r->prog_scene,"uViewmodel",0);
    for(int i=0;i<w->count;i++){
        Actor* a=&w->actors[i];
        if(a->type==ACT_TRIGGER||a->type==ACT_LIGHT||a->type==ACT_PLAYER_START||a->type==ACT_PICKUP||a->type==ACT_JUMPPAD||a->type==ACT_LAVA||a->type==ACT_NAV_POINT) continue;
        if(!(a->flags&FLAG_SOLID)&&a->type==ACT_BRUSH&&a->shape==SHAPE_BOX) { /* non-solid decoration still drawn */ }
        float tint=0.92f+0.16f*hash01((uint32_t)(i*2654435761u));
        Color c=col_mul(a->color,tint);
        Mat4 model=mat4_from_trs(a->pos,v3(0,a->yaw,0),a->size);
        Mesh* mesh=&r->cube;
        if(a->shape==SHAPE_RAMP) mesh=&r->ramp;
        else if(a->shape==SHAPE_CYLINDER) mesh=&r->cylinder;
        renderer_draw_model(r,mesh,model,c,a->material,(a->flags&FLAG_EMISSIVE)?1.0f:0.0f,0.38f,true);
    }
    /* light gizmos are drawn by editor */
    glUseProgram(0);
}
void renderer_draw_actor_ghost(Renderer* r,World* w,int actor,Color tint,bool wire){
    (void)wire; if(actor<0||actor>=w->count) return;
    Actor* a=&w->actors[actor];
    Color c=col(tint.r,tint.g,tint.b,0.35f);
    if(a->type==ACT_BRUSH||a->type==ACT_DECOR||a->type==ACT_STATICMESH){
        Mat4 model=mat4_from_trs(a->pos,v3(0,a->yaw,0),a->size);
        Mesh* mesh=&r->cube; if(a->shape==SHAPE_RAMP)mesh=&r->ramp; else if(a->shape==SHAPE_CYLINDER)mesh=&r->cylinder;
        renderer_draw_model(r,mesh,model,c,a->material,0.0f,0.38f,true);
    } else {
        Mat4 model=mat4_from_trs(a->pos,v3(0,a->yaw,0),v3(0.5f,0.5f,0.5f));
        renderer_draw_model(r,&r->sphere,model,c,5,1.0f,1.0f,false);
    }
}
void renderer_begin_viewmodel(Renderer* r){
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(r->prog_scene);
    shader_uniform1i(r->prog_scene,"uViewmodel",1);
    shader_uniform_mat4(r->prog_scene,"uView",mat4_identity());
    float aspect=(float)r->vw/fmaxf((float)r->vh,1);
    shader_uniform_mat4(r->prog_scene,"uProj",mat4_perspective(58.0f*DEG2RAD,aspect,0.01f,10.0f));
    r->viewmodel=true;
}
void renderer_draw_weapon_part(Renderer* r,Mesh* m,Vec3 pos,Vec3 scale,Vec3 rot,Color color,int material,float emissive){
    Mat4 model=mat4_from_trs(pos,rot,scale);
    renderer_draw_model(r,m,model,color,material,emissive,1.0f,false);
}
void renderer_end(Renderer* r){
    if(r->viewmodel){ glUseProgram(r->prog_scene); shader_uniform1i(r->prog_scene,"uViewmodel",0); }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,r->w,r->h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    /* bright */
    glBindFramebuffer(GL_FRAMEBUFFER,r->bloomA.fbo); glViewport(0,0,r->bloomA.w,r->bloomA.h);
    glUseProgram(r->prog_bright); shader_uniform1i(r->prog_bright,"uTex",0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->hdr.color_tex);
    glBindVertexArray(r->fs_vao); glDrawArrays(GL_TRIANGLES,0,3);
    /* blur h */
    glBindFramebuffer(GL_FRAMEBUFFER,r->bloomB.fbo); glViewport(0,0,r->bloomB.w,r->bloomB.h);
    glUseProgram(r->prog_blur); shader_uniform1i(r->prog_blur,"uTex",0); shader_uniform2f(r->prog_blur,"uDir",1.0f/r->bloomA.w,0);
    glBindTexture(GL_TEXTURE_2D,r->bloomA.color_tex); glDrawArrays(GL_TRIANGLES,0,3);
    /* blur v */
    glBindFramebuffer(GL_FRAMEBUFFER,r->bloomA.fbo); glViewport(0,0,r->bloomA.w,r->bloomA.h);
    shader_uniform2f(r->prog_blur,"uDir",0,1.0f/r->bloomB.h);
    glBindTexture(GL_TEXTURE_2D,r->bloomB.color_tex); glDrawArrays(GL_TRIANGLES,0,3);
    /* composite */
    glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,r->w,r->h);
    glUseProgram(r->prog_composite); shader_uniform1i(r->prog_composite,"uTex",0); shader_uniform1i(r->prog_composite,"uBloomTex",1);
    shader_uniform1f(r->prog_composite,"uBloomStrength",r->bloom_strength);
    shader_uniform1f(r->prog_composite,"uExposure",r->exposure);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->hdr.color_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,r->bloomA.color_tex);
    shader_uniform1f(r->prog_composite,"uTime",r->time); shader_uniform2f(r->prog_composite,"uRes",(float)r->w,(float)r->h);
    glDrawArrays(GL_TRIANGLES,0,3); glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    r->in_scene=false;
}

/* ------------ fx ------------ */
void fx_begin(Renderer* r){ r->fx_count=0; r->fx_alpha_count=0; }
static void fx_put(Renderer* r,Vec3 p,float u,float v,Color c,bool additive){
    FxVert** arr=additive?&r->fx_verts:&r->fx_alpha_verts; int* cnt=additive?&r->fx_count:&r->fx_alpha_count; int cap=additive?r->fx_cap:r->fx_alpha_cap;
    if(*cnt>=cap) return;
    FxVert* q=&(*arr)[(*cnt)++];
    q->x=p.x; q->y=p.y; q->z=p.z; q->u=u; q->v=v;
    q->r=c.r; q->g=c.g; q->b=c.b; q->a=c.a;
}
static void fx_quad(Renderer* r,Vec3 c,Vec3 right,Vec3 up,Color col,bool additive){
    fx_put(r,v3add(c,v3add(v3mul(right,-0.5f),v3mul(up,-0.5f))),0,1,col,additive);
    fx_put(r,v3add(c,v3add(v3mul(right, 0.5f),v3mul(up,-0.5f))),1,1,col,additive);
    fx_put(r,v3add(c,v3add(v3mul(right, 0.5f),v3mul(up, 0.5f))),1,0,col,additive);
    fx_put(r,v3add(c,v3add(v3mul(right,-0.5f),v3mul(up,-0.5f))),0,1,col,additive);
    fx_put(r,v3add(c,v3add(v3mul(right, 0.5f),v3mul(up, 0.5f))),1,0,col,additive);
    fx_put(r,v3add(c,v3add(v3mul(right,-0.5f),v3mul(up, 0.5f))),0,0,col,additive);
}
void fx_sprite(Renderer* r,Vec3 pos,float size,Color c,bool additive){
    Vec3 right=v3mul(r->cam.right,size), up=v3mul(r->cam.up,size);
    fx_quad(r,pos,right,up,c,additive);
}
void fx_beam(Renderer* r,Vec3 a,Vec3 b,Color c,float width){
    Vec3 mid=v3mul(v3add(a,b),0.5f); Vec3 dir=v3norm(v3sub(b,a)); Vec3 side=v3norm(v3cross(dir,v3sub(r->cam.pos,mid)));
    Vec3 off=v3mul(side,width*0.5f);
    Vec3 p0=v3sub(a,off),p1=v3add(a,off),p2=v3add(b,off),p3=v3sub(b,off);
    fx_put(r,p0,0,1,c,true); fx_put(r,p1,1,1,c,true); fx_put(r,p2,1,0,c,true);
    fx_put(r,p0,0,1,c,true); fx_put(r,p2,1,0,c,true); fx_put(r,p3,0,0,c,true);
}
void fx_render_split(Renderer* r,bool additive){
    int count=additive?r->fx_count:r->fx_alpha_count;
    FxVert* verts=additive?r->fx_verts:r->fx_alpha_verts;
    if(count<=0)return;
    glUseProgram(r->prog_fx); shader_uniform_mat4(r->prog_fx,"uView",r->view); shader_uniform_mat4(r->prog_fx,"uProj",r->proj);
    shader_uniform1i(r->prog_fx,"uTex",0);
    glBindVertexArray(r->fx_vao); glBindBuffer(GL_ARRAY_BUFFER,r->fx_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)count*sizeof(FxVert),verts);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->fx_tex);
    glEnable(GL_BLEND); glDepthMask(GL_FALSE);
    if(additive) glBlendFunc(GL_SRC_ALPHA,GL_ONE); else glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,count);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glBindVertexArray(0);
}
void fx_render(Renderer* r){
    fx_render_split(r,true);
    fx_render_split(r,false);
    r->fx_count=0; r->fx_alpha_count=0;
}

/* ------------ lines ------------ */
void lines_begin(Renderer* r){ r->line_count=0; }
void line_add(Renderer* r,Vec3 a,Vec3 b,Color c){
    if(r->line_count+2>r->line_cap)return;
    LineVert* p=&r->line_verts[r->line_count++]; p->x=a.x;p->y=a.y;p->z=a.z;p->r=c.r;p->g=c.g;p->b=c.b;p->a=c.a;
    p=&r->line_verts[r->line_count++]; p->x=b.x;p->y=b.y;p->z=b.z;p->r=c.r;p->g=c.g;p->b=c.b;p->a=c.a;
}
void line_box(Renderer* r,AABB b,Color c){
    Vec3 p[8]={{b.min.x,b.min.y,b.min.z},{b.max.x,b.min.y,b.min.z},{b.max.x,b.min.y,b.max.z},{b.min.x,b.min.y,b.max.z},{b.min.x,b.max.y,b.min.z},{b.max.x,b.max.y,b.min.z},{b.max.x,b.max.y,b.max.z},{b.min.x,b.max.y,b.max.z}};
    int e[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for(int i=0;i<12;i++) line_add(r,p[e[i][0]],p[e[i][1]],c);
}
void line_box_corners(Renderer* r,AABB b,Color c,float len){
    Vec3 p[8]={{b.min.x,b.min.y,b.min.z},{b.max.x,b.min.y,b.min.z},{b.max.x,b.min.y,b.max.z},{b.min.x,b.min.y,b.max.z},{b.min.x,b.max.y,b.min.z},{b.max.x,b.max.y,b.min.z},{b.max.x,b.max.y,b.max.z},{b.min.x,b.max.y,b.max.z}};
    Vec3 s=v3(b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z);
    float lx=fminf(len,s.x*0.35f),ly=fminf(len,s.y*0.35f),lz=fminf(len,s.z*0.35f);
    for(int i=0;i<8;i++){
        Vec3 q=p[i];
        float sx=(i&1)?-1:1, sy=(i&2)?-1:1, sz=(i&4)?-1:1;
        line_add(r,q,v3(q.x+sx*lx,q.y,q.z),c); line_add(r,q,v3(q.x,q.y+sy*ly,q.z),c); line_add(r,q,v3(q.x,q.y,q.z+sz*lz),c);
    }
}
void line_axis(Renderer* r,Vec3 o,Vec3 dir,float len,Color c){ line_add(r,o,v3add(o,v3mul(dir,len)),c); }
void line_circle(Renderer* r,Vec3 center,Vec3 axis,float radius,Color c,int seg){
    Vec3 a=fabsf(axis.y)>0.9f?v3(1,0,0):v3(0,1,0); Vec3 u=v3norm(v3cross(axis,a)), v=v3norm(v3cross(axis,u));
    Vec3 prev=v3add(center,v3mul(u,radius));
    for(int i=1;i<=seg;i++){ float t=2*PI*i/seg; Vec3 p=v3add(center,v3add(v3mul(u,cosf(t)*radius),v3mul(v,sinf(t)*radius))); line_add(r,prev,p,c); prev=p; }
}
void lines_render(Renderer* r,bool depth_test,float width){
    if(r->line_count<=0)return;
    glUseProgram(r->prog_line); shader_uniform_mat4(r->prog_line,"uView",r->view); shader_uniform_mat4(r->prog_line,"uProj",r->proj);
    glBindVertexArray(r->line_vao); glBindBuffer(GL_ARRAY_BUFFER,r->line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)r->line_count*sizeof(LineVert),r->line_verts);
    if(depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(width);
    glDrawArrays(GL_LINES,0,r->line_count);
    glLineWidth(1); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glBindVertexArray(0);
}
