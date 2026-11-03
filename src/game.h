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
#define MAX_TREES 260
#define MAX_CRITTERS 64
#define MAX_AMBIENT 1000

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
    GM_INVASION,        /* zombie waves (Back4Blood / Unreal Invasion) */
    GM_COUNT
} GameMode;

typedef enum { WPN_AR=0, WPN_SHOCK, WPN_ROCKET, WPN_FLAK, WPN_LIGHTNING, WPN_COUNT } WeaponId;

/* Unreal RPG / Druid style permanent upgrades, bought in the Armory. */
enum {
    UPG_DAMAGE = 0,   /* +10% weapon damage per level        */
    UPG_FIRERATE,     /* faster shooting                      */
    UPG_HEALTH,       /* +25 max health                       */
    UPG_AMMO,         /* +15% max ammo                        */
    UPG_VAMP,         /* vampiric lifesteal on damage dealt   */
    UPG_SPEED,        /* faster movement                      */
    UPG_ARMOR,        /* damage reduction                     */
    UPG_COUNT
};

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
    /* rewards / RPG / item shop */
    int upg[UPG_COUNT];     /* permanent upgrade levels            */
    int credits;            /* spendable shop currency             */
    int score_points;       /* reward points earned this match     */
    int career_points;      /* persistent career score             */
    int rank;               /* derived career rank index           */
    int assists;
    int last_attacker;      /* for assist attribution              */
    float last_damage_t;
    float vamp_flash;       /* HUD feedback for lifesteal          */
    int vehicle;           /* vehicle index or -1 */
    bool in_vehicle;
    float use_latch;
    int bot_vehicle_target; /* vehicle index a bot wants to board, -1 none */
    float bot_vehicle_t;    /* cooldown before looking for another vehicle */
    /* Invasion zombies reuse the player capsule/movement/AI budget. This
     * lets 1000 zombies share collision, rendering LOD and networking. */
    bool zombie;
    int zombie_type;        /* 0 walker, 1 runner, 2 brute, 3 spitter */
    float zombie_attack;    /* melee/ranged attack cooldown */
    float zombie_growl;
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

#define VEH_COUNT 3
/* Modular vehicle damage: four wheels/thrusters, engine and weapon. */
#define VEH_WHEELS 4
#define VEH_ENGINE 4
#define VEH_WEAPON 5
#define VEH_MODULES 6

typedef struct {
    float hp, max_hp;
    bool destroyed;
} VehicleModule;

typedef struct {
    const char* name;
    const char* short_name;
    float max_hp;
    float max_speed;       /* horizontal units/s */
    float accel;           /* acceleration factor */
    float damage;
    float splash;
    float fire_delay;
    float proj_speed;
    float gravity;
    float hit_radius;      /* projectile hit sphere */
    float mass;            /* rough collision push factor */
} VehicleDef;

extern const VehicleDef vehicle_defs[VEH_COUNT];

typedef struct {
    bool active;
    int type;              /* 0 scorpion, 1 manta, 2 goliath */
    Vec3 pos, home;
    float yaw;
    float pitch;           /* manta nose attitude */
    float hp, max_hp;
    float respawn;
    float fire_timer;
    float hit_flash;
    float smoke;
    int driver;            /* player id or -1 */
    int killer;
    int team;
    float bob;
    VehicleModule modules[VEH_MODULES];
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
    int frags, deaths, assists, points, rank;
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

/* Procedural nature layer: purely decorative, no collision cost. */
typedef struct {
    Vec3 pos;
    float height, radius, phase, sway;
    int kind;        /* 0 pine, 1 broadleaf, 2 dead tree, 3 bush, 4 rock */
    int active;
} NatureProp;

/* Wildlife that roams the arena so the world never feels dead. */
typedef struct {
    Vec3 pos, home, vel;
    float yaw, speed, timer, phase, ground;
    int kind;        /* 0 wolf, 1 bear, 2 snake, 3 fox, 4 bird */
    int state;
    int active;
} Critter;

/* GTA-style ambient population, lightweight and purely visual. */
typedef struct {
    Vec3 pos, home, vel;
    float yaw, timer, phase;
    int palette;
    int active;
} AmbientNpc;

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
    float match_over_t;
    bool rewards_granted;
    int reward_total_points, reward_total_credits;
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
    /* zombie invasion (Back4Blood / Unreal Invasion) */
    int invasion_wave, invasion_wave_max;
    int invasion_spawn_left;      /* zombies still queued for this wave */
    int invasion_alive, invasion_killed, invasion_score;
    int invasion_survivors;       /* human/bot slots protected from cleanup */
    float invasion_wave_timer;    /* intermission / spawn drip */
    float invasion_spawn_timer;
    bool invasion_victory;
    int invasion_best_wave;
    /* mega server zones (PlanetSide style, local zone is simulated) */
    int zone_id, zone_total, zone_count;
    int zone_pop[MAX_ZONES];
    char zone_label[MAX_ZONES][24];
    float zone_tick;
    /* Metin2 style channels (instances) inside the current zone */
    int chan_id, chan_count, chan_cap;
    int chan_pop[MAX_CHANNELS];
    char chan_label[MAX_CHANNELS][16];
    /* procedural nature + ambient population */
    NatureProp trees[MAX_TREES];
    int tree_count;
    Critter critters[MAX_CRITTERS];
    int critter_count;
    AmbientNpc ambient[MAX_AMBIENT];
    int ambient_count;
    int ambient_capacity;
    bool nature_enabled;
    float nature_time;
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
int game_spawn_vehicle_type(Game* g, int team, int type);
void game_damage_vehicle(Game* g, int vehicle, float dmg, int attacker, Vec3 point);
void game_damage_vehicle_part(Game* g, int vehicle, float dmg, int attacker, Vec3 point, int part);
float game_vehicle_module_max_hp(int type, int part);
const char* game_vehicle_module_name(int part);
const char* game_vehicle_name(int type);
const char* game_vehicle_short(int type);
int game_vehicle_type_from_name(const char* name);
void game_configure_zones(Game* g, int total_players);
int game_spawn_zombie(Game* g, int type, Vec3 near);
int game_horde(Game* g, int count);
void game_generate_nature(Game* g);
void game_update_nature(Game* g, float dt);
void game_render_nature(Game* g, Renderer* r);
int game_set_ambient(Game* g, int count);
int game_ambient_active(Game* g);
void game_next_wave(Game* g);
int game_invasion_survivors_alive(Game* g);
const char* game_zombie_name(int type);
/* RPG / rewards / shop */
const char* game_upgrade_name(int id);
const char* game_upgrade_desc(int id);
int game_upgrade_max(int id);
int game_upgrade_cost(Player* p, int id);
bool game_upgrade_buy(Game* g, Player* p, int id);
float game_player_max_health(Player* p);
float game_player_max_armor(Player* p);
int game_player_ammo_max(Player* p, int wi);
float game_player_speed_mult(Player* p);
float game_player_fire_mult(Player* p);
float game_player_damage_mult(Player* p);
void game_player_refresh_stats(Player* p);
void game_add_reward(Game* g, Player* p, int points, int credits, const char* reason);
int game_rank_index(int career_points);
const char* game_rank_name(int rank);
const char* game_upgrade_key(int id);
int game_upgrade_from_name(const char* name);
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
