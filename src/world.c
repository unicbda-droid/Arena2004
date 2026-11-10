#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "world.h"
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void set_name(Actor* a,const char* n){ str_copy(a->name,n?n:"",MAX_NAME); }

void world_init(World* w){
    memset(w,0,sizeof(*w));
    str_copy(w->name,"DM-Deck17",sizeof(w->name));
    w->version=1;
    w->fog_color=col3(0.70f,0.80f,0.90f); w->fog_density=0.006f;
    w->ambient_sky=col3(0.30f,0.38f,0.50f); w->ambient_ground=col3(0.20f,0.20f,0.18f);
    w->sun_dir=v3norm(v3(0.25f,1.0f,0.18f)); w->sun_color=col3(1.0f,0.95f,0.85f); w->sun_intensity=1.2f;
}
void world_clear(World* w){ int v=w->version; char nm[64]; str_copy(nm,w->name,64); Color fc=w->fog_color; float fd=w->fog_density; Color as=w->ambient_sky,ag=w->ambient_ground; Vec3 sd=w->sun_dir; Color sc=w->sun_color; float si=w->sun_intensity; world_init(w); w->version=v; str_copy(w->name,nm,64); w->fog_color=fc;w->fog_density=fd;w->ambient_sky=as;w->ambient_ground=ag;w->sun_dir=sd;w->sun_color=sc;w->sun_intensity=si; w->count=0; }
int world_add(World* w,Actor a){
    if(w->count>=MAX_ACTORS) return -1;
    if(!a.name[0]) snprintf(a.name,MAX_NAME,"%s_%d",actor_type_name(a.type),w->count);
    int i=w->count++; w->actors[i]=a; return i;
}
void world_delete(World* w,int index){ if(index<0||index>=w->count)return; memmove(&w->actors[index],&w->actors[index+1],sizeof(Actor)*(w->count-index-1)); w->count--; }
AABB world_actor_aabb(const World* w,const Actor* a){
    (void)w; Vec3 h=v3mul(a->size,0.5f); AABB b={v3(a->pos.x-h.x,a->pos.y-h.y,a->pos.z-h.z),v3(a->pos.x+h.x,a->pos.y+h.y,a->pos.z+h.z)}; return b;
}
bool world_actor_solid(const Actor* a){ return (a->type==ACT_BRUSH||a->type==ACT_DECOR||a->type==ACT_STATICMESH) && (a->flags&FLAG_SOLID); }
bool world_ramp_params(const Actor* a,int* axis,float* dir,float* y0,float* y1,float* lo,float* hi,float* cross_lo,float* cross_hi){
    if(a->type!=ACT_BRUSH||a->shape!=SHAPE_RAMP) return false;
    float yaw=a->yaw*DEG2RAD; float c=cosf(yaw),s=sinf(yaw);
    /* local z axis in world = (sin yaw, cos yaw) */
    if(axis)*axis=(fabsf(c)>fabsf(s))?2:0;
    if(dir)*dir=(fabsf(c)>fabsf(s))?signf(c):signf(s);
    if(y0)*y0=a->pos.y-a->size.y*0.5f;
    if(y1)*y1=a->pos.y+a->size.y*0.5f;
    if(axis && *axis==2){ if(lo)*lo=a->pos.z-a->size.z*0.5f; if(hi)*hi=a->pos.z+a->size.z*0.5f; if(cross_lo)*cross_lo=a->pos.x-a->size.x*0.5f; if(cross_hi)*cross_hi=a->pos.x+a->size.x*0.5f; }
    else { if(lo)*lo=a->pos.x-a->size.z*0.5f; if(hi)*hi=a->pos.x+a->size.z*0.5f; if(cross_lo)*cross_lo=a->pos.z-a->size.x*0.5f; if(cross_hi)*cross_hi=a->pos.z+a->size.x*0.5f; }
    return true;
}
static bool ramp_local(const Actor* a,float x,float z,float* lx,float* lz){
    if(a->type!=ACT_BRUSH||a->shape!=SHAPE_RAMP) return false;
    float yaw=a->yaw*DEG2RAD; float c=cosf(yaw),s=sinf(yaw);
    float dx=x-a->pos.x, dz=z-a->pos.z;
    /* local x = world (cos, -sin), local z = world (sin, cos) */
    *lx = dx*c - dz*s;
    *lz = dx*s + dz*c;
    return fabsf(*lx)<=a->size.x*0.5f+1e-4f && fabsf(*lz)<=a->size.z*0.5f+1e-4f;
}
float world_ramp_height(const Actor* a,float x,float z){
    float lx,lz; if(!ramp_local(a,x,z,&lx,&lz)) return -1e9f;
    float t=(lz+a->size.z*0.5f)/fmaxf(a->size.z,1e-5f);
    return a->pos.y-a->size.y*0.5f + t*a->size.y;
}
bool world_raycast(const World* w,Ray r,float maxdist,bool solids_only,RayHit* out){
    RayHit best; memset(&best,0,sizeof(best)); best.t=maxdist; best.hit=false;
    for(int i=0;i<w->count;i++){
        const Actor* a=&w->actors[i];
        if(!world_actor_solid(a)) continue;
        if(solids_only && a->type==ACT_DECOR && !(a->flags&FLAG_SOLID)) continue;
        AABB b=world_actor_aabb(w,a);
        /* expand a tiny bit for stability */
        b.min=v3sub(b.min,v3s(0.001f)); b.max=v3add(b.max,v3s(0.001f));
        float t; Vec3 n;
        if(ray_aabb(r,b,&t,&n) && t>=0 && t<best.t){ best.hit=true; best.t=t; best.normal=n; best.actor=i; best.point=v3add(r.origin,v3mul(r.dir,t)); }
    }
    if(out) *out=best;
    return best.hit;
}
bool world_overlap_box(const World* w,AABB box,int ignore){
    for(int i=0;i<w->count;i++){
        if(i==ignore) continue;
        const Actor* a=&w->actors[i];
        if(!world_actor_solid(a)) continue;
        if(a->shape==SHAPE_RAMP) continue;
        if(aabb_overlap(box,world_actor_aabb(w,a))) return true;
    }
    return false;
}
static AABB player_box(Vec3 p,float r,float h){ AABB b={v3(p.x-r,p.y,p.z-r),v3(p.x+r,p.y+h,p.z+r)}; return b; }
/* Overlap test with an epsilon: merely touching a surface is not a collision.
 * The old <=/>= based test made standing on a deck look like an overlap on
 * every frame and could snag or teleport the player at edges. */
static bool overlap_eps(AABB a,AABB b,float e){
    return a.min.x < b.max.x-e && a.max.x > b.min.x+e &&
           a.min.y < b.max.y-e && a.max.y > b.min.y+e &&
           a.min.z < b.max.z-e && a.max.z > b.min.z+e;
}
static bool box_free_for_step(const World* w,Vec3 p,float r,float h,int ignore){
    return !world_overlap_box(w,player_box(p,r,h),ignore);
}
bool world_move_box(World* w,Vec3* pos,Vec3* vel,float dt,float radius,float height,bool* grounded,bool* hit_wall,int* ground_actor){
    bool was_grounded=*grounded; *grounded=false; *hit_wall=false; if(ground_actor)*ground_actor=-1;
    Vec3 p=*pos; Vec3 v=*vel;
    const float STEP=0.002f;

    /* ---- vertical first -------------------------------------------------
     * Resolving vertical penetration before the horizontal axes means a
     * player who lands slightly inside a floor is pushed up instead of being
     * shoved sideways out of the platform. */
    p.y += v.y*dt;
    for(int i=0;i<w->count;i++){
        const Actor* a=&w->actors[i]; if(!world_actor_solid(a)||a->shape==SHAPE_RAMP) continue;
        AABB b=world_actor_aabb(w,a);
        AABB pb=player_box(p,radius,height);
        if(!overlap_eps(pb,b,0.0005f)) continue;
        float up=(b.max.y+0.0005f)-p.y;
        float down=(p.y+height+0.0005f)-b.min.y;
        if(up<=down){
            p.y+=up; if(v.y<0) v.y=0; *grounded=true; if(ground_actor)*ground_actor=i;
        } else {
            p.y-=down; if(v.y>0) v.y=0;
        }
    }
    /* ramps: stand on the slope */
    for(int i=0;i<w->count;i++){
        const Actor* a=&w->actors[i]; if(a->type!=ACT_BRUSH||a->shape!=SHAPE_RAMP||!(a->flags&FLAG_SOLID)) continue;
        float surf=world_ramp_height(a,p.x,p.z); if(surf<-1e8f) continue;
        if(v.y<=0 && p.y<=surf && p.y>surf-1.2f){ p.y=surf; v.y=0; *grounded=true; if(ground_actor)*ground_actor=i; }
    }

    /* ---- horizontal axes ------------------------------------------------
     * The box is swept by moving one axis at a time. Each axis resolves the
     * smallest penetration and re-scans, so overlapping map brushes cannot
     * push the player into another solid. */
    for(int axis=0;axis<2;axis++){
        float* coord = axis==0 ? &p.x : &p.z;
        float* vaxis = axis==0 ? &v.x : &v.z;
        *coord += *vaxis*dt;
        for(int iter=0;iter<6;iter++){
            const Actor* hit=NULL; AABB hb; int hi=-1;
            for(int i=0;i<w->count;i++){
                const Actor* a=&w->actors[i]; if(!world_actor_solid(a)||a->shape==SHAPE_RAMP) continue;
                AABB b=world_actor_aabb(w,a);
                AABB pb=player_box(p,radius,height);
                if(overlap_eps(pb,b,0.001f)){ hit=a; hb=b; hi=i; break; }
            }
            if(!hit) break;
            /* step-up: low ledges should be walkable, not walls */
            float rise=hb.max.y-p.y;
            if((*grounded||was_grounded) && rise>0.0f && rise<=0.45f){
                Vec3 lifted=v3(p.x,hb.max.y+0.001f,p.z);
                if(box_free_for_step(w,lifted,radius,height,hi)){
                    p.y=hb.max.y+0.001f; *grounded=true; continue;
                }
            }
            if(axis==0){
                float push_px=(hb.max.x+radius+STEP)-p.x;
                float push_nx=p.x-(hb.min.x-radius-STEP);
                bool plus = (*vaxis>0.0001f) ? true : ((*vaxis<-0.0001f) ? false : push_px<push_nx);
                p.x += plus ? push_px : -push_nx;
            } else {
                float push_pz=(hb.max.z+radius+STEP)-p.z;
                float push_nz=p.z-(hb.min.z-radius-STEP);
                bool plus = (*vaxis>0.0001f) ? true : ((*vaxis<-0.0001f) ? false : push_pz<push_nz);
                p.z += plus ? push_pz : -push_nz;
            }
            *vaxis=0; *hit_wall=true;
        }
    }
    /* keep the player glued to ramps after horizontal movement */
    for(int i=0;i<w->count;i++){
        const Actor* a=&w->actors[i]; if(a->type!=ACT_BRUSH||a->shape!=SHAPE_RAMP||!(a->flags&FLAG_SOLID)) continue;
        float surf=world_ramp_height(a,p.x,p.z); if(surf<-1e8f) continue;
        if(was_grounded && fabsf(p.y-surf)<0.65f){ p.y=surf; v.y=0; *grounded=true; if(ground_actor)*ground_actor=i; }
    }
    /* Sub-stepping calls this function several times per frame. The first call
     * consumes the gravity velocity and reports ground; the following calls
     * have v.y==0 and would otherwise report airborne, which disabled step-up
     * and made stairs feel sticky. Probe just below the feet to keep support. */
    if(!*grounded && was_grounded && v.y<=0.0f){
        Ray r; r.origin=v3(p.x,p.y+0.04f,p.z); r.dir=v3(0,-1,0); RayHit hit;
        if(world_raycast(w,r,0.14f,true,&hit) && hit.normal.y>0.5f) *grounded=true;
    }
    *pos=p; *vel=v;
    return *grounded;
}

const char* actor_type_name(int t){
    switch(t){case ACT_BRUSH:return "Brush";case ACT_LIGHT:return "Light";case ACT_PLAYER_START:return "PlayerStart";case ACT_PICKUP:return "Pickup";case ACT_JUMPPAD:return "JumpPad";case ACT_LAVA:return "LavaVolume";case ACT_DECOR:return "Decor";case ACT_STATICMESH:return "StaticMesh";case ACT_TRIGGER:return "Trigger";case ACT_NAV_POINT:return "NavPoint";default:return "Actor";}
}
const char* pickup_name(int t){
    static const char* n[]={"Health Vial","Health Pack","Shield Small","Shield Pack","Ammo","Assault Rifle","Shock Rifle","Rocket Launcher","Flak Cannon","Lightning Gun"};
    return (t>=0&&t<PICKUP_TYPE_COUNT)?n[t]:"Pickup";
}
const char* material_name(int m){
    static const char* n[]={"Metal Panels","Deck Plates","Hazard Yellow","Grate","Concrete","Tech Circuit","Blue Panels","Rust Metal","White Panels","Hex Grid","Ribbed Trim","Red Armor","Yellow Tech","Blue Metal","Hazard Blue","Black Gloss","Grass"};
    return (m>=0&&m<17)?n[m]:"Metal Panels";
}
bool actor_is_weapon_pickup(int subtype,int* weapon_index){
    if(subtype>=PICK_WEAPON_AR&&subtype<=PICK_WEAPON_LIGHTNING){ if(weapon_index)*weapon_index=subtype-PICK_WEAPON_AR; return true; }
    return false;
}
void world_actor_defaults(Actor* a,int type,int subtype){
    memset(a,0,sizeof(*a)); a->type=type; a->subtype=subtype; a->pos=v3(0,0,0); a->size=v3(1,1,1); a->yaw=0; a->color=col3(0.4f,0.42f,0.46f); a->material=0;
    a->flags=FLAG_SOLID; a->power=1; a->extra=1; a->respawn=20; a->shape=SHAPE_BOX;
    switch(type){
        case ACT_BRUSH: a->size=v3(4,2,4); a->pos.y=1; a->material=0; break;
        case ACT_LIGHT: a->size=v3(0.4f,0.4f,0.4f); a->color=col3(0.4f,0.7f,1.0f); a->power=12; a->extra=1.5f; a->flags=0; break;
        case ACT_PLAYER_START: a->size=v3(0.9f,0.1f,0.9f); a->pos.y=0.05f; a->flags=0; break;
        case ACT_PICKUP: a->size=v3(0.7f,0.7f,0.7f); a->pos.y=0.6f; a->flags=0; a->respawn=20;
            a->color=col3(0.4f,0.8f,1.0f);
            if(subtype==PICK_HEALTH_SMALL||subtype==PICK_HEALTH_BIG) a->color=col3(0.3f,1.0f,0.5f);
            else if(subtype==PICK_ARMOR_SMALL||subtype==PICK_ARMOR_BIG) a->color=col3(0.3f,0.6f,1.0f);
            else if(subtype==PICK_AMMO) a->color=col3(1.0f,0.75f,0.25f);
            else { int wi=0; if(actor_is_weapon_pickup(subtype,&wi)){ Color wc[]={col3(0.8f,0.8f,0.85f),col3(0.25f,0.6f,1.0f),col3(1.0f,0.35f,0.15f),col3(1.0f,0.65f,0.2f),col3(0.7f,0.4f,1.0f)}; a->color=wc[wi]; } }
            break;
        case ACT_JUMPPAD: a->size=v3(2.2f,0.18f,2.2f); a->pos.y=0.09f; a->flags=0; a->power=10; a->extra=16; a->color=col3(0.2f,0.75f,1.0f); break;
        case ACT_LAVA: a->size=v3(24,4,24); a->pos.y=-2; a->flags=0; a->color=col3(1.0f,0.3f,0.03f); break;
        case ACT_DECOR: a->size=v3(1.2f,1.2f,1.2f); a->pos.y=0.6f; a->material=7; a->shape=SHAPE_CYLINDER; a->flags=FLAG_SOLID; break;
        case ACT_STATICMESH: a->size=v3(2.0f,1.6f,2.0f); a->pos.y=0.8f; a->material=0; a->shape=SHAPE_BOX; a->flags=FLAG_SOLID; break;
        case ACT_TRIGGER: a->size=v3(5.0f,4.0f,5.0f); a->pos.y=2.0f; a->shape=SHAPE_BOX; a->flags=0; a->color=col3(0.25f,0.95f,0.45f); a->power=1; a->extra=0; a->respawn=2.0f; str_copy(a->name,"say TRIGGER",MAX_NAME); break;
        case ACT_NAV_POINT: a->size=v3(0.6f,0.6f,0.6f); a->pos.y=0.55f; a->shape=SHAPE_CYLINDER; a->flags=0; a->color=col3(0.35f,0.9f,1.0f); str_copy(a->name,"NavPoint",MAX_NAME); break;
    }
}

/* ------------- default map ------------- */
static int B(World* w,float x,float y,float z,float sx,float sy,float sz,int mat,Color c,int emissive,int shape,float yaw,const char* name){
    Actor a; world_actor_defaults(&a,ACT_BRUSH,0); a.pos=v3(x,y,z); a.size=v3(sx,sy,sz); a.material=mat; a.color=c; a.yaw=yaw; a.shape=shape;
    if(emissive) a.flags|=FLAG_EMISSIVE;
    a.name[0]=0;
    int i=world_add(w,a);
    if(name) set_name(&w->actors[i],name);
    return i;
}
static int L(World* w,float x,float y,float z,Color c,float radius,float intensity){
    Actor a; world_actor_defaults(&a,ACT_LIGHT,0); a.pos=v3(x,y,z); a.color=c; a.power=radius; a.extra=intensity; return world_add(w,a);
}
static int P(World* w,float x,float z,float yaw,int type){
    Actor a; world_actor_defaults(&a,ACT_PLAYER_START,0); a.pos=v3(x,0.08f,z); a.yaw=yaw; a.name[0]=0; (void)type; return world_add(w,a);
}
static int PK(World* w,float x,float y,float z,int subtype,float respawn){
    Actor a; world_actor_defaults(&a,ACT_PICKUP,subtype); a.pos=v3(x,y,z); a.respawn=respawn; a.name[0]=0; return world_add(w,a);
}
static int JP(World* w,float x,float z,float yaw,float power,float up){
    Actor a; world_actor_defaults(&a,ACT_JUMPPAD,0); a.pos=v3(x,0.10f,z); a.yaw=yaw; a.power=power; a.extra=up; a.name[0]=0; return world_add(w,a);
}

void world_default_deck17(World* w){
    world_init(w); str_copy(w->name,"DM-Deck17",sizeof(w->name));
    Color dark=col3(0.24f,0.27f,0.32f), steel=col3(0.30f,0.33f,0.39f), blue=col3(0.16f,0.24f,0.38f), rust=col3(0.42f,0.30f,0.22f);
    Color orange=col3(1.0f,0.45f,0.08f), cyan=col3(0.15f,0.7f,1.0f);
    Color grass=col3(0.45f,0.65f,0.35f), dirt=col3(0.35f,0.25f,0.18f), stone=col3(0.55f,0.55f,0.58f);

    /* ---- terrain: flat grassy ground ---- */
    B(w,-42,-0.8f,-42, 84,2.0f,84, 16,grass,0,0,0,"GrassFloor");
    /* central playable arena */
    B(w,0,-0.6f,0, 34,0.6f,34, 1,dark,0,0,0,"ArenaDeck");
    B(w,0,-0.5f,0, 32,0.2f,32, 4,dirt,0,0,0,"ArenaEdge");

    /* ---- a few decorative trees and rocks (non-solid, bots can walk through) ---- */
    for(int t=0;t<12;t++){
        float tx=rndf()*60-30, tz=rndf()*60-30;
        /* keep trees off the arena deck */
        if(fabsf(tx)<17 && fabsf(tz)<17) continue;
        float th=4+rndf()*3;
        Actor tr; world_actor_defaults(&tr,ACT_BRUSH,0); tr.pos=v3(tx,0.5f,tz); tr.size=v3(0.7f,th,0.7f); tr.material=7; tr.color=col3(0.45f,0.30f,0.20f); tr.shape=SHAPE_CYLINDER; tr.flags=0; world_add(w,tr);
        Actor cr; world_actor_defaults(&cr,ACT_BRUSH,0); cr.pos=v3(tx,th,tz); cr.size=v3(2.4f,2.4f,2.4f); cr.material=16; cr.color=grass; cr.shape=SHAPE_SPHERE; cr.flags=0; world_add(w,cr);
    }
    for(int r=0;r<8;r++){
        float rx=rndf()*50-25, rz=rndf()*50-25;
        if(fabsf(rx)<16 && fabsf(rz)<16) continue;
        float rs=1.2f+rndf()*1.6f;
        Actor bo; world_actor_defaults(&bo,ACT_BRUSH,0); bo.pos=v3(rx,0.4f,rz); bo.size=v3(rs,rs,rs); bo.material=13; bo.color=stone; bo.shape=SHAPE_SPHERE; bo.flags=0; world_add(w,bo);
    }

    /* ---- pit with lava - walls go all the way down so no one slips through ---- */
    B(w,0,-6.6f,0, 20,0.4f,20, 15,dark,0,0,0,"PitFloor");
    B(w,0,-6.1f,0, 18,0.2f,18, 2,orange,1,0,0,"LavaSurface");
    for(int s=-1;s<=1;s+=2){
        B(w,s*7.5f,-3.2f,0, 0.5f,6.0f,20, 7,rust,0,0,0,"PitWallW");
        B(w,0,-3.2f,s*7.5f, 20,6.0f,0.5f, 7,rust,0,0,0,"PitWallS");
    }

    /* ---- central platform ---- */
    B(w,0,2.6f,0, 14,0.4f,14, 1,dark,0,0,0,"CenterPlatform");
    B(w,0,2.32f,0, 13.2f,0.16f,13.2f, 5,cyan,1,0,0,"CenterGlow");
    for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2){
        B(w,sx*4.5f,1.12f,sz*4.5f, 0.7f,2.4f,0.7f, 10,steel,0,0,0,"PlatformLeg");
        B(w,sx*4.5f,2.3f,sz*4.5f, 1.0f,0.2f,1.0f, 15,dark,0,0,0,"PlatformLegCap");
    }

    /* ---- catwalks above ---- */
    B(w,0,6.5f, 34, 46,0.5f,6, 1,dark,0,0,0,"CatwalkNorth");
    B(w,0,6.5f,-34, 46,0.5f,6, 1,dark,0,0,0,"CatwalkSouth");
    B(w,0,6.8f, 36.8f, 46,1.0f,0.25f, 10,blue,0,0,0,"RailNorth");
    B(w,0,6.8f,-36.8f, 46,1.0f,0.25f, 10,blue,0,0,0,"RailSouth");
    for(int sx=-1;sx<=1;sx+=2){
        B(w,sx*23.8f,6.8f, 34, 0.25f,1.0f,6, 10,blue,0,0,0,"RailEndN");
        B(w,sx*23.8f,6.8f,-34, 0.25f,1.0f,6, 10,blue,0,0,0,"RailEndS");
        B(w,sx*23.8f,3.4f, 34, 0.4f,6.0f,6, 7,rust,0,0,0,"CatwalkSupportN");
        B(w,sx*23.8f,3.4f,-34, 0.4f,6.0f,6, 7,rust,0,0,0,"CatwalkSupportS");
    }
    /* ramps to catwalk */
    B(w,-29,2.6f, 34, 6,5.5f,10, 1,steel,0,1, 90,"RampNW");
    B(w, 29,2.6f, 34, 6,5.5f,10, 1,steel,0,1,-90,"RampNE");
    B(w,-29,2.6f,-34, 6,5.5f,10, 1,steel,0,1, 90,"RampSW");
    B(w, 29,2.6f,-34, 6,5.5f,10, 1,steel,0,1,-90,"RampSE");

    /* ---- outer walls - start above ground so nav raycasts work ---- */
    for(int s=-1;s<=1;s+=2){
        B(w,s*40.25f,0,0, 1.5f,24,82, 13,blue,0,0,0,"WallX");
        B(w,0,0,s*40.25f, 82,24,1.5f, 13,blue,0,0,0,"WallZ");
    }
    /* wall trim strips */
    for(int s=-1;s<=1;s+=2){
        B(w,s*39.4f,0.2f,0, 0.18f,0.6f,80, 5,cyan,1,0,0,"TrimLowX");
        B(w,s*39.4f,6.9f,0, 0.18f,0.3f,80, 5,cyan,1,0,0,"TrimMidX");
        B(w,s*39.4f,13.5f,0, 0.18f,0.25f,80, 12,orange,1,0,0,"TrimHighX");
        B(w,0,0.2f,s*39.4f, 80,0.5f,0.18f, 5,cyan,1,0,0,"TrimLowZ");
        B(w,0,6.9f,s*39.4f, 80,0.3f,0.18f, 5,cyan,1,0,0,"TrimMidZ");
        B(w,0,13.5f,s*39.4f, 80,0.25f,0.18f, 12,orange,1,0,0,"TrimHighZ");
    }
    /* roof beams / trusses */
    for(int i=-3;i<=3;i++) B(w,0,14.5f,i*11.5f, 82,0.5f,0.7f, 10,dark,0,0,0,"RoofBeam");
    for(int i=-3;i<=3;i++) B(w,i*11.5f,14.5f,0, 0.7f,0.5f,82, 10,dark,0,0,0,"RoofBeam");

    /* ---- pillars and crates ---- */
    for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2){
        B(w,sx*18,2.4f,sz*18, 2.0f,5,2.0f, 4,steel,0,0,0,"Pillar");
        B(w,sx*18,5.0f,sz*18, 2.6f,0.3f,2.6f, 5,cyan,1,0,0,"PillarTrim");
        B(w,sx*21,0.7f,sz*23, 2.8f,1.4f,2.8f, 0,rust,0,0,0,"Crate");
        B(w,sx*21,0.3f,sz*28, 1.8f,0.6f,1.8f, 15,dark,0,0,0,"LowCrate");
    }
    B(w,0,0.9f, 24, 4,2,2, 0,dark,0,0,0,"CenterCoverN");
    B(w,0,0.9f,-24, 4,2,2, 0,dark,0,0,0,"CenterCoverS");
    /* side catwalk connectors / low bridges */
    for(int s=-1;s<=1;s+=2) B(w,s*35,0.5f,0, 4,0.6f,14, 1,dark,0,0,0,"SideStep");

    /* emissive wall panels */
    for(int s=-1;s<=1;s+=2){
        B(w,s*39.35f,9.6f, 22, 0.2f,3.2f,8, 6,blue,1,0,0,"WallPanelBlue");
        B(w,s*39.35f,9.6f,-22, 0.2f,3.2f,8, 5,cyan,1,0,0,"WallPanelCyan");
        B(w, 22,9.6f,s*39.35f, 8,3.2f,0.2f, 6,blue,1,0,0,"WallPanelBlueZ");
        B(w,-22,9.6f,s*39.35f, 8,3.2f,0.2f, 5,cyan,1,0,0,"WallPanelCyanZ");
    }

    /* big "17" sign on west wall */
    B(w,-39.1f,9.8f,-5.0f, 0.3f,5.4f,0.8f, 12,orange,1,0,0,"Sign1");
    B(w,-39.1f,7.2f,-5.9f, 0.3f,0.6f,2.4f, 12,orange,1,0,0,"Sign1Base");
    B(w,-39.1f,12.3f,0.8f, 0.3f,0.6f,5.6f, 12,orange,1,0,0,"Sign7Top");
    B(w,-39.1f,10.6f,2.7f, 0.3f,3.8f,0.7f, 12,orange,1,0,0,"Sign7DiagA");
    B(w,-39.1f,8.2f,1.5f, 0.3f,2.2f,0.7f, 12,orange,1,0,0,"Sign7DiagB");

    /* barrels */
    for(int i=0;i<4;i++){
        Actor a; world_actor_defaults(&a,ACT_DECOR,0); a.pos=v3(-35.0f+i*0.6f,0.55f, 21.0f); a.size=v3(1.0f,1.1f,1.0f); a.material=7; a.shape=SHAPE_CYLINDER; a.flags=FLAG_SOLID; a.color=col3(0.45f,0.30f,0.20f); world_add(w,a);
    }
    Actor lava; world_actor_defaults(&lava,ACT_LAVA,0); lava.pos=v3(0,-4.0f,0); lava.size=v3(18,4,18); lava.color=col3(1.0f,0.25f,0.02f); world_add(w,lava);

    /* pickups */
    PK(w,-14,0.7f,-14, PICK_WEAPON_AR, 15);
    PK(w, 14,0.7f,-14, PICK_WEAPON_SHOCK, 15);
    PK(w,-14,0.7f, 14, PICK_WEAPON_ROCKET, 15);
    PK(w, 14,0.7f, 14, PICK_WEAPON_FLAK, 15);
    PK(w, 0,0.7f,  0, PICK_WEAPON_LIGHTNING, 15);
    PK(w,-12,0.7f,  0, PICK_HEALTH_BIG, 20);
    PK(w, 12,0.7f,  0, PICK_HEALTH_SMALL, 20);
    PK(w,  0,0.7f,-12, PICK_ARMOR_BIG, 20);
    PK(w,  0,0.7f, 12, PICK_ARMOR_SMALL, 20);
    PK(w,-18,0.7f,-18, PICK_AMMO, 20);
    PK(w, 18,0.7f, 18, PICK_AMMO, 20);

    /* jump pads */
    JP(w,-26, 0,  26,  90, 16);
    JP(w, 26, 0,  26, -90, 16);
    JP(w,-26, 0, -26,  90, 16);
    JP(w, 26, 0, -26, -90, 16);

    /* player starts */
    P(w,-24,-24, 45, 0); P(w, 24,-24, -45, 0);
    P(w,-24, 24, 135, 0); P(w, 24, 24, -135, 0);
    P(w,  0, 28, 0, 0);   P(w,  0,-28, 180, 0);

    /* lights */
    L(w,0,7.0f,0, col3(0.72f,0.85f,1.0f), 30, 2.4f);
    L(w,0,-2.0f,0, col3(1.0f,0.35f,0.06f), 22, 2.2f);
    L(w,0,3.5f,0, col3(0.15f,0.85f,1.0f), 10, 1.0f);
    for(int s=-1;s<=1;s+=2){
        L(w,s*34,7,0, col3(0.18f,0.65f,1.0f), 22, 1.5f);
        L(w,0,7,s*34, col3(1.0f,0.42f,0.10f), 20, 1.2f);
        L(w,s*34,7,s*30, col3(0.75f,0.15f,0.9f), 16, 1.0f);
        L(w,s*20,9,s*32, col3(0.55f,0.8f,1.0f), 14, 1.0f);
        L(w,s*30,2.2f,s*30, col3(0.2f,1.0f,0.5f), 12, 0.8f);
    }
    L(w,0,10,22, col3(0.3f,0.5f,1.0f), 14, 0.8f);
    L(w,0,10,-22, col3(0.3f,0.5f,1.0f), 14, 0.8f);

    /* lights */
    L(w,0,7.0f,0, col3(0.72f,0.85f,1.0f), 30, 2.4f);
    L(w,0,-2.0f,0, col3(1.0f,0.35f,0.06f), 22, 2.2f);
    L(w,0,3.5f,0, col3(0.15f,0.85f,1.0f), 10, 1.0f);
    for(int s=-1;s<=1;s+=2){
        L(w,s*34,7,0, col3(0.18f,0.65f,1.0f), 22, 1.5f);
        L(w,0,7,s*34, col3(1.0f,0.42f,0.10f), 20, 1.2f);
        L(w,s*34,7,s*30, col3(0.75f,0.15f,0.9f), 16, 1.0f);
        L(w,s*20,9,s*32, col3(0.55f,0.8f,1.0f), 14, 1.0f);
        L(w,s*30,2.2f,s*30, col3(0.2f,1.0f,0.5f), 12, 0.8f);
    }
    L(w,0,10,22, col3(0.3f,0.5f,1.0f), 14, 0.8f);
    L(w,0,10,-22, col3(0.3f,0.5f,1.0f), 14, 0.8f);

    /* jump pads */
    JP(w, 14,0, -90, 10, 16);
    JP(w,-14,0,  90, 10, 16);
    JP(w,0, 14, 180, 10, 16);
    JP(w,0,-14,   0, 10, 16);
    JP(w,0, 18,   0,  8, 18);
    JP(w,0,-18, 180,  8, 18);

    /* pickups */
    PK(w,0,4.1f,0, PICK_WEAPON_ROCKET, 25);
    PK(w,0,4.1f, 5.0f, PICK_AMMO, 20);
    PK(w,0,4.1f,-5.0f, PICK_AMMO, 20);
    PK(w,18,6.2f,32, PICK_WEAPON_SHOCK, 25);
    PK(w,-18,6.2f,-32, PICK_WEAPON_FLAK, 25);
    PK(w,32,1.0f,10, PICK_WEAPON_LIGHTNING, 30);
    PK(w,-32,1.0f,-10, PICK_WEAPON_AR, 30);
    PK(w,0,1.0f,30, PICK_WEAPON_AR, 30);
    PK(w,-22,6.2f,32, PICK_HEALTH_BIG, 30);
    PK(w,22,6.2f,-32, PICK_HEALTH_BIG, 30);
    PK(w,22,6.2f,32, PICK_ARMOR_BIG, 35);
    PK(w,-22,6.2f,-32, PICK_ARMOR_BIG, 35);
    for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2){
        PK(w,sx*30,1.3f,sz*30, PICK_HEALTH_SMALL, 18);
        PK(w,sx*30,1.3f,sz*26, PICK_ARMOR_SMALL, 22);
        PK(w,sx*24,0.6f,sz*10, PICK_AMMO, 18);
    }
    PK(w,0,6.2f,32, PICK_HEALTH_SMALL, 18);
    PK(w,0,6.2f,-32, PICK_HEALTH_SMALL, 18);

    /* spawn points */
    P(w, -20, -20, 45, 0); P(w, 20, -20, -45, 0); P(w, -20, 20, 135, 0); P(w, 20, 20, -135, 0);
    P(w, -30, 0, 90, 0); P(w, 30, 0, -90, 0); P(w, 0, 24, 180, 0); P(w, 0, -24, 0, 0);
    P(w, -24, -24, 45, 0); P(w, 24, -24, -135, 0);
}

/* ------------- serialize ------------- */
static void flags_str(uint32_t f,char* out,int cap){
    out[0]=0;
    if(f&FLAG_SOLID) strncat(out,"solid,",cap-strlen(out)-1);
    if(f&FLAG_EMISSIVE) strncat(out,"emissive,",cap-strlen(out)-1);
    int l=(int)strlen(out); if(l>0) out[l-1]=0;
}
static uint32_t parse_flags(const char* s){ uint32_t f=0; if(strstr(s,"solid"))f|=FLAG_SOLID; if(strstr(s,"emissive"))f|=FLAG_EMISSIVE; return f; }
bool world_save(const World* w,const char* path){
    FILE* f=fopen(path,"w"); if(!f) return false;
    fprintf(f,"arena2004 1\n");
    fprintf(f,"name %s\n",w->name);
    fprintf(f,"env fog=%.4f,%.4f,%.4f fogdens=%.4f amb=%.4f,%.4f,%.4f ground=%.4f,%.4f,%.4f sun=%.4f,%.4f,%.4f suncol=%.4f,%.4f,%.4f sunint=%.4f\n",
        w->fog_color.r,w->fog_color.g,w->fog_color.b,w->fog_density,w->ambient_sky.r,w->ambient_sky.g,w->ambient_sky.b,w->ambient_ground.r,w->ambient_ground.g,w->ambient_ground.b,
        w->sun_dir.x,w->sun_dir.y,w->sun_dir.z,w->sun_color.r,w->sun_color.g,w->sun_color.b,w->sun_intensity);
    for(int i=0;i<w->count;i++){
        const Actor* a=&w->actors[i]; char fl[64]; flags_str(a->flags,fl,sizeof(fl));
        fprintf(f,"actor type=%d sub=%d pos=%.3f,%.3f,%.3f size=%.3f,%.3f,%.3f yaw=%.2f color=%.3f,%.3f,%.3f mat=%d shape=%d flags=%s power=%.3f extra=%.3f respawn=%.2f name=%s\n",
            a->type,a->subtype,a->pos.x,a->pos.y,a->pos.z,a->size.x,a->size.y,a->size.z,a->yaw,a->color.r,a->color.g,a->color.b,a->material,a->shape,fl,a->power,a->extra,a->respawn,a->name);
    }
    fprintf(f,"end\n");
    fclose(f); return true;
}
static bool read_vec3(const char* s,Vec3* v){ return sscanf(s,"%f,%f,%f",&v->x,&v->y,&v->z)==3; }
static bool read_col(const char* s,Color* c){ float r,g,b; if(sscanf(s,"%f,%f,%f",&r,&g,&b)==3){ *c=col3(r,g,b); return true; } return false; }
bool world_load(World* w,const char* path){
    FILE* f=fopen(path,"r"); if(!f) return false;
    World nw; world_init(&nw);
    char line[2048]; bool ok=false; int line_no=0;
    while(fgets(line,sizeof(line),f)){
        line_no++;
        char* p=str_skip(line);
        if(!*p||*p=='#'||*p=='\r'||*p=='\n') continue;
        if(!strncmp(p,"arena2004",9)){ ok=true; continue; }
        if(!ok) continue;
        if(!strncmp(p,"end",3)) break;
        if(!strncmp(p,"name ",5)){ char* v=str_skip(p+5); int l=(int)strlen(v); while(l>0&&(v[l-1]=='\n'||v[l-1]=='\r'||v[l-1]==' '))v[--l]=0; str_copy(nw.name,v,64); continue; }
        if(!strncmp(p,"env ",4)){
            char* save=NULL; char* tok=strtok_r(p+4," \n\r",&save);
            while(tok){ char* eq=strchr(tok,'='); if(eq){ *eq=0; const char* key=tok; const char* val=eq+1;
                if(!strcmp(key,"fog")) read_col(val,&nw.fog_color);
                else if(!strcmp(key,"fogdens")) nw.fog_density=(float)atof(val);
                else if(!strcmp(key,"amb")) read_col(val,&nw.ambient_sky);
                else if(!strcmp(key,"ground")) read_col(val,&nw.ambient_ground);
                else if(!strcmp(key,"sun")) read_vec3(val,&nw.sun_dir);
                else if(!strcmp(key,"suncol")) read_col(val,&nw.sun_color);
                else if(!strcmp(key,"sunint")) nw.sun_intensity=(float)atof(val);
            } tok=strtok_r(NULL," \n\r",&save); }
            continue;
        }
        if(!strncmp(p,"actor ",6)){
            Actor a; world_actor_defaults(&a,ACT_BRUSH,0); memset(a.name,0,sizeof(a.name));
            char* save=NULL; char* tok=strtok_r(p+6," \n\r",&save);
            while(tok){ char* eq=strchr(tok,'='); if(eq){ *eq=0; const char* key=tok; const char* val=eq+1;
                if(!strcmp(key,"type")) a.type=atoi(val);
                else if(!strcmp(key,"sub")) a.subtype=atoi(val);
                else if(!strcmp(key,"pos")) read_vec3(val,&a.pos);
                else if(!strcmp(key,"size")) read_vec3(val,&a.size);
                else if(!strcmp(key,"yaw")) a.yaw=(float)atof(val);
                else if(!strcmp(key,"color")) read_col(val,&a.color);
                else if(!strcmp(key,"mat")) a.material=atoi(val);
                else if(!strcmp(key,"shape")) a.shape=atoi(val);
                else if(!strcmp(key,"flags")) a.flags=parse_flags(val);
                else if(!strcmp(key,"power")) a.power=(float)atof(val);
                else if(!strcmp(key,"extra")) a.extra=(float)atof(val);
                else if(!strcmp(key,"respawn")) a.respawn=(float)atof(val);
                else if(!strcmp(key,"name")) str_copy(a.name,val,MAX_NAME);
            } tok=strtok_r(NULL," \n\r",&save); }
            if(nw.count<MAX_ACTORS){ if(!a.name[0]) snprintf(a.name,MAX_NAME,"Actor_%d",nw.count); nw.actors[nw.count++]=a; }
            continue;
        }
    }
    fclose(f);
    if(!ok||nw.count==0){ fprintf(stderr,"[map] failed to load %s (line %d)\n",path,line_no); return false; }
    *w=nw; return true;
}
void world_collect_lights(const World* w,Vec3 eye,WorldLight* out,int max,int* count,float time){
    int n=0;
    for(int i=0;i<w->count&&n<max;i++){
        const Actor* a=&w->actors[i]; if(a->type!=ACT_LIGHT) continue;
        float d=v3len(v3sub(a->pos,eye)); float score=a->extra*100.0f/(1.0f+d*d*0.02f);
        WorldLight l; l.pos=a->pos; l.color=a->color; l.radius=a->power; l.intensity=a->extra;
        /* tiny flicker on warm lights */
        if(a->color.r>0.6f&&a->color.g<0.6f) l.intensity*=0.93f+0.07f*sinf(time*9.0f+i*1.7f);
        int j=n;
        while(j>0){ float sd=v3len(v3sub(out[j-1].pos,eye)); float sc=out[j-1].intensity*100.0f/(1.0f+sd*sd*0.02f); if(sc>=score)break; out[j]=out[j-1]; j--; }
        out[j]=l; n++;
    }
    *count=n;
}
