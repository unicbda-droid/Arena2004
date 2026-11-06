#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "ui.h"
#include "gl_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

stbtt_bakedchar g_baked[96];

UiTheme ui_theme = {
    .panel  = {0.075f,0.085f,0.100f,0.97f},
    .panel2 = {0.105f,0.118f,0.135f,1.0f},
    .header = {0.135f,0.150f,0.170f,1.0f},
    .border = {0.230f,0.255f,0.285f,1.0f},
    .text   = {0.820f,0.850f,0.880f,1.0f},
    .dim    = {0.470f,0.510f,0.560f,1.0f},
    .accent = {0.980f,0.600f,0.140f,1.0f},
    .accent2= {0.250f,0.620f,0.980f,1.0f},
    .good   = {0.390f,0.820f,0.430f,1.0f},
    .bad    = {0.900f,0.270f,0.220f,1.0f},
    .black  = {0.020f,0.022f,0.026f,1.0f},
    .white  = {1.0f,1.0f,1.0f,1.0f},
};

static const char* UI_VS =
"#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec4 aCol;\n"
"uniform vec2 uScreen;\n"
"out vec2 vUV; out vec4 vCol;\n"
"void main(){ vUV=aUV; vCol=aCol; vec2 p=vec2(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0); gl_Position=vec4(p,0,1); }\n";
static const char* UI_FS =
"#version 330 core\n"
"in vec2 vUV; in vec4 vCol; out vec4 frag;\n"
"uniform sampler2D uTex;\n"
"void main(){ vec4 t=texture(uTex,vUV); frag=vec4(vCol.rgb*t.rgb, vCol.a*t.a); if(frag.a<0.002) discard; }\n";

static unsigned int fnv(const char* s){ unsigned int h=2166136261u; while(*s){ h^=(unsigned char)*s++; h*=16777619u; } return h; }
static void add_vert(Ui* ui,float x,float y,float u,float v,Color c){
    if(ui->vcount>=ui->vbo_cap) return;
    UiVert* q=&ui->verts[ui->vcount++]; q->x=x;q->y=y;q->u=u;q->v=v;q->r=c.r;q->g=c.g;q->b=c.b;q->a=c.a;
}
static void add_quad_uv(Ui* ui,float x,float y,float w,float h,float u0,float v0,float u1,float v1,Color c){
    add_vert(ui,x,y,u0,v0,c); add_vert(ui,x+w,y,u1,v0,c); add_vert(ui,x+w,y+h,u1,v1,c);
    add_vert(ui,x,y,u0,v0,c); add_vert(ui,x+w,y+h,u1,v1,c); add_vert(ui,x,y+h,u0,v1,c);
}

bool ui_init(Ui* ui,const char* font_path,int w,int h){
    memset(ui,0,sizeof(*ui)); ui->w=w; ui->h=h; ui->font_bake=40;
    unsigned char* ttf=NULL; long ttf_len=0;
    const char* paths[4]={font_path,"assets/fonts/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for(int i=0;i<4;i++){ if(!paths[i]) continue; FILE* f=fopen(paths[i],"rb"); if(!f) continue; fseek(f,0,SEEK_END); ttf_len=ftell(f); fseek(f,0,SEEK_SET); ttf=malloc(ttf_len); fread(ttf,1,ttf_len,f); fclose(f); if(ttf) break; }
    if(!ttf){ fprintf(stderr,"[ui] no font found\n"); return false; }
    const int AW=1024,AH=1024;
    unsigned char* alpha=calloc(AW*AH,1);
    stbtt_fontinfo info;
    if(!stbtt_InitFont(&info,ttf,0)){ fprintf(stderr,"[ui] bad font\n"); free(ttf); free(alpha); return false; }
    stbtt_bakedchar cdata[96];
    int bw=stbtt_BakeFontBitmap(ttf,0,40.0f,alpha,AW,AH,32,96,cdata);
    if(bw<=0){ fprintf(stderr,"[ui] font bake failed\n"); free(ttf); free(alpha); return false; }
    int asc,desc,gap; stbtt_GetFontVMetrics(&info,&asc,&desc,&gap);
    float sc=stbtt_ScaleForPixelHeight(&info,40.0f);
    ui->ascent=asc*sc; ui->descent=desc*sc; ui->line_gap=gap*sc;
    /* extra atlas regions below font */
    int by=820;
    int wx=0,wy=by,ww=16;
    for(int y=0;y<ww;y++)for(int x=0;x<ww;x++) alpha[(wy+y)*AW+(wx+x)]=255;
    int gx=32,gy=by,gs=128;
    for(int y=0;y<gs;y++)for(int x=0;x<gs;x++){ float dx=(x-63.5f)/63.5f,dy=(y-63.5f)/63.5f; float r=sqrtf(dx*dx+dy*dy); float a=powf(saturate(1-r),2.0f); alpha[(gy+y)*AW+(gx+x)]=clampf(ceilf(a*255),0,255); }
    int rx=176,ry=by,rs=128;
    for(int y=0;y<rs;y++)for(int x=0;x<rs;x++){ float dx=(x-63.5f)/63.5f,dy=(y-63.5f)/63.5f; float r=sqrtf(dx*dx+dy*dy); float a=saturate(1-fabsf(r-0.82f)/0.06f); a*=a; alpha[(ry+y)*AW+(rx+x)]=clampf(ceilf(a*255),0,255); }
    int bx=320,bys=by;
    for(int y=0;y<64;y++)for(int x=0;x<64;x++) alpha[(bys+y)*AW+(bx+x)]=255-(unsigned char)(y*4);
    int sx=400,sy=by;
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){ float b=fmodf((x+y)*0.12f,1.0f); alpha[(sy+y)*AW+(sx+x)]=(b<0.5f)?255:0; }
    unsigned char* rgba=malloc((size_t)AW*AH*4);
    for(int i=0;i<AW*AH;i++){ rgba[i*4+0]=255; rgba[i*4+1]=255; rgba[i*4+2]=255; rgba[i*4+3]=alpha[i]; }
    glGenTextures(1,&ui->tex); glBindTexture(GL_TEXTURE_2D,ui->tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,AW,AH,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);
    ui->tex_w=AW; ui->tex_h=AH;
    ui->white_u=(float)wx/AW; ui->white_v=(float)wy/AH; ui->white_du=ww/(float)AW; ui->white_dv=ww/(float)AH;
    ui->glow_u=(float)gx/AW; ui->glow_v=(float)gy/AH; ui->glow_du=gs/(float)AW; ui->glow_dv=gs/(float)AH;
    ui->ring_u=(float)rx/AW; ui->ring_v=(float)ry/AH; ui->ring_du=rs/(float)AW; ui->ring_dv=rs/(float)AH;
    ui->bar_u=(float)bx/AW; ui->bar_v=(float)bys/AH; ui->bar_du=64.0f/AW; ui->bar_dv=64.0f/AH;
    ui->stripe_u=(float)sx/AW; ui->stripe_v=(float)sy/AH; ui->stripe_du=64.0f/AW; ui->stripe_dv=64.0f/AH;
    /* cache baked glyphs in a static per-ui? store in global (one ui) */
    memcpy(ui->edit_buf,"",1);
    memcpy(g_baked,cdata,sizeof(cdata));
    ui->prog=shader_create(UI_VS,UI_FS,"ui");
    ui->vbo_cap=65536; ui->verts=malloc(sizeof(UiVert)*ui->vbo_cap);
    glGenVertexArrays(1,&ui->vao); glGenBuffers(1,&ui->vbo);
    glBindVertexArray(ui->vao); glBindBuffer(GL_ARRAY_BUFFER,ui->vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)ui->vbo_cap*sizeof(UiVert),NULL,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(UiVert),(void*)offsetof(UiVert,x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(UiVert),(void*)offsetof(UiVert,u));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(UiVert),(void*)offsetof(UiVert,r));
    glBindVertexArray(0);
    free(ttf); free(alpha); free(rgba);
    return true;
}
void ui_destroy(Ui* ui){ if(ui->tex)glDeleteTextures(1,&ui->tex); if(ui->prog)glDeleteProgram(ui->prog); if(ui->vao)glDeleteVertexArrays(1,&ui->vao); if(ui->vbo)glDeleteBuffers(1,&ui->vbo); free(ui->verts); memset(ui,0,sizeof(*ui)); }

void ui_flush(Ui* ui){
    if(ui->vcount<=0) return;
    glUseProgram(ui->prog);
    glBindVertexArray(ui->vao);
    glBindBuffer(GL_ARRAY_BUFFER,ui->vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)ui->vcount*sizeof(UiVert),ui->verts);
    shader_uniform2f(ui->prog,"uScreen",(float)ui->w,(float)ui->h);
    shader_uniform1i(ui->prog,"uTex",0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,ui->tex);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES,0,ui->vcount);
    glBindVertexArray(0);
    ui->vcount=0;
}
void ui_frame_begin(Ui* ui,int w,int h,UiInput in){
    ui->w=w; ui->h=h; ui->in=in; ui->vcount=0;
    ui->hot=0; ui->mouse_over_ui=false;
    /* Keep the active widget alive through the release frame. The button is
     * only activated on release (clickable()), so clearing active here as
     * soon as the button is up made real mouse clicks never fire. */
    if(!in.down && !in.released) ui->active=0;
    if(in.text_len>0 && ui->focus){ for(int i=0;i<in.text_len&&ui->edit_len<255;i++){ unsigned char c=in.text[i]; if(c>=32&&c<127) ui->edit_buf[ui->edit_len++]=(char)c; } ui->edit_buf[ui->edit_len]=0; }
    if(ui->focus){
        if(in.backspace && ui->edit_len>0) ui->edit_buf[--ui->edit_len]=0;
        if(in.escape){ ui->focus=0; ui->edit_len=0; }
    }
    /* TAB cycles keyboard focus and reports a cursor warp target */
    if(ui->nav_tab_cycle && in.tab && !ui->focus && ui->nav_prev_count>0){
        int idx=-1;
        for(int i=0;i<ui->nav_prev_count;i++) if(ui->nav_prev[i].id==ui->nav_focus){ idx=i; break; }
        idx=(idx+1)%ui->nav_prev_count;
        UiNavItem* it=&ui->nav_prev[idx];
        ui->nav_focus=it->id;
        ui->nav_warp=true;
        ui->nav_warp_x=it->x+it->w*0.5f;
        ui->nav_warp_y=it->y+it->h*0.5f;
        ui->nav_engaged=true;
    }
    ui->nav_count=0;
}
void ui_frame_end(Ui* ui){
    if(ui->focus && ui->in.enter){ ui->edit_commit=true; }
    if(ui->focus && (ui->edit_commit)){
        /* caller consumes by copying in textbox; generic fallback: keep */
    }
    /* focus ring for keyboard cursor */
    if(ui->nav_engaged && ui->nav_focus){
        for(int i=0;i<ui->nav_count;i++){
            UiNavItem* it=&ui->nav_items[i];
            if(it->id!=ui->nav_focus) continue;
            Color ring=col(ui_theme.accent.r,ui_theme.accent.g,ui_theme.accent.b,0.95f);
            ui_rect_outline(ui,it->x-2,it->y-2,it->w+4,it->h+4,col(0,0,0,0.35f),1);
            ui_rect_outline(ui,it->x-1,it->y-1,it->w+2,it->h+2,ring,1);
            float s=5;
            ui_rect(ui,it->x-2,it->y-2,s,2,ring); ui_rect(ui,it->x-2,it->y-2,2,s,ring);
            ui_rect(ui,it->x+it->w+2-s,it->y-2,s,2,ring); ui_rect(ui,it->x+it->w,it->y-2,2,s,ring);
            ui_rect(ui,it->x-2,it->y+it->h,s,2,ring); ui_rect(ui,it->x-2,it->y+it->h+2-s,2,s,ring);
            ui_rect(ui,it->x+it->w+2-s,it->y+it->h,s,2,ring); ui_rect(ui,it->x+it->w,it->y+it->h+2-s,2,s,ring);
            break;
        }
    }
    if(ui->nav_count>0) memcpy(ui->nav_prev,ui->nav_items,sizeof(UiNavItem)*(size_t)ui->nav_count);
    ui->nav_prev_count=ui->nav_count;
    ui->nav_count=0;
    /* Release edge has been consumed by the widgets above; never leave a
     * widget stuck in the active/pressed state. */
    if(!ui->in.down) ui->active=0;
    ui_flush(ui);
}
bool ui_nav_take_warp(Ui* ui,float* x,float* y){
    if(!ui->nav_warp) return false;
    ui->nav_warp=false;
    if(x)*x=ui->nav_warp_x;
    if(y)*y=ui->nav_warp_y;
    return true;
}
void ui_push_clip(Ui* ui,int x,int y,int w,int h){
    ui_flush(ui);
    if(w<0) w=0;
    if(h<0) h=0;
    ui->clip[0]=x; ui->clip[1]=y; ui->clip[2]=w; ui->clip[3]=h; ui->clipping=true;
    glEnable(GL_SCISSOR_TEST); glScissor(x,ui->h-(y+h),w,h);
}
void ui_pop_clip(Ui* ui){ ui_flush(ui); ui->clipping=false; glDisable(GL_SCISSOR_TEST); }

void ui_rect(Ui* ui,float x,float y,float w,float h,Color c){ add_quad_uv(ui,x,y,w,h,ui->white_u,ui->white_v,ui->white_u+ui->white_du,ui->white_v+ui->white_dv,c); }
void ui_rect_grad(Ui* ui,float x,float y,float w,float h,Color top,Color bot){
    add_vert(ui,x,y,ui->white_u,ui->white_v,top); add_vert(ui,x+w,y,ui->white_u+ui->white_du,ui->white_v,top); add_vert(ui,x+w,y+h,ui->white_u+ui->white_du,ui->white_v+ui->white_dv,bot);
    add_vert(ui,x,y,ui->white_u,ui->white_v,top); add_vert(ui,x+w,y+h,ui->white_u+ui->white_du,ui->white_v+ui->white_dv,bot); add_vert(ui,x,y+h,ui->white_u,ui->white_v+ui->white_dv,bot);
}
void ui_rect_outline(Ui* ui,float x,float y,float w,float h,Color c,float t){ ui_rect(ui,x,y,w,t,c); ui_rect(ui,x,y+h-t,w,t,c); ui_rect(ui,x,y,t,h,c); ui_rect(ui,x+w-t,y,t,h,c); }
static inline Vec2 v2neg(Vec2 v){ return v2(-v.x,-v.y); }
void ui_tri(Ui* ui,Vec2 a,Vec2 b,Vec2 c,Color col){ add_vert(ui,a.x,a.y,ui->white_u,ui->white_v,col); add_vert(ui,b.x,b.y,ui->white_u,ui->white_v,col); add_vert(ui,c.x,c.y,ui->white_u,ui->white_v,col); }
void ui_line(Ui* ui,Vec2 a,Vec2 b,Color c,float t){
    Vec2 d=v2sub(b,a); float l=v2len(d); if(l<0.001f)return; Vec2 n=v2(-d.y/l*t*0.5f,d.x/l*t*0.5f);
    ui_tri(ui,v2add(a,n),v2add(a,v2neg(n)),v2add(b,n),c); ui_tri(ui,v2add(b,n),v2add(a,v2neg(n)),v2add(b,v2neg(n)),c);
}
void ui_glow(Ui* ui,float x,float y,float w,float h,Color c){ add_quad_uv(ui,x,y,w,h,ui->glow_u,ui->glow_v,ui->glow_u+ui->glow_du,ui->glow_v+ui->glow_dv,c); }
void ui_ring(Ui* ui,float x,float y,float w,float h,Color c){ add_quad_uv(ui,x,y,w,h,ui->ring_u,ui->ring_v,ui->ring_u+ui->ring_du,ui->ring_v+ui->ring_dv,c); }

void ui_text(Ui* ui,float x,float top,const char* text,float size,Color c){
    float sc=size/(float)ui->font_bake; float bx=x; float by=top+ui->ascent*sc;
    for(const unsigned char* p=(const unsigned char*)text;*p;p++){
        if(*p=='\n'){ bx=x; by+= (ui->ascent-ui->descent+ui->line_gap)*sc; continue; }
        if(*p<32||*p>127) continue;
        stbtt_aligned_quad q; float qx=bx/sc, qy=by/sc;
        stbtt_GetBakedQuad(g_baked,ui->tex_w,ui->tex_h,*p-32,&qx,&qy,&q,1);
        bx=qx*sc; by=qy*sc;
        add_quad_uv(ui,q.x0*sc,q.y0*sc,(q.x1-q.x0)*sc,(q.y1-q.y0)*sc,q.s0,q.t0,q.s1,q.t1,c);
    }
}
void ui_text_shadow(Ui* ui,float x,float top,const char* text,float size,Color c,Color sh){ ui_text(ui,x+1.5f,top+1.5f,text,size,sh); ui_text(ui,x,top,text,size,c); }
float ui_text_width(Ui* ui,const char* text,float size){
    float sc=size/(float)ui->font_bake; float bx=0; float by=0; float maxw=0;
    for(const unsigned char* p=(const unsigned char*)text;*p;p++){
        if(*p=='\n'){ if(bx>maxw)maxw=bx; bx=0; continue; }
        if(*p<32||*p>127) continue;
        stbtt_aligned_quad q; float qx=bx/sc,qy=by/sc; stbtt_GetBakedQuad(g_baked,ui->tex_w,ui->tex_h,*p-32,&qx,&qy,&q,1); bx=qx*sc; by=qy*sc;
    }
    if(bx>maxw) maxw=bx;
    return maxw;
}
void ui_text_center(Ui* ui,float cx,float top,const char* text,float size,Color c){ ui_text(ui,cx-ui_text_width(ui,text,size)*0.5f,top,text,size,c); }
void ui_text_right(Ui* ui,float rx,float top,const char* text,float size,Color c){ ui_text(ui,rx-ui_text_width(ui,text,size),top,text,size,c); }

bool ui_mouse_in(Ui* ui,float x,float y,float w,float h){ return ui->in.mx>=x&&ui->in.mx<=x+w&&ui->in.my>=y&&ui->in.my<=y+h; }

void ui_icon(Ui* ui,int icon,float x,float y,float s,Color c){
    float h=s*0.5f;
    switch(icon){
        case ICON_PLAY: ui_tri(ui,v2(x+s*0.2f,y+s*0.1f),v2(x+s*0.2f,y+s*0.9f),v2(x+s*0.9f,y+h),c); break;
        case ICON_STOP: ui_rect(ui,x+s*0.18f,y+s*0.18f,s*0.64f,s*0.64f,c); break;
        case ICON_SAVE: ui_rect(ui,x+s*0.15f,y+s*0.12f,s*0.7f,s*0.76f,c); ui_rect(ui,x+s*0.28f,y+s*0.14f,s*0.44f,s*0.28f,col(0.1f,0.1f,0.12f,1)); ui_rect(ui,x+s*0.28f,y+s*0.6f,s*0.44f,s*0.2f,col(0.1f,0.1f,0.12f,1)); break;
        case ICON_FOLDER: ui_rect(ui,x+s*0.1f,y+s*0.3f,s*0.8f,s*0.55f,c); ui_rect(ui,x+s*0.12f,y+s*0.18f,s*0.35f,s*0.18f,c); break;
        case ICON_CUBE: { Vec2 a=v2(x+h,y+s*0.08f),b=v2(x+s*0.9f,y+s*0.3f),cc=v2(x+s*0.9f,y+s*0.75f),d=v2(x+h,y+s*0.95f),e=v2(x+s*0.1f,y+s*0.75f),f=v2(x+s*0.1f,y+s*0.3f); ui_line(ui,a,b,c,s*0.07f);ui_line(ui,b,cc,c,s*0.07f);ui_line(ui,cc,d,c,s*0.07f);ui_line(ui,d,e,c,s*0.07f);ui_line(ui,e,f,c,s*0.07f);ui_line(ui,f,a,c,s*0.07f);ui_line(ui,a,d,col_mul(c,0.7f),s*0.05f);ui_line(ui,b,e,col_mul(c,0.7f),s*0.05f);ui_line(ui,cc,f,col_mul(c,0.7f),s*0.05f); } break;
        case ICON_LIGHT: ui_glow(ui,x,y,s,s,col(c.r,c.g,c.b,0.45f)); ui_rect(ui,x+h-s*0.1f,y+s*0.35f,s*0.2f,s*0.32f,c); ui_rect(ui,x+h-s*0.16f,y+s*0.7f,s*0.32f,s*0.12f,c); break;
        case ICON_PICKUP: ui_rect(ui,x+s*0.18f,y+s*0.3f,s*0.64f,s*0.55f,c); ui_rect(ui,x+s*0.3f,y+s*0.18f,s*0.4f,s*0.16f,c); ui_rect(ui,x+s*0.46f,y+s*0.45f,s*0.08f,s*0.28f,col(0.1f,0.1f,0.1f,1)); break;
        case ICON_SPAWN: ui_ring(ui,x+s*0.1f,y+s*0.1f,s*0.8f,s*0.8f,c); ui_tri(ui,v2(x+h-s*0.16f,y+s*0.2f),v2(x+h+s*0.16f,y+s*0.2f),v2(x+h,y+s*0.5f),c); break;
        case ICON_JUMP: ui_tri(ui,v2(x+s*0.2f,y+s*0.6f),v2(x+s*0.8f,y+s*0.6f),v2(x+s*0.5f,y+s*0.2f),c); ui_rect(ui,x+s*0.42f,y+s*0.62f,s*0.16f,s*0.25f,c); break;
        case ICON_LAVA: ui_tri(ui,v2(x+s*0.08f,y+s*0.8f),v2(x+s*0.3f,y+s*0.2f),v2(x+s*0.5f,y+s*0.8f),c); ui_tri(ui,v2(x+s*0.4f,y+s*0.8f),v2(x+s*0.65f,y+s*0.35f),v2(x+s*0.92f,y+s*0.8f),c); break;
        case ICON_MOVE: { ui_rect(ui,x+h-1.5f,y+s*0.15f,3,s*0.7f,c); ui_rect(ui,x+s*0.15f,y+h-1.5f,s*0.7f,3,c); ui_tri(ui,v2(x+h,y+s*0.02f),v2(x+h-5,y+s*0.16f),v2(x+h+5,y+s*0.16f),c); ui_tri(ui,v2(x+h,y+s*0.98f),v2(x+h-5,y+s*0.84f),v2(x+h+5,y+s*0.84f),c); ui_tri(ui,v2(x+s*0.02f,y+h),v2(x+s*0.16f,y+h-5),v2(x+s*0.16f,y+h+5),c); ui_tri(ui,v2(x+s*0.98f,y+h),v2(x+s*0.84f,y+h-5),v2(x+s*0.84f,y+h+5),c); } break;
        case ICON_ROTATE: ui_ring(ui,x+s*0.12f,y+s*0.12f,s*0.76f,s*0.76f,c); ui_tri(ui,v2(x+s*0.82f,y+s*0.16f),v2(x+s*0.98f,y+s*0.1f),v2(x+s*0.88f,y+s*0.3f),c); break;
        case ICON_SCALE: ui_line(ui,v2(x+s*0.2f,y+s*0.8f),v2(x+s*0.8f,y+s*0.2f),c,2); ui_rect(ui,x+s*0.72f,y+s*0.1f,s*0.16f,s*0.16f,c); ui_rect(ui,x+s*0.1f,y+s*0.72f,s*0.16f,s*0.16f,c); break;
        case ICON_SELECT: { ui_line(ui,v2(x+s*0.25f,y+s*0.1f),v2(x+s*0.25f,y+s*0.8f),c,2); ui_line(ui,v2(x+s*0.25f,y+s*0.8f),v2(x+s*0.75f,y+s*0.55f),c,2); ui_line(ui,v2(x+s*0.25f,y+s*0.55f),v2(x+s*0.55f,y+s*0.95f),c,2); } break;
        case ICON_GRID: for(int i=1;i<4;i++){ ui_rect(ui,x+s*0.12f+i*s*0.2f,y+s*0.12f,1,s*0.76f,col(c.r,c.g,c.b,0.7f)); ui_rect(ui,x+s*0.12f,y+s*0.12f+i*s*0.2f,s*0.76f,1,col(c.r,c.g,c.b,0.7f)); } break;
        case ICON_UNDO: { ui_line(ui,v2(x+s*0.15f,y+s*0.45f),v2(x+s*0.55f,y+s*0.2f),c,3); ui_line(ui,v2(x+s*0.55f,y+s*0.2f),v2(x+s*0.85f,y+s*0.45f),c,3); ui_line(ui,v2(x+s*0.55f,y+s*0.2f),v2(x+s*0.55f,y+s*0.8f),c,3); } break;
        case ICON_REDO: { ui_line(ui,v2(x+s*0.85f,y+s*0.45f),v2(x+s*0.45f,y+s*0.2f),c,3); ui_line(ui,v2(x+s*0.45f,y+s*0.2f),v2(x+s*0.15f,y+s*0.45f),c,3); ui_line(ui,v2(x+s*0.45f,y+s*0.2f),v2(x+s*0.45f,y+s*0.8f),c,3); } break;
        case ICON_TRASH: ui_rect(ui,x+s*0.25f,y+s*0.3f,s*0.5f,s*0.55f,c); ui_rect(ui,x+s*0.18f,y+s*0.2f,s*0.64f,s*0.08f,c); ui_rect(ui,x+s*0.4f,y+s*0.1f,s*0.2f,s*0.1f,c); break;
        case ICON_COPY: ui_rect(ui,x+s*0.18f,y+s*0.18f,s*0.45f,s*0.55f,c); ui_rect_outline(ui,x+s*0.38f,y+s*0.3f,s*0.44f,s*0.55f,c,1); break;
        case ICON_CLOSE: ui_line(ui,v2(x+s*0.2f,y+s*0.2f),v2(x+s*0.8f,y+s*0.8f),c,2); ui_line(ui,v2(x+s*0.8f,y+s*0.2f),v2(x+s*0.2f,y+s*0.8f),c,2); break;
        case ICON_SEARCH: ui_ring(ui,x+s*0.12f,y+s*0.12f,s*0.55f,s*0.55f,c); ui_line(ui,v2(x+s*0.62f,y+s*0.62f),v2(x+s*0.9f,y+s*0.9f),c,3); break;
        case ICON_MUSIC: ui_rect(ui,x+s*0.6f,y+s*0.15f,3,s*0.45f,c); ui_rect(ui,x+s*0.3f,y+s*0.15f,3,s*0.45f,c); ui_rect(ui,x+s*0.3f,y+s*0.15f,s*0.33f,3,c); ui_glow(ui,x+s*0.12f,y+s*0.55f,s*0.3f,s*0.3f,col(c.r,c.g,c.b,0.7f)); break;
        default: ui_rect(ui,x+s*0.2f,y+s*0.2f,s*0.6f,s*0.6f,c); break;
    }
}

static void nav_register(Ui* ui,unsigned int hsh,float x,float y,float w,float h,int kind){
    for(int i=0;i<ui->nav_count;i++) if(ui->nav_items[i].id==hsh) return;
    if(ui->nav_count>=256) return;
    UiNavItem* it=&ui->nav_items[ui->nav_count++];
    it->id=hsh; it->x=x; it->y=y; it->w=w; it->h=h; it->kind=(unsigned char)kind;
}
static bool clickable(Ui* ui,const char* id,float x,float y,float w,float h){
    bool over=ui_mouse_in(ui,x,y,w,h);
    unsigned int hsh=fnv(id);
    nav_register(ui,hsh,x,y,w,h,UI_NAV_BUTTON);
    if(over){ ui->hot=hsh; ui->mouse_over_ui=true; ui->nav_focus=hsh; }
    if(over && ui->in.pressed){ ui->active=hsh; ui->claimed=true; }
    bool clk = over && ui->in.released && ui->active==hsh;
    if(over && ui->in.released && ui->active==hsh) ui->active=0;
    return clk;
}
bool ui_button(Ui* ui,const char* id,const char* label,float x,float y,float w,float h,int style){
    bool over=ui_mouse_in(ui,x,y,w,h);
    bool down=over&&ui->in.down&&ui->active==fnv(id);
    bool clk=clickable(ui,id,x,y,w,h);
    Color bg=ui_theme.panel2, fg=ui_theme.text, border=ui_theme.border;
    if(style==1){ bg=ui_theme.accent2; fg=ui_theme.white; border=col_mul(ui_theme.accent2,1.2f); }
    else if(style==2){ bg=ui_theme.bad; fg=ui_theme.white; border=col_mul(ui_theme.bad,1.2f); }
    else if(style==3){ bg=col(0,0,0,0); fg=over?ui_theme.white:ui_theme.dim; border=col(0,0,0,0); }
    if(over) bg=col_lerp(bg,ui_theme.accent,style==1?0.18f:0.12f);
    if(down) bg=col_mul(bg,0.78f);
    if(style==1&&over) bg=col_lerp(ui_theme.accent2,ui_theme.accent,0.25f);
    ui_rect(ui,x,y,w,h,bg);
    if(style!=3) ui_rect_outline(ui,x,y,w,h,border,1);
    float tw=ui_text_width(ui,label,13); ui_text(ui,x+(w-tw)*0.5f,y+(h-16)*0.5f,label,13,fg);
    return clk;
}
bool ui_icon_button(Ui* ui,const char* id,int icon,float x,float y,float w,float h,bool active){
    bool over=ui_mouse_in(ui,x,y,w,h); bool down=over&&ui->in.down&&ui->active==fnv(id);
    bool clk=clickable(ui,id,x,y,w,h);
    Color bg=active?col(0.23f,0.32f,0.44f,1):(over?col(0.20f,0.23f,0.27f,1):col(0.12f,0.14f,0.16f,1));
    if(down) bg=col_mul(bg,0.8f);
    ui_rect(ui,x,y,w,h,bg); ui_rect_outline(ui,x,y,w,h,active?ui_theme.accent2:ui_theme.border,1);
    ui_icon(ui,icon,x+(w-h)*0.5f+h*0.12f,y+h*0.12f,h*0.76f,active?ui_theme.white:(over?ui_theme.accent:ui_theme.dim));
    return clk;
}
bool ui_toggle(Ui* ui,const char* id,const char* label,float x,float y,float w,float h,bool* value){
    bool clk=clickable(ui,id,x,y,w,h);
    if(clk)*value=!*value;
    ui_rect(ui,x,y,w,h,ui_theme.panel2); ui_rect_outline(ui,x,y,w,h,ui_theme.border,1);
    float bx=x+7,by=y+h*0.5f-7;
    ui_rect(ui,bx,by,14,14,*value?ui_theme.accent2:col(0.09f,0.10f,0.12f,1));
    if(*value){ ui_line(ui,v2(bx+3,by+7),v2(bx+6,by+10),ui_theme.white,2); ui_line(ui,v2(bx+6,by+10),v2(bx+12,by+3),ui_theme.white,2); }
    ui_text(ui,x+28,y+(h-16)*0.5f,label,12,ui_theme.text); return clk;
}
bool ui_dragf(Ui* ui,const char* id,const char* label,float x,float y,float w,float h,float* value,float step,float lo,float hi,int decimals){
    unsigned int hsh=fnv(id); bool over=ui_mouse_in(ui,x,y,w,h);
    nav_register(ui,hsh,x,y,w,h,UI_NAV_SLIDER);
    if(over){ ui->mouse_over_ui=true; ui->hot=hsh; ui->nav_focus=hsh; }
    if(over&&(ui->in.plus||ui->in.minus)){
        float d=step*(ui->in.shift?10.0f:1.0f)*(ui->in.plus?1.0f:-1.0f);
        *value=clampf(*value+d,lo,hi);
        return true;
    }
    if(over&&ui->in.pressed){ ui->active=hsh; ui->drag_start_x=ui->in.mx; ui->drag_start_val=*value; ui->drag_moved=false; }
    if(ui->active==hsh&&ui->in.down){
        float dx=ui->in.mx-ui->drag_start_x;
        if(fabsf(dx)>2) ui->drag_moved=true;
        float sens=step*(ui->in.shift?0.1f:1.0f);
        *value=clampf(ui->drag_start_val+dx*sens,lo,hi);
    }
    if(ui->active==hsh&&ui->in.released) ui->active=0;
    Color bg=(over||ui->active==hsh)?col(0.16f,0.19f,0.23f,1):col(0.10f,0.115f,0.13f,1);
    ui_rect(ui,x,y,w,h,bg); ui_rect_outline(ui,x,y,w,h,ui->active==hsh?ui_theme.accent2:col(0.19f,0.21f,0.24f,1),1);
    if(label) ui_text(ui,x+6,y+(h-15)*0.5f,label,11,ui_theme.dim);
    char buf[64];
    if(decimals<=0) snprintf(buf,sizeof(buf),"%d",(int)lroundf(*value)); else if(decimals==1) snprintf(buf,sizeof(buf),"%.1f",*value); else snprintf(buf,sizeof(buf),"%.2f",*value);
    ui_text_right(ui,x+w-6,y+(h-15)*0.5f,buf,11,ui_theme.text);
    return ui->active==hsh&&ui->drag_moved;
}
bool ui_slider(Ui* ui,const char* id,const char* label,float x,float y,float w,float h,float* value,float lo,float hi){
    unsigned int hsh=fnv(id); bool over=ui_mouse_in(ui,x,y,w,h);
    nav_register(ui,hsh,x,y,w,h,UI_NAV_SLIDER);
    if(over){ ui->mouse_over_ui=true; ui->hot=hsh; ui->nav_focus=hsh; }
    if(over&&(ui->in.plus||ui->in.minus)){
        float d=(hi-lo)*0.02f*(ui->in.shift?5.0f:1.0f)*(ui->in.plus?1.0f:-1.0f);
        *value=clampf(*value+d,lo,hi);
        return true;
    }
    if(over&&ui->in.pressed){ ui->active=hsh; }
    if(ui->active==hsh&&ui->in.down&&w>1){ *value=lo+clampf((ui->in.mx-x)/w,0,1)*(hi-lo); }
    if(ui->active==hsh&&ui->in.released) ui->active=0;
    ui_rect(ui,x,y+h*0.45f,w,h*0.4f,col(0.08f,0.09f,0.11f,1));
    float t=(*value-lo)/fmaxf(hi-lo,1e-6f);
    ui_rect(ui,x,y+h*0.45f,w*t,h*0.4f,ui_theme.accent2);
    float kx=x+w*t; ui_rect(ui,kx-3,y+h*0.2f,6,h*0.8f,ui_theme.accent);
    if(label) ui_text(ui,x,y-15,label,11,ui_theme.dim);
    return ui->active==hsh;
}
bool ui_textbox(Ui* ui,const char* id,float x,float y,float w,float h,char* buf,int cap,const char* hint){
    unsigned int hsh=fnv(id); bool over=ui_mouse_in(ui,x,y,w,h);
    nav_register(ui,hsh,x,y,w,h,UI_NAV_TEXT);
    if(over){ ui->mouse_over_ui=true; ui->hot=hsh; ui->nav_focus=hsh; }
    if(over&&ui->in.pressed){
        if(ui->focus&&ui->focus!=hsh){ /* commit previous */ }
        ui->focus=hsh; ui->edit_len=(int)strlen(buf); if(ui->edit_len>255)ui->edit_len=255; memcpy(ui->edit_buf,buf,ui->edit_len); ui->edit_buf[ui->edit_len]=0; ui->edit_commit=false;
    }
    bool enter=false;
    if(ui->focus==hsh){
        if(ui->in.enter){ enter=true; str_copy(buf,ui->edit_buf,cap); ui->focus=0; }
        else if(ui->in.escape){ ui->focus=0; }
        else if(over&&ui->in.pressed && ui->in.mx<x) { }
        if(ui->focus==hsh && ui->in.released && !over && !ui->in.down){ /* click away commits */ str_copy(buf,ui->edit_buf,cap); ui->focus=0; }
    }
    Color border=(ui->focus==hsh)?ui_theme.accent2:ui_theme.border;
    ui_rect(ui,x,y,w,h,col(0.06f,0.07f,0.085f,1)); ui_rect_outline(ui,x,y,w,h,border,1);
    const char* txt=(ui->focus==hsh)?ui->edit_buf:buf;
    if(!txt[0]&&hint) ui_text(ui,x+7,y+(h-16)*0.5f,hint,13,ui_theme.dim);
    else ui_text(ui,x+7,y+(h-16)*0.5f,txt,13,ui_theme.text);
    if(ui->focus==hsh){ float cx=x+7+ui_text_width(ui,ui->edit_buf,13); ui_rect(ui,cx,y+4,1,h-8,ui_theme.accent); }
    return enter;
}
bool ui_color_swatch(Ui* ui,const char* id,float x,float y,float w,float h,Color c,bool selected){
    bool clk=clickable(ui,id,x,y,w,h);
    ui_rect(ui,x,y,w,h,c);
    ui_rect_outline(ui,x,y,w,h,selected?ui_theme.white:ui_theme.border,selected?2:1);
    return clk;
}
int ui_list(Ui* ui,const char* id,const char** items,int count,int selected,float x,float y,float w,float row_h){
    int result=selected;
    for(int i=0;i<count;i++){
        char wid[96]; snprintf(wid,sizeof(wid),"%s#%d",id,i);
        bool over=ui_mouse_in(ui,x,y+i*row_h,w,row_h);
        if(over){ ui->mouse_over_ui=true; }
        if(over&&ui->in.pressed) result=i;
        Color bg = (i==selected)?col(0.16f,0.26f,0.39f,1):(over?col(0.14f,0.16f,0.19f,1):col(0,0,0,0));
        ui_rect(ui,x,y+i*row_h,w,row_h,bg);
        if(i==selected) ui_rect(ui,x,y+i*row_h,3,row_h,ui_theme.accent2);
        ui_text(ui,x+10,y+i*row_h+(row_h-16)*0.5f,items[i],12,(i==selected)?ui_theme.white:ui_theme.text);
    }
    return result;
}
void ui_tooltip(Ui* ui,const char* text,float x,float y){
    float w=ui_text_width(ui,text,12)+14; float h=24;
    if(x+w>ui->w) x=ui->w-w-4;
    if(y+h>ui->h) y-=h+8;
    ui_rect(ui,x,y,w,h,col(0.02f,0.03f,0.04f,0.96f)); ui_rect_outline(ui,x,y,w,h,ui_theme.border,1);
    ui_text(ui,x+7,y+4,text,12,ui_theme.text);
}
