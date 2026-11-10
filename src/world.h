/* Arena2004 - map / world model */
#ifndef WORLD_H
#define WORLD_H
#include "math3d.h"

#define MAX_ACTORS 4096
#define MAX_NAME 48

typedef enum {
    ACT_BRUSH=0, ACT_LIGHT, ACT_PLAYER_START, ACT_PICKUP, ACT_JUMPPAD, ACT_LAVA, ACT_DECOR,
    ACT_STATICMESH, ACT_TRIGGER, ACT_NAV_POINT
} ActorType;

typedef enum {
    PICK_HEALTH_SMALL=0, PICK_HEALTH_BIG, PICK_ARMOR_SMALL, PICK_ARMOR_BIG, PICK_AMMO,
    PICK_WEAPON_AR, PICK_WEAPON_SHOCK, PICK_WEAPON_ROCKET, PICK_WEAPON_FLAK, PICK_WEAPON_LIGHTNING,
    PICKUP_TYPE_COUNT
} PickupType;

typedef enum { SHAPE_BOX=0, SHAPE_RAMP, SHAPE_CYLINDER, SHAPE_SPHERE } BrushShape;

enum { FLAG_SOLID=1, FLAG_EMISSIVE=2 };

typedef struct {
    int type;
    int subtype;
    Vec3 pos;
    Vec3 size;
    float yaw;
    Color color;
    int material;
    int shape;
    uint32_t flags;
    float power;      /* jump pad horizontal speed / light radius */
    float extra;      /* jump pad up speed / light intensity */
    float respawn;
    char name[MAX_NAME];
} Actor;

typedef struct {
    char name[64];
    Actor actors[MAX_ACTORS];
    int count;
    Color fog_color;
    float fog_density;
    Color ambient_sky;
    Color ambient_ground;
    Vec3 sun_dir;
    Color sun_color;
    float sun_intensity;
    int version;
} World;

typedef struct {
    bool hit;
    float t;
    Vec3 point, normal;
    int actor;
} RayHit;

typedef struct {
    Vec3 pos;
    Color color;
    float radius;
    float intensity;
} WorldLight;

void world_init(World* w);
void world_clear(World* w);
void world_default_deck17(World* w);
int world_add(World* w, Actor a);
void world_delete(World* w, int index);
AABB world_actor_aabb(const World* w, const Actor* a);
bool world_actor_solid(const Actor* a);
bool world_ramp_params(const Actor* a, int* axis, float* dir, float* y0, float* y1, float* lo, float* hi, float* cross_lo, float* cross_hi);
float world_ramp_height(const Actor* a, float x, float z);
bool world_raycast(const World* w, Ray r, float maxdist, bool solids_only, RayHit* out);
bool world_overlap_box(const World* w, AABB box, int ignore);
bool world_move_box(World* w, Vec3* pos, Vec3* vel, float dt, float radius, float height, bool* grounded, bool* hit_wall, int* ground_actor);
bool world_save(const World* w, const char* path);
bool world_load(World* w, const char* path);
void world_collect_lights(const World* w, Vec3 eye, WorldLight* out, int max, int* count, float time);
const char* actor_type_name(int t);
const char* pickup_name(int t);
const char* material_name(int m);
bool actor_is_weapon_pickup(int subtype, int* weapon_index);
void world_actor_defaults(Actor* a, int type, int subtype);

#endif
