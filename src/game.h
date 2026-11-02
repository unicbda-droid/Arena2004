/* Arena2004 - arena deathmatch game */
#ifndef GAME_H
#define GAME_H
#include "world.h"
#include "render.h"
#include "ui.h"
#include "audio.h"

#define MAX_PLAYERS 1000
#define MAX_PROJECTILES 96
#define MAX_PARTICLES 3072
#define MAX_BEAMS 48
#define MAX_EXPLOSIONS 24
#define MAX_KILLFEED 6
#define MAX_NAV 1600
#define MAX_NAV_LINKS 12
#define MAX_PATH 64
#define MAX_FLAGS 2
#define MAX_NODES 8
#define MAX_VEHICLES 16
#define MAX_ZONES 32
#define MAX_CHANNELS 64

/* UT2004 / Battlefield / PlanetSide inspired match types. */
typedef enum {
    GM_DM = 0,          /* free for all deathmatch            */
    GM_TDM,             /* team deathmatch                    */
    GM_CTF,             /* capture the flag                   */
    GM_VCTF,            /* vehicle capture the flag           */
    GM_ASSAULT,         /* attack / defend objectives         */
    GM_ONSLAUGHT,       /* node capture + power core          */
    GM_CONQUEST,        /* Battlefield style capture points   */
    GM_RPG,             /* persistent style leveling          */
    GM_BATTLE_ROYALE,   /* shrinking zone, last one standing  */
    GM_COUNT
} GameMode;

typedef enum { WPN_AR=0, WPN_SHOCK, WPN_ROCKET, WPN_FLAK, WPN_LIGHTNING, WPN_COUNT } WeaponId;

typedef struct {
    const char* name;
    const char* short_name;
    int ammo_max;
    float fire_delay, alt_delay;
    float damage, alt_damage;
    float splash, alt_splash;
    float speed, alt_speed;
    int pellets;
    float spread;
    Color color;
    int material;
} WeaponDef;

extern WeaponDef weapon_defs[WPN_COUNT];

typedef struct {
    float move_x, move_z;   /* -1..1 local */
    float yaw, pitch;
    bool jump, crouch, fire, alt_fire, dodge, use;
    int switch_weapon;      /* -1 none, else weapon id */
} PlayerInput;

typedef struct {
    bool active, bot, alive;
    int id;
    char name[32];
    Vec3 pos, vel;
    float yaw, pitch;
    float health, armor;
    float respawn_timer, protection;
    int frags, deaths, spree;
    int weapon;
    int ammo[WPN_COUNT];
    bool has[WPN_COUNT];
    float fire_timer, alt_timer, switch_timer;
    float dodge_timer, dodge_cooldown;
    float tap_timer[4];
    int last_tap[4];
    int jump_count;
    bool jump_latch, fire_latch, alt_latch;
    bool crouch;
    bool grounded;
    float phase, bob, land_kick, recoil, shake;
    float damage_flash;
    Vec3 damage_dir;
    float pickup_msg_t;
    char pickup_msg[64];
    float muzzle_flash;
    float view_kick;
    /* bot */
    int bot_target, bot_goal_node, bot_path[MAX_PATH], bot_path_len, bot_path_idx;
    float bot_repath, bot_strafe, bot_strafe_timer, bot_react, bot_aim_error, bot_jump_t;
    int bot_weapon_pref;
    float bot_think_t;
    float bot_aim_yaw, bot_aim_pitch;
    int killer;
    float death_time;
    PlayerInput input;
    /* game modes / factions / progression */
    int team;              /* 0 red, 1 blue, -1 free for all */
    Color color;
    int level, xp, xp_next;
    float dmg_mult;
    int vehicle;           /* vehicle index or -1 */
    bool in_vehicle;
    float use_latch;
    /* net */
    int net_owner;
    float last_snapshot_t;
    Vec3 render_pos; float render_yaw, render_pitch; float last_pos_t;
} Player;

typedef struct {
    bool active;
    int type, owner;
    Vec3 pos, vel;
    float life, damage, splash, radius;
    float gravity;
    Color color;
    float age;
    bool stuck;
    int bounces;
} Projectile;

typedef struct {
    bool active;
    Vec3 pos, vel;
    Color color;
    float size, life, max_life;
    float gravity, drag;
    bool additive;
    float stretch;
} Particle;

typedef struct {
    bool active;
    Vec3 a, b;
    Color color;
    float width, life, max_life;
} Beam;

typedef struct {
    bool active;
    Vec3 pos;
    Color color;
    float radius, life, max_life, intensity;
} Explosion;

typedef struct {
    int actor;
    bool active;
    float respawn;
} PickupState;

typedef struct {
    bool active;
    int team;              /* owning team: 0 red, 1 blue, -1 neutral */
    Vec3 home, pos;
    int carrier;           /* player id, -1 when on ground */
    bool carried;
    bool dropped;
    float return_timer;
    float bob;
} Flag;

typedef struct {
    bool active;
    int team;              /* owner, -1 neutral */
    int attacker;          /* team currently capturing */
    float progress;        /* 0..1 capture progress */
    float radius;
    Vec3 pos;
    char name[24];
    float pulse;
} ControlNode;

typedef struct {
    bool active;
    int type;              /* 0 scorpion, 1 manta, 2 tank */
    Vec3 pos, home;
    float yaw;
    float hp;
    float respawn;
    int driver;            /* player id or -1 */
    int team;
    float bob;
} Vehicle;

typedef struct {
    Vec3 pos;
    int nb[MAX_NAV_LINKS];
    int nn;
    int pad_landing; /* -1 or node */
    int hint;        /* 1 = authored ACT_NAV_POINT, 0 = automatic grid node */
} NavNode;

typedef struct {
    NavNode nodes[MAX_NAV];
    int count;
    int built;
} NavGraph;

typedef struct {
    int frags, deaths;
    char name[32];
    Color color;
    bool bot;
    int id;
} ScoreEntry;

typedef struct {
    char text[96];
    float time;
    Color color;
} KillFeedItem;

typedef struct Game {
    World* world;
    Player players[MAX_PLAYERS];
    int num_players;
    int local_player;
    bool demo_mode;
    bool pie_mode;
    bool server_mode;
    bool paused;
    bool match_over;
    float match_time, elapsed;
    int frag_limit;
    float time_limit;
    int winner;
    Projectile projs[MAX_PROJECTILES];
    Particle particles[MAX_PARTICLES];
    int particle_count;
    Beam beams[MAX_BEAMS];
    Explosion explosions[MAX_EXPLOSIONS];
    PickupState pickups[MAX_ACTORS];
    int pickup_actors[MAX_ACTORS];
    int pickup_actor_count;
    unsigned char trigger_state[MAX_ACTORS];
    float trigger_timer[MAX_ACTORS];
    char events[16][96];
    int event_count;
    NavGraph nav;
    KillFeedItem killfeed[MAX_KILLFEED];
    float announce_t;
    char announce[96];
    Color announce_color;
    float hitmarker, damage_flash;
    Vec3 damage_dir;
    float shake;
    float time;
    float start_countdown;
    bool started;
    int local_kills_this_life;
    float multikill_t;
    int multikill;
    int difficulty;
    float pickup_msg_t;
    char pickup_msg[96];
    float fps, ping;
    float snapshot_accum;
    /* demo */
    float demo_t;
    int demo_target;
    float last_shot;
    bool ready;
    int mode; /* 0 menu, 1 game, 2 editor, 3 net */
    /* match type, factions, objectives */
    int game_mode;
    int team_score[2];
    int team_kills[2];
    float score_limit;
    Vec3 team_base[2];
    float node_tick;
    Flag flags[MAX_FLAGS];
    ControlNode nodes[MAX_NODES];
    int node_count;
    Vehicle vehicles[MAX_VEHICLES];
    int vehicle_count;
    /* battle royale */
    float zone_radius, zone_target, zone_center_x, zone_center_z, zone_timer;
    int zone_stage, br_alive, br_total;
    float br_tick;
    /* mega server zones (PlanetSide style, local zone is simulated) */
    int zone_id, zone_total, zone_count;
    int zone_pop[MAX_ZONES];
    char zone_label[MAX_ZONES][24];
    float zone_tick;
    /* Metin2 style channels (instances) inside the current zone */
    int chan_id, chan_count, chan_cap;
    int chan_pop[MAX_CHANNELS];
    char chan_label[MAX_CHANNELS][16];
    /* results */
    char result[96];
    float fov;
    float gravity;
    /* network */
    bool networked;
    void* net;
    float net_send_accum;
    int net_snapshots;
    int net_last_ack;
} Game;

void game_init(Game* g, World* w, bool demo, int bot_count);
const char* game_mode_name(int mode);
const char* game_mode_short(int mode);
int game_mode_from_name(const char* name);
bool game_mode_team_based(int mode);
void game_set_mode(Game* g, int mode);
void game_set_team(Game* g, int player, int team);
void game_set_player_count(Game* g, int count);
int game_alive_count(Game* g);
void game_spawn_vehicle(Game* g, int team);
void game_configure_zones(Game* g, int total_players);
void game_select_zone(Game* g, int zone);
void game_select_channel(Game* g, int chan);
void game_shutdown(Game* g);
void game_reset(Game* g);
void game_update(Game* g, float dt);
void game_render_world(Game* g, Renderer* r, Camera* cam);
void game_render_objectives(Game* g, Renderer* r);
void game_render_viewmodel(Game* g, Renderer* r, float time);
void game_collect_lights(Game* g, Renderer* r);
void game_get_camera(Game* g, Camera* cam);
void game_fire(Game* g, Player* p, bool alt);
void game_damage(Game* g, Player* victim, float dmg, int attacker, Vec3 dir, Vec3 point, int weapon);
void game_respawn(Game* g, Player* p);
void game_start(Game* g);
void game_build_nav(Game* g);
int game_nav_nearest(Game* g, Vec3 pos, float maxdist);
bool game_nav_path(Game* g, int start, int end, int* out, int* outlen);
void game_demo_input(Game* g, float dt);
void game_add_killfeed(Game* g, const char* fmt, ...);
void game_announce(Game* g, const char* text, Color c, float dur);
void game_add_particle(Game* g, Vec3 pos, Vec3 vel, Color c, float size, float life, bool additive, float gravity, float drag);
void game_explosion(Game* g, Vec3 pos, float radius, float damage, int attacker, Color color);
void game_beam_fx(Game* g, Vec3 a, Vec3 b, Color c, float width, float life);
void game_pickup_msg(Game* g, const char* fmt, ...);
void game_render_players(Game* g, Renderer* r);
void game_render_pickups(Game* g, Renderer* r);
void game_render_projectiles(Game* g, Renderer* r);
void game_render_particles(Game* g, Renderer* r);
void game_update_camera_shake(Game* g, float dt);

/* scoring */
void game_sort_scores(Game* g, ScoreEntry* out, int* n);
const char* game_time_string(float t, char* buf, int cap);

#endif
