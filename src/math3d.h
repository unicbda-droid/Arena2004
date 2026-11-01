/* Arena2004 - small 3D math library (Y up, right handed, OpenGL clip space) */
#ifndef MATH3D_H
#define MATH3D_H
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846f
#define DEG2RAD (PI/180.0f)
#define RAD2DEG (180.0f/PI)

typedef struct { float x, y; } Vec2;
typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z, w; } Vec4;

static inline Vec2 v2(float x, float y){ Vec2 v={x,y}; return v; }
static inline Vec3 v3(float x, float y, float z){ Vec3 v={x,y,z}; return v; }
static inline Vec4 v4(float x, float y, float z, float w){ Vec4 v={x,y,z,w}; return v; }
static inline Vec3 v3s(float s){ return v3(s,s,s); }
static inline Vec2 v2add(Vec2 a, Vec2 b){ return v2(a.x+b.x,a.y+b.y); }
static inline Vec2 v2sub(Vec2 a, Vec2 b){ return v2(a.x-b.x,a.y-b.y); }
static inline Vec2 v2mul(Vec2 a, float s){ return v2(a.x*s,a.y*s); }
static inline float v2dot(Vec2 a, Vec2 b){ return a.x*b.x+a.y*b.y; }
static inline float v2len(Vec2 a){ return sqrtf(v2dot(a,a)); }
static inline Vec2 v2norm(Vec2 a){ float l=v2len(a); return l>1e-8f?v2mul(a,1.0f/l):v2(0,0); }

static inline Vec3 v3add(Vec3 a, Vec3 b){ return v3(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline Vec3 v3sub(Vec3 a, Vec3 b){ return v3(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline Vec3 v3mul(Vec3 a, float s){ return v3(a.x*s,a.y*s,a.z*s); }
static inline Vec3 v3mulv(Vec3 a, Vec3 b){ return v3(a.x*b.x,a.y*b.y,a.z*b.z); }
static inline Vec3 v3div(Vec3 a, float s){ return v3(a.x/s,a.y/s,a.z/s); }
static inline Vec3 v3neg(Vec3 a){ return v3(-a.x,-a.y,-a.z); }
static inline float v3dot(Vec3 a, Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline Vec3 v3cross(Vec3 a, Vec3 b){ return v3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
static inline float v3len(Vec3 a){ return sqrtf(v3dot(a,a)); }
static inline float v3len2(Vec3 a){ return v3dot(a,a); }
static inline Vec3 v3norm(Vec3 a){ float l=v3len(a); return l>1e-8f?v3mul(a,1.0f/l):v3(0,0,0); }
static inline Vec3 v3lerp(Vec3 a, Vec3 b, float t){ return v3(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t); }
static inline Vec3 v3min(Vec3 a, Vec3 b){ return v3(fminf(a.x,b.x),fminf(a.y,b.y),fminf(a.z,b.z)); }
static inline Vec3 v3max(Vec3 a, Vec3 b){ return v3(fmaxf(a.x,b.x),fmaxf(a.y,b.y),fmaxf(a.z,b.z)); }
static inline Vec3 v3abs(Vec3 a){ return v3(fabsf(a.x),fabsf(a.y),fabsf(a.z)); }
static inline Vec3 v3clamp(Vec3 a, float lo, float hi){ return v3(fminf(fmaxf(a.x,lo),hi),fminf(fmaxf(a.y,lo),hi),fminf(fmaxf(a.z,lo),hi)); }
static inline Vec3 v3reflect(Vec3 d, Vec3 n){ return v3sub(d, v3mul(n, 2.0f*v3dot(d,n))); }
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float lerpf(float a, float b, float t){ return a+(b-a)*t; }
static inline float saturate(float v){ return clampf(v,0,1); }
static inline float smoothstepf(float a, float b, float x){ float t=saturate((x-a)/(b-a)); return t*t*(3-2*t); }
static inline float signf(float v){ return v<0?-1.0f:1.0f; }
static inline float angle_wrap(float a){ while(a>PI)a-=2*PI; while(a<-PI)a+=2*PI; return a; }
static inline float angle_lerp(float a, float b, float t){ return a + angle_wrap(b-a)*t; }
static inline float rndf(void){ return (float)rand()/(float)RAND_MAX; }
static inline float rndr(float a, float b){ return a + (b-a)*rndf(); }
static inline int rndi(int a, int b){ return a + rand()%(b-a+1); }

/* deterministic per-index hash noise */
static inline float hash11(float p){ p=fmodf(p*0.1031f,1.0f); p*=p+33.33f; p*=p+p; return p-fmodf(p,1.0f); }
static inline uint32_t hash_u32(uint32_t x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
static inline float hash01(uint32_t x){ return (float)(hash_u32(x)&0xffffff)/16777216.0f; }

/* ---- Mat4, column major, m[col*4+row] ---- */
typedef struct { float m[16]; } Mat4;

static inline Mat4 mat4_identity(void){ Mat4 r; memset(&r,0,sizeof(r)); r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
static inline Mat4 mat4_mul(Mat4 a, Mat4 b){
    Mat4 r; for(int c=0;c<4;c++) for(int rr=0;rr<4;rr++){
        r.m[c*4+rr]=a.m[0*4+rr]*b.m[c*4+0]+a.m[1*4+rr]*b.m[c*4+1]+a.m[2*4+rr]*b.m[c*4+2]+a.m[3*4+rr]*b.m[c*4+3];
    } return r;
}
static inline Mat4 mat4_translate(Vec3 t){ Mat4 r=mat4_identity(); r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; return r; }
static inline Mat4 mat4_scale(Vec3 s){ Mat4 r=mat4_identity(); r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; return r; }
static inline Mat4 mat4_rotate_x(float a){ float c=cosf(a),s=sinf(a); Mat4 r=mat4_identity(); r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c; return r; }
static inline Mat4 mat4_rotate_y(float a){ float c=cosf(a),s=sinf(a); Mat4 r=mat4_identity(); r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c; return r; }
static inline Mat4 mat4_rotate_z(float a){ float c=cosf(a),s=sinf(a); Mat4 r=mat4_identity(); r.m[0]=c; r.m[1]=s; r.m[4]=-s; r.m[5]=c; return r; }
static inline Mat4 mat4_from_trs(Vec3 pos, Vec3 rot_deg, Vec3 scale){
    Mat4 r=mat4_translate(pos);
    r=mat4_mul(r, mat4_rotate_y(rot_deg.y*DEG2RAD));
    r=mat4_mul(r, mat4_rotate_x(rot_deg.x*DEG2RAD));
    r=mat4_mul(r, mat4_rotate_z(rot_deg.z*DEG2RAD));
    r=mat4_mul(r, mat4_scale(scale));
    return r;
}
static inline Mat4 mat4_perspective(float fovy, float aspect, float znear, float zfar){
    float f=1.0f/tanf(fovy*0.5f); Mat4 r; memset(&r,0,sizeof(r));
    r.m[0]=f/aspect; r.m[5]=f; r.m[10]=(zfar+znear)/(znear-zfar); r.m[11]=-1.0f; r.m[14]=(2*zfar*znear)/(znear-zfar);
    return r;
}
static inline Mat4 mat4_ortho(float l,float r,float b,float t,float n,float f){
    Mat4 m; memset(&m,0,sizeof(m));
    m.m[0]=2/(r-l); m.m[5]=2/(t-b); m.m[10]=-2/(f-n);
    m.m[12]=-(r+l)/(r-l); m.m[13]=-(t+b)/(t-b); m.m[14]=-(f+n)/(f-n); m.m[15]=1; return m;
}
static inline Vec3 mat4_mul_point(Mat4 m, Vec3 p){ float x=p.x,y=p.y,z=p.z; float w=m.m[3]*x+m.m[7]*y+m.m[11]*z+m.m[15]; if(fabsf(w)<1e-9f)w=1; return v3((m.m[0]*x+m.m[4]*y+m.m[8]*z+m.m[12])/w,(m.m[1]*x+m.m[5]*y+m.m[9]*z+m.m[13])/w,(m.m[2]*x+m.m[6]*y+m.m[10]*z+m.m[14])/w); }
static inline Vec3 mat4_mul_dir(Mat4 m, Vec3 p){ return v3(m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z, m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z, m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z); }
static inline Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up){
    Vec3 f=v3norm(v3sub(target,eye)); Vec3 s=v3norm(v3cross(f,up)); Vec3 u=v3cross(s,f);
    Mat4 r=mat4_identity();
    r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
    r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
    r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
    r.m[12]=-v3dot(s,eye); r.m[13]=-v3dot(u,eye); r.m[14]=v3dot(f,eye);
    return r;
}
static inline Mat4 mat4_invert(Mat4 mat){
    const float* m=mat.m; Mat4 out; float* inv=out.m;
    inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    float det=m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
    if(fabsf(det)<1e-9f) return mat4_identity();
    det=1.0f/det; for(int i=0;i<16;i++) inv[i]*=det; return out;
}

/* ---- colors ---- */
typedef struct { float r,g,b,a; } Color;
static inline Color col(float r,float g,float b,float a){ Color c={r,g,b,a}; return c; }
static inline Color col3(float r,float g,float b){ return col(r,g,b,1); }
static inline Color col_hex(uint32_t hex){ return col(((hex>>16)&255)/255.0f,((hex>>8)&255)/255.0f,(hex&255)/255.0f,1); }
static inline Color col_lerp(Color a, Color b, float t){ return col(lerpf(a.r,b.r,t),lerpf(a.g,b.g,t),lerpf(a.b,b.b,t),lerpf(a.a,b.a,t)); }
static inline Color col_mul(Color a, float s){ return col(a.r*s,a.g*s,a.b*s,a.a); }

/* ---- rays / bounds ---- */
typedef struct { Vec3 origin, dir; } Ray;
typedef struct { Vec3 min, max; } AABB;
static inline AABB aabb_from_center(Vec3 c, Vec3 half){ AABB b={v3(c.x-half.x,c.y-half.y,c.z-half.z),v3(c.x+half.x,c.y+half.y,c.z+half.z)}; return b; }
static inline AABB aabb_center_size(Vec3 c, Vec3 size){ AABB b={v3(c.x-size.x*0.5f,c.y-size.y*0.5f,c.z-size.z*0.5f),v3(c.x+size.x*0.5f,c.y+size.y*0.5f,c.z+size.z*0.5f)}; return b; }
static inline AABB aabb_union(AABB a, AABB b){ AABB r={v3(fminf(a.min.x,b.min.x),fminf(a.min.y,b.min.y),fminf(a.min.z,b.min.z)),v3(fmaxf(a.max.x,b.max.x),fmaxf(a.max.y,b.max.y),fmaxf(a.max.z,b.max.z))}; return r; }
static inline bool aabb_overlap(AABB a, AABB b){ return a.min.x<=b.max.x&&a.max.x>=b.min.x&&a.min.y<=b.max.y&&a.max.y>=b.min.y&&a.min.z<=b.max.z&&a.max.z>=b.min.z; }
static inline bool aabb_contains(AABB a, Vec3 p){ return p.x>=a.min.x&&p.x<=a.max.x&&p.y>=a.min.y&&p.y<=a.max.y&&p.z>=a.min.z&&p.z<=a.max.z; }
static inline Vec3 aabb_center(AABB a){ return v3((a.min.x+a.max.x)*0.5f,(a.min.y+a.max.y)*0.5f,(a.min.z+a.max.z)*0.5f); }
static inline Vec3 aabb_size(AABB a){ return v3(a.max.x-a.min.x,a.max.y-a.min.y,a.max.z-a.min.z); }

static inline bool ray_aabb(Ray r, AABB b, float* tmin_out, Vec3* n_out){
    float tmin=0.0f, tmax=1e30f; Vec3 n=v3(0,0,0);
    for(int i=0;i<3;i++){
        float o=((float*)&r.origin)[i], d=((float*)&r.dir)[i];
        float lo=((float*)&b.min)[i], hi=((float*)&b.max)[i];
        if(fabsf(d)<1e-8f){ if(o<lo||o>hi) return false; }
        else {
            float t1=(lo-o)/d, t2=(hi-o)/d; float sign=-1.0f;
            if(t1>t2){ float tt=t1;t1=t2;t2=tt; sign=1.0f; }
            if(t1>tmin){ tmin=t1; n=v3(0,0,0); ((float*)&n)[i]=sign; }
            if(t2<tmax) tmax=t2;
            if(tmin>tmax) return false;
        }
    }
    if(tmin_out) *tmin_out=tmin;
    if(n_out) *n_out=n;
    return true;
}
/* returns t of first hit within maxdist, -1 if none */
static inline float ray_aabb_t(Ray r, AABB b, float maxdist){ float t; if(ray_aabb(r,b,&t,NULL)&&t<=maxdist) return t; return -1; }

static inline float ray_sphere(Vec3 ro, Vec3 rd, Vec3 center, float radius){
    Vec3 oc=v3sub(ro,center); float b=v3dot(oc,rd); float c=v3dot(oc,oc)-radius*radius; float h=b*b-c;
    if(h<0) return -1;
    h=sqrtf(h);
    float t=-b-h;
    if(t<0) t=-b+h;
    return t;
}
static inline bool point_in_cone(Vec3 dir, Vec3 to, float coshalf){
    float l=v3len(to); if(l<1e-5f) return true; return v3dot(dir,v3mul(to,1.0f/l))>coshalf;
}
static inline Vec3 closest_point_on_segment(Vec3 a, Vec3 b, Vec3 p){
    Vec3 ab=v3sub(b,a); float t=clampf(v3dot(v3sub(p,a),ab)/fmaxf(v3dot(ab,ab),1e-8f),0,1); return v3add(a,v3mul(ab,t));
}
static inline float dist_point_line(Vec3 p, Vec3 a, Vec3 b){ return v3len(v3sub(p,closest_point_on_segment(a,b,p))); }

/* ray from screen pixel, camera */
static inline Ray camera_ray(Vec3 pos, Vec3 fwd, Vec3 right, Vec3 up, float fovy, float aspect, float sx, float sy, float w, float h){
    float ndcx=(2.0f*sx/w-1.0f);
    float ndcy=(1.0f-2.0f*sy/h);
    float th=tanf(fovy*0.5f);
    Vec3 d=v3add(fwd, v3add(v3mul(right, ndcx*aspect*th), v3mul(up, ndcy*th)));
    Ray r={pos, v3norm(d)}; return r;
}

/* string helpers */
static inline void str_copy(char* dst, const char* src, int cap){ if(cap<=0)return; int i=0; for(;src[i]&&i<cap-1;i++)dst[i]=src[i]; dst[i]=0; }
static inline char* str_skip(char* s){ while(*s==' '||*s=='\t')s++; return s; }

#endif
