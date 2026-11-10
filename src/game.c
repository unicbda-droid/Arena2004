#include "game.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* weapons                                                             */
/* ------------------------------------------------------------------ */
WeaponDef weapon_defs[WPN_COUNT] = {
    /* name          short  ammo  delay  alt    dmg   alt   spl alt  spd   altspd  pel spread color                          mat */
    { "Assault Rifle","AR",    120, 0.105f,0.70f,  9.0f, 30.0f, 0,   0,   0,    0,     1, 0.018f, {0.85f,0.86f,0.90f,1}, 0 },
    { "Shock Rifle",  "SHOCK",  60, 0.72f, 1.15f, 45.0f, 30.0f, 0,  70.0f,0,    22.0f, 1, 0.000f, {0.30f,0.65f,1.00f,1}, 5 },
    { "Rocket Launcher","RL",   30, 1.05f, 1.65f, 90.0f, 40.0f, 70.0f,50.0f,26.0f,16.0f, 1, 0.000f, {1.00f,0.40f,0.14f,1}, 11 },
    { "Flak Cannon",  "FLAK",   30, 0.95f, 1.35f, 13.0f, 80.0f, 0,   45.0f,0,    18.0f, 9, 0.085f, {1.00f,0.66f,0.20f,1}, 10 },
    { "Lightning Gun","LG",     40, 0.85f, 0.85f, 34.0f, 34.0f, 0,   0,   0,    0,     1, 0.000f, {0.70f,0.45f,1.00f,1}, 5 },
};

static const char* BOT_NAMES[] = { "Xan", "Malcolm", "Gorge", "Rapier", "Kragg", "Brock", "Lauren", "Satin", "Visse", "Rylisa" };

/* Vehicle stats. Deliberately simple numbers: ground vehicles use the normal
 * player movement step, the Manta uses cheap hover motion. This keeps 16
 * vehicles cheaper than one extra bot and avoids physics spikes. */
const VehicleDef vehicle_defs[VEH_COUNT] = {
    /* name       short    hp    speed accel dmg  spl  delay speed grav radius mass */
    { "Scorpion", "SCORP", 240, 24.0f, 30.0f, 16.0f, 0.0f, 0.11f, 70.0f, 0.0f, 2.1f, 1.0f },
    { "Manta",    "MANTA", 180, 30.0f, 26.0f, 18.0f, 2.4f, 0.20f, 60.0f, 0.0f, 2.5f, 0.7f },
    { "Goliath",  "GOLI",  520, 13.0f, 16.0f, 50.0f, 5.0f, 0.95f, 46.0f, 3.0f, 3.7f, 2.4f },
};

float game_vehicle_module_max_hp(int type, int part) {
    if (type < 0 || type >= VEH_COUNT) type = 0;
    if (part < 0 || part >= VEH_MODULES) part = VEH_ENGINE;
    float base = vehicle_defs[type].max_hp;
    if (part < VEH_WHEELS) return base * 0.32f;
    if (part == VEH_ENGINE) return base * 0.45f;
    return base * 0.30f;
}

const char* game_vehicle_module_name(int part) {
    if (part < 0 || part >= VEH_MODULES) return "HULL";
    if (part < VEH_WHEELS) return "WHEEL";
    if (part == VEH_ENGINE) return "ENGINE";
    return "WEAPON";
}

const char* game_vehicle_name(int type) {
    if (type < 0 || type >= VEH_COUNT) type = 0;
    return vehicle_defs[type].name;
}
const char* game_vehicle_short(int type) {
    if (type < 0 || type >= VEH_COUNT) type = 0;
    return vehicle_defs[type].short_name;
}
int game_vehicle_type_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    char b[24];
    str_copy(b, name, sizeof(b));
    for (int i = 0; b[i]; i++) {
        if (b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] - 'A' + 'a');
        if (b[i] == ' ' || b[i] == '_' || b[i] == '-') b[i] = 0;
    }
    const char* k = b;
    if (!strcmp(k, "scorpion") || !strcmp(k, "scorp") || !strcmp(k, "buggy") || !strcmp(k, "0")) return 0;
    if (!strcmp(k, "manta") || !strcmp(k, "flyer") || !strcmp(k, "air") || !strcmp(k, "1")) return 1;
    if (!strcmp(k, "goliath") || !strcmp(k, "tank") || !strcmp(k, "goli") || !strcmp(k, "2")) return 2;
    return -1;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float frandr(float a, float b) { return a + (b - a) * frand(); }
static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static Color team_color(int id) {
    static const Color c[8] = {
        {0.95f,0.22f,0.18f,1}, {0.25f,0.55f,1.00f,1}, {0.35f,0.95f,0.35f,1}, {1.00f,0.75f,0.15f,1},
        {0.85f,0.35f,1.00f,1}, {0.15f,0.90f,0.85f,1}, {1.00f,0.50f,0.10f,1}, {0.95f,0.95f,0.95f,1}
    };
    return c[id & 7];
}
static Color team_base_color(int team) {
    return team == 0 ? col3(0.96f, 0.25f, 0.20f) : col3(0.25f, 0.58f, 1.00f);
}

/* ------------------------------------------------------------------ */
/* match types (UT2004 / Battlefield / PlanetSide inspired)            */
/* ------------------------------------------------------------------ */
static const char* MODE_NAMES[GM_COUNT] = {
    "Deathmatch", "Team Deathmatch", "Capture the Flag", "Vehicle CTF",
    "Assault", "Onslaught", "Conquest", "RPG Deathmatch", "Battle Royale",
    "Zombie Invasion", "Altis Life"
};
static const char* MODE_SHORT[GM_COUNT] = { "DM", "TDM", "CTF", "VCTF", "ASSAULT", "ONSLAUGHT", "CONQUEST", "RPG", "BR", "INVASION", "ALTIS" };

const char* game_mode_name(int mode) {
    if (mode < 0 || mode >= GM_COUNT) mode = GM_DM;
    return MODE_NAMES[mode];
}
const char* game_mode_short(int mode) {
    if (mode < 0 || mode >= GM_COUNT) mode = GM_DM;
    return MODE_SHORT[mode];
}
int game_mode_from_name(const char* name) {
    if (!name || !name[0]) return GM_DM;
    char b[32];
    str_copy(b, name, sizeof(b));
    for (int i = 0; b[i]; i++) { if (b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] - 'A' + 'a'); if (b[i] == ' ' || b[i] == '_' || b[i] == '-') b[i] = 0; }
    const char* k = b;
    if (!strcmp(k, "dm") || !strcmp(k, "deathmatch") || !strcmp(k, "ffa")) return GM_DM;
    if (!strcmp(k, "tdm") || !strcmp(k, "teamdeathmatch")) return GM_TDM;
    if (!strcmp(k, "ctf") || !strcmp(k, "capturetheflag")) return GM_CTF;
    if (!strcmp(k, "vctf") || !strcmp(k, "vehictf") || !strcmp(k, "vehiclecapturetheflag")) return GM_VCTF;
    if (!strcmp(k, "as") || !strcmp(k, "assault")) return GM_ASSAULT;
    if (!strcmp(k, "ons") || !strcmp(k, "onslaught")) return GM_ONSLAUGHT;
    if (!strcmp(k, "con") || !strcmp(k, "conquest") || !strcmp(k, "battlefield")) return GM_CONQUEST;
    if (!strcmp(k, "rpg")) return GM_RPG;
    if (!strcmp(k, "br") || !strcmp(k, "battleroyale") || !strcmp(k, "royale")) return GM_BATTLE_ROYALE;
    if (!strcmp(k, "inv") || !strcmp(k, "invasion") || !strcmp(k, "zombie") || !strcmp(k, "zombies") ||
        !strcmp(k, "survival") || !strcmp(k, "back4blood") || !strcmp(k, "horde")) return GM_INVASION;
    if (!strcmp(k, "altis") || !strcmp(k, "altislife") || !strcmp(k, "life") || !strcmp(k, "roleplay") ||
        !strcmp(k, "rp") || !strcmp(k, "arma")) return GM_ALTIS;
    return -1;
}
bool game_mode_team_based(int mode) {
    return mode == GM_TDM || mode == GM_CTF || mode == GM_VCTF || mode == GM_ASSAULT ||
           mode == GM_ONSLAUGHT || mode == GM_CONQUEST || mode == GM_RPG;
}

/* ------------------------------------------------------------------ */
/* RPG / Druid style progression, rewards and item shop                */
/* ------------------------------------------------------------------ */
static const char* UPG_NAMES[UPG_COUNT] = {
    "DAMAGE CORE", "OVERCLOCK", "BIO WEAVE", "AMMO BELT",
    "VAMPIRIC RUNE", "SERVO LEGS", "ARMOR PLATING"
};
static const char* UPG_KEYS[UPG_COUNT] = {
    "damage", "firerate", "health", "ammo", "vamp", "speed", "armor"
};
static const char* UPG_DESC[UPG_COUNT] = {
    "+10% Waffenschaden pro Stufe",
    "+7% Feuerrate pro Stufe",
    "+25 max. HP pro Stufe",
    "+15% Munitionskapazitaet",
    "2% Lebensraub pro Stufe",
    "+5% Laufgeschwindigkeit",
    "-3.5% erlittener Schaden"
};
static const int UPG_BASE[UPG_COUNT] = { 80, 90, 70, 70, 120, 80, 90 };
static const int UPG_MAXV[UPG_COUNT] = { 10, 8, 10, 8, 5, 8, 6 };

const char* game_upgrade_name(int id) { return (id >= 0 && id < UPG_COUNT) ? UPG_NAMES[id] : "?"; }
const char* game_upgrade_desc(int id) { return (id >= 0 && id < UPG_COUNT) ? UPG_DESC[id] : ""; }
const char* game_upgrade_key(int id)  { return (id >= 0 && id < UPG_COUNT) ? UPG_KEYS[id] : "?"; }
int game_upgrade_max(int id) { return (id >= 0 && id < UPG_COUNT) ? UPG_MAXV[id] : 0; }
int game_upgrade_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < UPG_COUNT; i++)
        if (!strcasecmp(name, UPG_KEYS[i]) || !strcasecmp(name, UPG_NAMES[i])) return i;
    if (!strcasecmp(name, "dmg")) return UPG_DAMAGE;
    if (!strcasecmp(name, "rate") || !strcasecmp(name, "fire")) return UPG_FIRERATE;
    if (!strcasecmp(name, "hp")) return UPG_HEALTH;
    if (!strcasecmp(name, "vampire") || !strcasecmp(name, "lifesteal")) return UPG_VAMP;
    return -1;
}

int game_upgrade_cost(Player* p, int id) {
    if (!p || id < 0 || id >= UPG_COUNT) return 999999;
    int lvl = p->upg[id];
    if (lvl >= UPG_MAXV[id]) return -1;
    return UPG_BASE[id] + lvl * (UPG_BASE[id] * 3 / 4);
}

float game_player_damage_mult(Player* p) {
    if (!p) return 1.0f;
    int lvl = p->level > 0 ? p->level : 1;
    return (1.0f + 0.08f * (float)(lvl - 1)) * (1.0f + 0.10f * (float)p->upg[UPG_DAMAGE]);
}
float game_player_fire_mult(Player* p) {
    if (!p) return 1.0f;
    return 1.0f / (1.0f + 0.07f * (float)p->upg[UPG_FIRERATE]);
}
float game_player_speed_mult(Player* p) {
    if (!p) return 1.0f;
    return 1.0f + 0.05f * (float)p->upg[UPG_SPEED];
}
float game_player_max_health(Player* p) {
    int lvl = p->level > 0 ? p->level : 1;
    return 100.0f + (float)(lvl - 1) * 20.0f + 25.0f * (float)p->upg[UPG_HEALTH];
}
float game_player_max_armor(Player* p) {
    return 100.0f + 20.0f * (float)p->upg[UPG_ARMOR];
}
int game_player_ammo_max(Player* p, int wi) {
    if (wi < 0 || wi >= WPN_COUNT) return 0;
    float mul = 1.0f + 0.15f * (float)p->upg[UPG_AMMO];
    return (int)((float)weapon_defs[wi].ammo_max * mul + 0.5f);
}
/* Magazine sizes keep the UT-style ammo pool but add a short reload pause.
 * The pool is still the total ammo; mag[] is only the loaded part. */
int game_player_mag_cap(int wi) {
    static const int caps[WPN_COUNT] = { 30, 8, 4, 6, 8 };
    if (wi < 0 || wi >= WPN_COUNT) return 0;
    return caps[wi];
}
static float reload_time(int wi) {
    static const float times[WPN_COUNT] = { 1.05f, 1.30f, 1.55f, 1.45f, 1.15f };
    if (wi < 0 || wi >= WPN_COUNT) return 1.0f;
    return times[wi];
}
static void reload_begin(Player* p) {
    if (!p || !p->alive) return;
    int wi = p->weapon;
    if (wi < 0 || wi >= WPN_COUNT || !p->has[wi]) return;
    if (p->reload_timer > 0.0f) return;
    int cap = game_player_mag_cap(wi);
    if (p->mag[wi] >= cap || p->mag[wi] >= p->ammo[wi]) return;
    p->reload_weapon = wi;
    p->reload_total = reload_time(wi);
    p->reload_timer = p->reload_total;
}
static void reload_tick(Game* g, Player* p, float dt) {
    if (p->reload_timer <= 0.0f) return;
    /* Switching weapons cancels the reload instead of silently finishing it. */
    if (p->reload_weapon < 0 || p->weapon != p->reload_weapon) {
        p->reload_timer = 0.0f;
        p->reload_total = 0.0f;
        p->reload_weapon = -1;
        return;
    }
    p->reload_timer -= dt;
    if (p->reload_timer > 0.0f) return;
    int wi = p->reload_weapon;
    if (wi >= 0 && wi < WPN_COUNT) {
        int cap = game_player_mag_cap(wi);
        p->mag[wi] = p->ammo[wi] < cap ? p->ammo[wi] : cap;
        if (p->id == g->local_player) audio_play(SFX_PICKUP_AMMO, 0.35f, 0.18f);
    }
    p->reload_timer = 0.0f;
    p->reload_total = 0.0f;
    p->reload_weapon = -1;
}
void game_player_refresh_stats(Player* p) {
    if (!p) return;
    p->dmg_mult = game_player_damage_mult(p);
}

bool game_upgrade_buy(Game* g, Player* p, int id) {
    if (!g || !p || id < 0 || id >= UPG_COUNT) return false;
    if (p->upg[id] >= UPG_MAXV[id]) return false;
    int cost = game_upgrade_cost(p, id);
    if (p->credits < cost) return false;
    p->credits -= cost;
    p->upg[id]++;
    float before = game_player_max_health(p);
    game_player_refresh_stats(p);
    if (id == UPG_HEALTH) p->health = fminf(before, p->health + 25.0f);
    if (p->id == g->local_player) {
        char b[96];
        snprintf(b, sizeof(b), "%s LV%d", game_upgrade_name(id), p->upg[id]);
        game_pickup_msg(g, "%s", b);
        game_announce(g, b, col3(0.45f, 0.95f, 1.0f), 1.8f);
        audio_play(SFX_PICKUP_WEAPON, 0.55f, 0.0f);
    }
    return true;
}

int game_rank_index(int career_points) {
    static const int TH[10] = { 0, 400, 1200, 2800, 5500, 9500, 15000, 23000, 34000, 50000 };
    int r = 0;
    for (int i = 0; i < 10; i++) if (career_points >= TH[i]) r = i;
    return r;
}
const char* game_rank_name(int rank) {
    static const char* R[10] = {
        "RECRUIT", "PRIVATE", "CORPORAL", "SERGEANT", "LIEUTENANT",
        "CAPTAIN", "MAJOR", "COLONEL", "COMMANDER", "GENERAL"
    };
    return R[rank < 0 ? 0 : (rank > 9 ? 9 : rank)];
}

void game_add_reward(Game* g, Player* p, int points, int credits, const char* reason) {
    if (!g || !p) return;
    if (points < 0) points = 0;
    if (credits < 0) credits = 0;
    p->score_points += points;
    p->career_points += points;
    p->credits += credits;
    g->reward_total_points += points;
    g->reward_total_credits += credits;
    int nr = game_rank_index(p->career_points);
    if (nr > p->rank) {
        p->rank = nr;
        if (p->id == g->local_player) {
            char b[96];
            snprintf(b, sizeof(b), "RANK UP: %s", game_rank_name(nr));
            game_announce(g, b, col3(1.0f, 0.82f, 0.25f), 3.0f);
            game_add_killfeed(g, "%s is now %s", p->name, game_rank_name(nr));
        }
    }
    if (p->id == g->local_player && points > 0 && reason && reason[0]) {
        /* compact feedback, the full ledger is on the results screen */
        char b[96];
        snprintf(b, sizeof(b), "+%d PTS  +%d CR  //  %s", points, credits, reason);
        str_copy(g->pickup_msg, b, sizeof(g->pickup_msg));
        g->pickup_msg_t = 2.0f;
    }
}

/* Match-end payout. The results screen reads the local player's ledger, so
 * every reward is mirrored into score_points/credits as soon as the match
 * freezes. Bots still receive their points for the scoreboard. */
static void grant_match_rewards(Game* g) {
    if (!g || g->rewards_granted) return;
    g->rewards_granted = true;
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) return;
    Player* me = &g->players[li];
    bool win = false;
    if (g->game_mode == GM_INVASION) {
        win = g->invasion_victory;
    } else if (game_mode_team_based(g->game_mode)) {
        int wt = (g->winner >= 0 && g->winner < g->num_players) ? g->players[g->winner].team : -1;
        win = (me->team >= 0 && me->team == wt);
    } else {
        win = (g->winner == li);
    }
    int place = 1;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active || p->zombie) continue;
        if (p->frags > me->frags) place++;
    }
    int pts = 0, cr = 0;
    const char* reason = "DEFEAT";
    if (win) {
        pts = 500 + (place == 1 ? 200 : 0);
        cr = 100 + (place == 1 ? 40 : 0);
        reason = "VICTORY";
    } else {
        pts = (place <= 3) ? 220 : 120;
        cr = (place <= 3) ? 45 : 20;
    }
    if (g->game_mode == GM_INVASION) {
        int w = g->invasion_wave > 0 ? g->invasion_wave : 1;
        if (g->invasion_victory) { pts = 600 + w * 120; cr = 220 + w * 35; reason = "INVASION CLEARED"; }
        else { pts = 100 + w * 60 + g->invasion_killed * 3; cr = 30 + w * 12; reason = "OVERRUN"; }
    }
    game_add_reward(g, me, pts, cr, reason);
    /* Team mates and the winning side get a smaller cut so the board is alive. */
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active || p->zombie || p == me) continue;
        int bp = p->team >= 0 && p->team == me->team ? (win ? 80 : 40) : 20;
        game_add_reward(g, p, bp, 4, NULL);
    }
}

static void map_bounds(World* w, Vec3* center, float* extent) {
    Vec3 mn = v3(1e9f, 1e9f, 1e9f), mx = v3(-1e9f, -1e9f, -1e9f);
    int n = 0;
    for (int i = 0; i < w->count; i++) {
        Actor* a = &w->actors[i];
        if (a->type == ACT_LIGHT) continue;
        Vec3 h = v3(fabsf(a->size.x) * 0.5f, fabsf(a->size.y) * 0.5f, fabsf(a->size.z) * 0.5f);
        Vec3 lo = v3(a->pos.x - h.x, a->pos.y - h.y, a->pos.z - h.z);
        Vec3 hi = v3(a->pos.x + h.x, a->pos.y + h.y, a->pos.z + h.z);
        if (lo.x < mn.x) mn.x = lo.x;
        if (lo.y < mn.y) mn.y = lo.y;
        if (lo.z < mn.z) mn.z = lo.z;
        if (hi.x > mx.x) mx.x = hi.x;
        if (hi.y > mx.y) mx.y = hi.y;
        if (hi.z > mx.z) mx.z = hi.z;
        n++;
    }
    if (n == 0) { *center = v3(0, 0, 0); *extent = 48.0f; return; }
    *center = v3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
    float ex = fmaxf(mx.x - mn.x, mx.z - mn.z) * 0.5f;
    if (ex < 24.0f) ex = 24.0f;
    *extent = ex;
}

static int spawn_points(Game* g, Vec3* out, int cap) {
    int n = 0;
    for (int i = 0; i < g->world->count && n < cap; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type == ACT_PLAYER_START) out[n++] = a->pos;
    }
    return n;
}

/* Authored player starts can sit inside solid brushes (DM-Deck17 shipped with
 * several starts inside pillars and corner pads), which traps the camera in
 * geometry. Check the player capsule against every solid actor. A larger
 * radius is used to reject cramped spots such as the pocket behind the
 * corner rails: the capsule fits there, but it is visually a closed box. */
static bool spawn_pos_free_r(const World* w, Vec3 feet, float radius) {
    AABB cap = { v3(feet.x - radius, feet.y + 0.02f, feet.z - radius),
                 v3(feet.x + radius, feet.y + 1.75f, feet.z + radius) };
    for (int i = 0; i < w->count; i++) {
        const Actor* a = &w->actors[i];
        if (!world_actor_solid(a)) continue;
        if (aabb_overlap(cap, world_actor_aabb(w, a))) return false;
    }
    return true;
}

static bool spawn_pos_free(const World* w, Vec3 feet) {
    return spawn_pos_free_r(w, feet, 0.45f);
}

static bool spawn_has_floor(const World* w, Vec3 feet) {
    Ray r;
    r.origin = v3(feet.x, feet.y + 0.35f, feet.z);
    r.dir = v3(0, -1, 0);
    RayHit hit;
    return world_raycast(w, r, 1.0f, true, &hit) && hit.normal.y > 0.35f;
}

static bool spawn_clear(const World* w, Vec3 feet, float radius) {
    return spawn_pos_free_r(w, feet, radius) && spawn_has_floor(w, feet);
}

static Vec3 spawn_safe_pos(const World* w, Vec3 base) {
    /* First try to keep a comfortable free ring around the spawn; if the map
     * is tight, fall back to the player capsule radius. */
    static const float clearances[2] = { 1.15f, 0.45f };
    for (int pass = 0; pass < 2; pass++) {
        float cr = clearances[pass];
        if (spawn_clear(w, base, cr)) return base;
        /* Search sideways first so a spawn next to a pillar stays on the deck
         * instead of being pushed on top of it. */
        for (int ring = 1; ring <= 12; ring++) {
            float radius = (float)ring * 0.4f;
            for (int k = 0; k < 12; k++) {
                float ang = (float)k * (2.0f * PI / 12.0f);
                Vec3 p = v3(base.x + cosf(ang) * radius, base.y, base.z + sinf(ang) * radius);
                if (spawn_clear(w, p, cr)) return p;
            }
        }
    }
    for (float lift = 0.5f; lift <= 4.0f; lift += 0.5f) {
        Vec3 p = v3(base.x, base.y + lift, base.z);
        if (spawn_pos_free(w, p)) return p;
    }
    return v3(base.x, base.y + 0.5f, base.z);
}

static void find_bases(Game* g, Vec3* a, Vec3* b) {
    Vec3 pts[128];
    int n = spawn_points(g, pts, 128);
    if (n >= 2) {
        float best = -1.0f;
        for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
            float dx = pts[i].x - pts[j].x, dz = pts[i].z - pts[j].z;
            float d = dx * dx + dz * dz;
            if (d > best) { best = d; *a = pts[i]; *b = pts[j]; }
        }
        return;
    }
    Vec3 c; float e;
    map_bounds(g->world, &c, &e);
    if (n == 1) {
        *a = pts[0];
        *b = v3(c.x + e * 0.45f, c.y, c.z);
        return;
    }
    *a = v3(c.x - e * 0.45f, c.y, c.z);
    *b = v3(c.x + e * 0.45f, c.y, c.z);
}

static void assign_teams(Game* g) {
    bool tb = game_mode_team_based(g->game_mode);
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        p->team = tb ? (i & 1) : -1;
        p->color = tb ? team_base_color(p->team) : team_color(i);
    }
}

void game_set_mode(Game* g, int mode) {
    if (!g) return;
    if (mode < 0 || mode >= GM_COUNT) mode = GM_DM;
    g->game_mode = mode;
    g->team_score[0] = g->team_score[1] = 0;
    g->team_kills[0] = g->team_kills[1] = 0;
    g->score_limit = (float)g->frag_limit;
    switch (mode) {
    case GM_CTF: case GM_VCTF: g->score_limit = 5.0f; break;
    case GM_ASSAULT: g->score_limit = 1.0f; break;
    case GM_ONSLAUGHT: g->score_limit = 120.0f; break;
    case GM_CONQUEST: g->score_limit = 250.0f; break;
    case GM_RPG: g->score_limit = (float)(g->frag_limit > 0 ? g->frag_limit : 50); break;
    case GM_BATTLE_ROYALE: g->score_limit = 0.0f; break;
    case GM_INVASION: g->score_limit = 0.0f; break;
    case GM_ALTIS: g->score_limit = 0.0f; break;
    default: break;
    }
    assign_teams(g);
}

void game_set_team(Game* g, int player, int team) {
    if (!g || player < 0 || player >= g->num_players) return;
    if (team < 0 || team > 1) team = -1;
    g->players[player].team = team;
    g->players[player].color = team >= 0 ? team_base_color(team) : team_color(player);
}

int game_alive_count(Game* g) {
    int n = 0;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->active && p->alive && !p->zombie) n++;
    }
    return n;
}

static void init_player_slot(Game* g, Player* p, int i) {
    memset(p, 0, sizeof(*p));
    p->id = i;
    p->active = true;
    p->bot = g->demo_mode ? true : (i != 0);
    p->alive = true;
    p->health = 100;
    p->armor = 0;
    p->weapon = WPN_AR;
    p->has[WPN_AR] = true;
    p->ammo[WPN_AR] = 60;
    p->mag[WPN_AR] = game_player_mag_cap(WPN_AR);
    p->reload_timer = 0; p->reload_total = 0; p->reload_weapon = -1;
    p->protection = 2.0f;
    p->bot_weapon_pref = WPN_AR;
    p->bot_aim_error = 0.02f;
    p->bot_strafe = 1;
    p->level = 1;
    p->xp = 0;
    p->xp_next = 100;
    p->dmg_mult = 1.0f;
    memset(p->upg, 0, sizeof(p->upg));
    p->credits = 0;
    p->score_points = 0;
    p->career_points = 0;
    p->rank = 0;
    p->assists = 0;
    p->last_attacker = -1;
    p->last_damage_t = -100.0f;
    p->vamp_flash = 0.0f;
    p->vehicle = -1;
    p->in_vehicle = false;
    p->bot_vehicle_target = -1;
    p->bot_vehicle_t = 0.0f;
    p->team = -1;
    p->color = team_color(i);
    if (i == 0 && !g->demo_mode) str_copy(p->name, "Player", sizeof(p->name));
    else {
        char buf[32];
        int k = i % 10;
        if (i < 10) snprintf(buf, sizeof(buf), "%s", BOT_NAMES[k]);
        else snprintf(buf, sizeof(buf), "%s-%d", BOT_NAMES[k], i / 10 + 1);
        str_copy(p->name, buf, sizeof(p->name));
    }
    p->render_pos = p->pos;
}

void game_set_player_count(Game* g, int count) {
    if (!g) return;
    if (count < 1) count = 1;
    if (count > MAX_PLAYERS) count = MAX_PLAYERS;
    if (count > g->num_players) {
        for (int i = g->num_players; i < count; i++) init_player_slot(g, &g->players[i], i);
    } else {
        for (int i = count; i < g->num_players; i++) g->players[i].active = false;
    }
    g->num_players = count;
    assign_teams(g);
}

/* PlanetSide style world zones + Metin2 style channels. Only the local
 * channel is fully simulated; the other populations are tracked
 * statistically and shown in the HUD/console. */
static void configure_channels(Game* g, int pop) {
    g->chan_cap = MAX_PLAYERS;
    int cc = (pop + g->chan_cap - 1) / g->chan_cap;
    if (cc < 1) cc = 1;
    if (cc > MAX_CHANNELS) cc = MAX_CHANNELS;
    g->chan_count = cc;
    int cb = pop / cc, cr = pop % cc;
    for (int c = 0; c < cc; c++) {
        g->chan_pop[c] = cb + (c < cr ? 1 : 0);
        snprintf(g->chan_label[c], sizeof(g->chan_label[c]), "CH-%d", c + 1);
    }
    g->chan_id = 0;
    g->zone_tick = 0;
}

void game_configure_zones(Game* g, int total_players) {
    if (!g) return;
    if (total_players < 1) total_players = 1;
    if (total_players > 30000) total_players = 30000;
    g->zone_total = total_players;
    static const char* ZNAMES[] = { "Indar", "Hossin", "Amerish", "Esamir", "Oshur", "Koltyr", "Searhus", "Cyssor", "Forseral", "Ceryshen", "Solsar", "Ishundar" };
    int zc = 1 + (total_players - 1) / 2500;
    if (zc > MAX_ZONES) zc = MAX_ZONES;
    if (zc > 12) zc = 12;
    g->zone_count = zc;
    int base = total_players / zc, rem = total_players % zc;
    for (int z = 0; z < zc; z++) {
        g->zone_pop[z] = base + (z < rem ? 1 : 0);
        snprintf(g->zone_label[z], sizeof(g->zone_label[z]), "%s", z < 12 ? ZNAMES[z] : "Sektor");
    }
    g->zone_id = 0;
    /* channels: cap the local simulation at the real player limit */
    configure_channels(g, g->zone_pop[g->zone_id]);
}

void game_select_zone(Game* g, int zone) {
    if (!g || g->zone_count < 1) return;
    if (zone < 0) zone = 0;
    if (zone >= g->zone_count) zone = g->zone_count - 1;
    g->zone_id = zone;
    configure_channels(g, g->zone_pop[zone]);
}

void game_select_channel(Game* g, int chan) {
    if (!g || g->chan_count < 1) return;
    if (chan < 0) chan = 0;
    if (chan >= g->chan_count) chan = g->chan_count - 1;
    g->chan_id = chan;
}

int game_spawn_vehicle_type(Game* g, int team, int type) {
    if (!g || g->vehicle_count >= MAX_VEHICLES) return -1;
    if (type < 0 || type >= VEH_COUNT) type = g->vehicle_count % VEH_COUNT;
    const VehicleDef* vd = &vehicle_defs[type];
    int idx = g->vehicle_count++;
    Vehicle* v = &g->vehicles[idx];
    memset(v, 0, sizeof(*v));
    v->active = true;
    v->team = (team < 0) ? -1 : (team & 1);
    v->type = type;
    Vec3 b = g->team_base[team < 0 ? 0 : (team & 1)];
    /* Put vehicles between the team base and the map centre, then snap them
     * onto the walkable floor. Fixed +X offsets landed inside base walls on
     * some maps, which made parked vehicles unreachable for bots. */
    Vec3 centre; float extent;
    map_bounds(g->world, &centre, &extent);
    Vec3 dir = v3(centre.x - b.x, 0, centre.z - b.z);
    if (v3len(dir) < 0.1f) dir = v3(team == 0 ? 1.0f : -1.0f, 0, 0);
    dir = v3norm(dir);
    Vec3 side = v3(-dir.z, 0, dir.x);
    int rank = idx % VEH_COUNT;
    float fwd = 4.5f + (float)rank * 2.8f;
    float lat = ((float)rank - 1.0f) * 3.2f;
    v->home = v3add(v3add(b, v3mul(dir, fwd)), v3mul(side, lat));
    v->home.y = b.y + 0.05f;
    Ray down; down.origin = v3(v->home.x, b.y + 7.0f, v->home.z); down.dir = v3(0, -1, 0);
    RayHit fh;
    if (world_raycast(g->world, down, 14.0f, true, &fh) && fh.normal.y > 0.45f)
        v->home.y = fh.point.y + 0.05f;
    if (type == 1) v->home.y += 2.35f;
    v->pos = v->home;
    v->yaw = (team == 1) ? PI : 0.0f;
    v->pitch = 0;
    v->hp = v->max_hp = vd->max_hp;
    v->respawn = 0;
    v->driver = -1;
    v->killer = -1;
    for (int k = 0; k < VEH_MODULES; k++) {
        v->modules[k].max_hp = game_vehicle_module_max_hp(type, k);
        v->modules[k].hp = v->modules[k].max_hp;
        v->modules[k].destroyed = false;
    }
    return idx;
}

void game_spawn_vehicle(Game* g, int team) {
    game_spawn_vehicle_type(g, team, g ? (g->vehicle_count % VEH_COUNT) : 0);
}
static Vec3 player_eye(Player* p) { return v3(p->pos.x, p->pos.y + 1.52f - (p->crouch ? 0.45f : 0.0f), p->pos.z); }
static float player_height(Player* p) { return p->crouch ? 1.15f : 1.72f; }

static void dir_from_angles(float yaw, float pitch, Vec3* out) {
    float cp = cosf(pitch);
    *out = v3(cp * sinf(yaw), sinf(pitch), -cp * cosf(yaw));
}
static void angles_from_dir(Vec3 d, float* yaw, float* pitch) {
    d = v3norm(d);
    if (yaw) *yaw = atan2f(d.x, -d.z);
    if (pitch) *pitch = asinf(clampf(d.y, -1.0f, 1.0f));
}
static bool player_is_visible(Game* g, Player* from, Player* to, float* dist_out) {
    if (!to->active || !to->alive) return false;
    Vec3 a = player_eye(from), b = player_eye(to);
    Vec3 d = v3sub(b, a); float dist = v3len(d);
    if (dist < 0.001f) { if (dist_out) *dist_out = 0; return true; }
    Ray r; r.origin = a; r.dir = v3mul(d, 1.0f / dist);
    RayHit h;
    if (world_raycast(g->world, r, dist, true, &h)) {
        /* a hit close to target counts as blocked only if clearly in front */
        if (h.t < dist - 0.55f) return false;
    }
    if (dist_out) *dist_out = dist;
    return true;
}

/* ------------------------------------------------------------------ */
/* init / reset                                                        */
/* ------------------------------------------------------------------ */
void game_init(Game* g, World* w, bool demo, int bot_count) {
    memset(g, 0, sizeof(*g));
    g->world = w;
    g->demo_mode = demo;
    g->local_player = 0;
    g->frag_limit = 20;
    g->time_limit = 600.0f;
    g->difficulty = 2;
    g->mode = demo ? 1 : 0;
    g->start_countdown = 0.0f;
    g->ready = true;
    g->fov = 90.0f;
    g->gravity = 24.0f;
    g->game_mode = GM_DM;
    g->score_limit = (float)g->frag_limit;
    g->chan_cap = MAX_PLAYERS;

    int n = demo ? bot_count : (1 + bot_count);
    if (n < 1) n = 1;
    if (n > MAX_PLAYERS) n = MAX_PLAYERS;
    g->num_players = n;
    for (int i = 0; i < n; i++) init_player_slot(g, &g->players[i], i);
    if (demo) str_copy(g->players[0].name, "Xan", sizeof(g->players[0].name));
    g->br_total = n;
    assign_teams(g);
    game_configure_zones(g, n);
    game_generate_nature(g);
}


/* ------------------------------------------------------------------ */
/* procedural nature + ambient population                              */
/* ------------------------------------------------------------------ */
static float nature_ground_at(World* w, float x, float z, float from_y, bool* ok) {
    Ray r;
    r.origin = v3(x, from_y, z);
    r.dir = v3(0, -1, 0);
    RayHit h;
    if (world_raycast(w, r, 4096.0f, true, &h)) {
        if (ok) *ok = true;
        return h.point.y;
    }
    if (ok) *ok = false;
    return from_y;
}

static bool nature_point_free(Game* g, float x, float z, float min_dist) {
    float m2 = min_dist * min_dist;
    for (int i = 0; i < g->tree_count; i++) {
        float dx = g->trees[i].pos.x - x, dz = g->trees[i].pos.z - z;
        if (dx * dx + dz * dz < m2) return false;
    }
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_PLAYER_START && a->type != ACT_PICKUP && a->type != ACT_JUMPPAD) continue;
        float dx = a->pos.x - x, dz = a->pos.z - z;
        if (dx * dx + dz * dz < 4.0f) return false;
    }
    return true;
}

void game_generate_nature(Game* g) {
    if (!g || !g->world) return;
    g->tree_count = 0;
    g->critter_count = 0;
    g->nature_enabled = true;
    g->nature_time = 0.0f;
    Vec3 c;
    float ext = 48.0f;
    map_bounds(g->world, &c, &ext);
    if (ext < 24.0f) ext = 24.0f;
    float top = c.y + 320.0f;

    /* Trees, bushes and rocks. Three tries per slot keeps the scatter dense
     * even on maps whose playable floor only covers part of the bounds. */
    for (int attempt = 0; attempt < MAX_TREES * 4 && g->tree_count < MAX_TREES; attempt++) {
        float ang = frandr(0.0f, 6.28318f);
        int band = attempt % 6;
        float rad = band < 3 ? ext * frandr(0.62f, 1.45f) : ext * frandr(0.04f, 0.62f);
        float x = c.x + cosf(ang) * rad;
        float z = c.z + sinf(ang) * rad;
        bool ok = false;
        float gy = nature_ground_at(g->world, x, z, top, &ok);
        if (!ok) continue;
        if (!nature_point_free(g, x, z, 4.0f)) continue;
        NatureProp* t = &g->trees[g->tree_count++];
        memset(t, 0, sizeof(*t));
        t->pos = v3(x, gy, z);
        t->active = 1;
        t->phase = frandr(0.0f, 6.28318f);
        t->sway = frandr(0.20f, 1.10f);
        float r = frand();
        if (r < 0.46f) {
            t->kind = 0;
            t->height = frandr(4.4f, 8.8f);
            t->radius = frandr(1.3f, 2.5f);
        } else if (r < 0.76f) {
            t->kind = 1;
            t->height = frandr(3.6f, 7.2f);
            t->radius = frandr(1.6f, 3.1f);
        } else if (r < 0.88f) {
            t->kind = 2;
            t->height = frandr(3.0f, 6.5f);
            t->radius = frandr(0.7f, 1.5f);
        } else if (r < 0.945f) {
            t->kind = 3;
            t->height = frandr(0.7f, 1.8f);
            t->radius = frandr(0.9f, 2.1f);
        } else if (r < 0.99f) {
            t->kind = 4;
            t->height = frandr(0.5f, 1.6f);
            t->radius = frandr(0.7f, 2.0f);
        } else {
            t->kind = 5;   /* rusted scrap: the metal source */
            t->height = frandr(1.0f, 1.9f);
            t->radius = frandr(0.8f, 1.4f);
        }
    }

    /* Wildlife: birds circle overhead, ground animals roam near the ring. */
    static const int KINDS[5] = { 0, 0, 0, 1, 2 };
    for (int i = 0; i < MAX_CRITTERS; i++) {
        Critter* cr = &g->critters[i];
        memset(cr, 0, sizeof(*cr));
        float ang = frandr(0.0f, 6.28318f);
        float rad = ext * frandr(0.15f, 0.95f);
        float x = c.x + cosf(ang) * rad;
        float z = c.z + sinf(ang) * rad;
        bool ok = false;
        float gy = nature_ground_at(g->world, x, z, top, &ok);
        int kind;
        float q = frand();
        if (q < 0.28f) kind = 0;          /* wolf  */
        else if (q < 0.36f) kind = 1;     /* bear  */
        else if (q < 0.54f) kind = 2;     /* snake */
        else if (q < 0.74f) kind = 3;     /* fox   */
        else kind = 4;                    /* bird  */
        if (kind != 4 && !ok) continue;
        if (kind == 4) { gy = c.y + frandr(14.0f, 34.0f); ok = true; }
        if (!ok) continue;
        cr->pos = cr->home = v3(x, gy, z);
        cr->ground = gy;
        cr->kind = kind;
        cr->yaw = frandr(0.0f, 6.28318f);
        cr->speed = kind == 1 ? frandr(1.0f, 2.2f) : frandr(1.6f, 4.6f);
        cr->timer = frandr(0.5f, 4.0f);
        cr->phase = frandr(0.0f, 6.28318f);
        cr->active = 1;
        g->critter_count++;
        (void)KINDS;
    }

    /* GTA-style ambient population. 120 by default, up to 1000 on demand. */
    game_set_ambient(g, g->ambient_count > 0 ? g->ambient_count : 120);
}

int game_ambient_active(Game* g) {
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < g->ambient_count; i++) if (g->ambient[i].active) n++;
    return n;
}

int game_set_ambient(Game* g, int count) {
    if (!g) return 0;
    if (count < 0) count = 0;
    if (count > MAX_AMBIENT) count = MAX_AMBIENT;
    Vec3 c;
    float ext = 48.0f;
    map_bounds(g->world, &c, &ext);
    if (ext < 24.0f) ext = 24.0f;
    float top = c.y + 320.0f;
    for (int i = 0; i < g->ambient_count; i++) g->ambient[i].active = 0;
    g->ambient_count = count;
    g->ambient_capacity = count;
    for (int i = 0; i < count; i++) {
        AmbientNpc* n = &g->ambient[i];
        memset(n, 0, sizeof(*n));
        float x = c.x, z = c.z, gy = c.y;
        bool ok = false;
        for (int tries = 0; tries < 6 && !ok; tries++) {
            float ang = frandr(0.0f, 6.28318f);
            float rad = ext * frandr(0.05f, 0.92f);
            x = c.x + cosf(ang) * rad;
            z = c.z + sinf(ang) * rad;
            gy = nature_ground_at(g->world, x, z, top, &ok);
        }
        if (!ok) {
            /* fall back to a player start so the crowd is never empty */
            int starts = 0;
            for (int k = 0; k < g->world->count; k++)
                if (g->world->actors[k].type == ACT_PLAYER_START) starts++;
            if (starts > 0) {
                int pick = (i * 7) % starts;
                for (int k = 0; k < g->world->count; k++) {
                    if (g->world->actors[k].type != ACT_PLAYER_START) continue;
                    if (pick-- == 0) { x = g->world->actors[k].pos.x; z = g->world->actors[k].pos.z; gy = g->world->actors[k].pos.y; break; }
                }
            }
        }
        n->pos = n->home = v3(x, gy, z);
        n->yaw = frandr(0.0f, 6.28318f);
        n->timer = frandr(0.2f, 5.0f);
        n->phase = frandr(0.0f, 6.28318f);
        n->palette = (int)(frand() * 6.0f) % 6;
        n->active = 1;
    }
    return count;
}

static void nature_wander(Vec3* pos, Vec3* home, Vec3* vel, float* yaw, float* timer,
                          float* phase, float dt, float speed, float radius, bool airborne,
                          float base_y) {
    *phase += dt;
    *timer -= dt;
    if (*timer <= 0.0f) {
        *timer = frandr(1.2f, 5.5f);
        float a = frandr(0.0f, 6.28318f);
        *yaw = a;
        float s = speed * frandr(0.45f, 1.0f);
        *vel = v3(cosf(a) * s, 0, sinf(a) * s);
    }
    pos->x += vel->x * dt;
    pos->z += vel->z * dt;
    float dx = pos->x - home->x, dz = pos->z - home->z;
    float d2 = dx * dx + dz * dz;
    if (d2 > radius * radius) {
        /* turn back towards home and reflect the heading */
        float a = atan2f(home->z - pos->z, home->x - pos->x);
        *yaw = a;
        vel->x = cosf(a) * speed * 0.7f;
        vel->z = sinf(a) * speed * 0.7f;
    }
    if (airborne) pos->y = base_y + sinf(*phase * 0.9f) * 1.6f;
    else pos->y = base_y;
    home->y = base_y;
}

void game_update_nature(Game* g, float dt) {
    if (!g || !g->nature_enabled) return;
    g->nature_time += dt;
    for (int i = 0; i < g->critter_count; i++) {
        Critter* cr = &g->critters[i];
        if (!cr->active) continue;
        if (cr->kind == 4) {
            /* birds circle their home point */
            cr->phase += dt;
            float a = cr->phase * 0.55f + (float)i;
            float rad = 8.0f + (float)(i % 5) * 3.0f;
            cr->pos.x = cr->home.x + cosf(a) * rad;
            cr->pos.z = cr->home.z + sinf(a) * rad;
            cr->pos.y = cr->ground + sinf(cr->phase * 0.8f) * 1.8f;
            cr->yaw = a + 1.5708f;
        } else {
            float rad = cr->kind == 1 ? 14.0f : (cr->kind == 2 ? 4.0f : 18.0f);
            nature_wander(&cr->pos, &cr->home, &cr->vel, &cr->yaw, &cr->timer, &cr->phase,
                          dt, cr->speed, rad, false, cr->ground);
        }
    }
    for (int i = 0; i < g->ambient_count; i++) {
        AmbientNpc* n = &g->ambient[i];
        if (!n->active) continue;
        nature_wander(&n->pos, &n->home, &n->vel, &n->yaw, &n->timer, &n->phase,
                      dt, 1.5f, 34.0f, false, n->home.y);
    }
}

static void nature_draw_tree(Renderer* r, NatureProp* t, float sway) {
    float h = t->height, rad = t->radius;
    float bend = sinf(t->phase + sway) * 0.05f * t->sway;
    if (t->kind == 5) {
        /* metal scrap: a leaning rusted plate and a couple of pipes */
        Color rust = col(0.46f, 0.28f, 0.16f, 1);
        Color steel = col(0.42f, 0.45f, 0.48f, 1);
        renderer_draw_model(r, &r->cube,
            mat4_from_trs(v3(t->pos.x, t->pos.y + h * 0.34f, t->pos.z),
                          v3(0.35f, t->phase, 0.28f), v3(rad * 1.2f, h * 0.62f, rad * 0.18f)),
            rust, 0, 0, 0.7f, false);
        renderer_draw_model(r, &r->cylinder,
            mat4_from_trs(v3(t->pos.x + rad * 0.55f, t->pos.y + h * 0.20f, t->pos.z - rad * 0.25f),
                          v3(1.45f, t->phase, 0), v3(rad * 0.16f, h * 0.8f, rad * 0.16f)),
            steel, 0, 0, 0.8f, false);
        renderer_draw_model(r, &r->cube,
            mat4_from_trs(v3(t->pos.x - rad * 0.5f, t->pos.y + h * 0.16f, t->pos.z + rad * 0.3f),
                          v3(0, t->phase * 0.7f, -0.4f), v3(rad * 0.85f, h * 0.3f, rad * 0.7f)),
            steel, 0, 0, 0.7f, false);
        return;
    }
    if (t->kind == 4) {
        Color rock = col(0.37f, 0.36f, 0.34f, 1);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(t->pos.x, t->pos.y + h * 0.34f, t->pos.z),
                          v3(0, t->phase, 0), v3(rad, h * 0.8f, rad * 0.9f)),
            rock, 0, 0, 0.7f, false);
        return;
    }
    if (t->kind == 3) {
        Color leaf = col(0.20f, 0.42f, 0.16f, 1);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(t->pos.x, t->pos.y + h * 0.5f, t->pos.z),
                          v3(0, t->phase, 0), v3(rad, h, rad)),
            leaf, 2, 0, 0.6f, false);
        return;
    }
    Color bark = t->kind == 2 ? col(0.28f, 0.24f, 0.20f, 1) : col(0.30f, 0.21f, 0.13f, 1);
    float trunk_h = h * 0.46f;
    renderer_draw_model(r, &r->cylinder,
        mat4_from_trs(v3(t->pos.x, t->pos.y + trunk_h * 0.5f, t->pos.z),
                      v3(bend * 0.4f, t->phase, bend), v3(rad * 0.16f, trunk_h, rad * 0.16f)),
        bark, 0, 0, 0.7f, false);
    if (t->kind == 0) {
        Color a = col(0.13f, 0.34f, 0.14f, 1);
        Color b = col(0.18f, 0.44f, 0.18f, 1);
        renderer_draw_model(r, &r->cone,
            mat4_from_trs(v3(t->pos.x + bend, t->pos.y + trunk_h + h * 0.20f, t->pos.z),
                          v3(0, t->phase * 0.7f, 0), v3(rad, h * 0.52f, rad)),
            a, 2, 0, 0.9f, false);
        renderer_draw_model(r, &r->cone,
            mat4_from_trs(v3(t->pos.x + bend * 1.4f, t->pos.y + trunk_h + h * 0.42f, t->pos.z),
                          v3(0, t->phase, 0), v3(rad * 0.74f, h * 0.42f, rad * 0.74f)),
            b, 2, 0, 1.0f, false);
    } else if (t->kind == 1) {
        Color a = col(0.19f, 0.43f, 0.17f, 1);
        Color b = col(0.27f, 0.53f, 0.22f, 1);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(t->pos.x + bend, t->pos.y + trunk_h + h * 0.27f, t->pos.z),
                          v3(0, t->phase, 0), v3(rad * 1.15f, h * 0.42f, rad * 1.15f)),
            a, 2, 0, 0.9f, false);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(t->pos.x + bend * 1.8f + rad * 0.34f, t->pos.y + trunk_h + h * 0.42f, t->pos.z - rad * 0.20f),
                          v3(0, t->phase * 0.6f, 0), v3(rad * 0.78f, h * 0.30f, rad * 0.78f)),
            b, 2, 0, 1.1f, false);
    } else {
        /* dead tree: a few bare branches */
        Color wood = col(0.34f, 0.29f, 0.23f, 1);
        renderer_draw_model(r, &r->cylinder,
            mat4_from_trs(v3(t->pos.x + rad * 0.3f, t->pos.y + trunk_h + h * 0.10f, t->pos.z),
                          v3(0, 0, 0.9f), v3(rad * 0.07f, h * 0.40f, rad * 0.07f)),
            wood, 0, 0, 0.8f, false);
        renderer_draw_model(r, &r->cylinder,
            mat4_from_trs(v3(t->pos.x - rad * 0.3f, t->pos.y + trunk_h + h * 0.16f, t->pos.z),
                          v3(0.8f, 0, 0), v3(rad * 0.06f, h * 0.34f, rad * 0.06f)),
            wood, 0, 0, 0.8f, false);
    }
}

void game_render_nature(Game* g, Renderer* r) {
    if (!g || !g->nature_enabled) return;
    int li = g->local_player;
    Vec3 eye = (li >= 0 && li < g->num_players) ? g->players[li].pos : r->cam.pos;
    float sway = sinf(g->nature_time * 0.7f) * 0.6f;
    float far = 260.0f;
    for (int i = 0; i < g->tree_count; i++) {
        NatureProp* t = &g->trees[i];
        if (!t->active) continue;
        float dx = t->pos.x - eye.x, dz = t->pos.z - eye.z;
        if (dx * dx + dz * dz > far * far) continue;
        nature_draw_tree(r, t, sway);
    }
    for (int i = 0; i < g->critter_count; i++) {
        Critter* cr = &g->critters[i];
        if (!cr->active) continue;
        float dx = cr->pos.x - eye.x, dz = cr->pos.z - eye.z;
        if (dx * dx + dz * dz > 150.0f * 150.0f) continue;
        Color body;
        Vec3 sz;
        switch (cr->kind) {
        case 0: body = col(0.36f, 0.36f, 0.40f, 1); sz = v3(0.42f, 0.46f, 1.25f); break;
        case 1: body = col(0.29f, 0.21f, 0.14f, 1); sz = v3(0.95f, 1.05f, 1.85f); break;
        case 2: body = col(0.36f, 0.46f, 0.20f, 1); sz = v3(0.20f, 0.18f, 1.25f); break;
        case 3: body = col(0.78f, 0.36f, 0.13f, 1); sz = v3(0.36f, 0.38f, 0.95f); break;
        default: body = col(0.24f, 0.26f, 0.30f, 1); sz = v3(0.16f, 0.14f, 0.40f); break;
        }
        float yaw = cr->yaw;
        float baseY = cr->pos.y + sz.y * 0.5f;
        renderer_draw_model(r, &r->cube,
            mat4_from_trs(v3(cr->pos.x, baseY, cr->pos.z), v3(0, yaw, 0), sz),
            body, 0, 0, 0.6f, false);
        if (cr->kind != 4) {
            Vec3 fwd = v3(cosf(yaw), 0, sinf(yaw));
            Vec3 head = v3add(v3(cr->pos.x, baseY + sz.y * 0.30f, cr->pos.z), v3mul(fwd, sz.z * 0.48f));
            renderer_draw_model(r, &r->sphere,
                mat4_from_trs(head, v3(0, yaw, 0), v3(sz.x * 0.72f, sz.y * 0.62f, sz.z * 0.38f)),
                body, 0, 0, 0.6f, false);
        }
    }
    /* Ambient crowd. Draw nearest agents only so 1000 citizens stay cheap. */
    int budget = 240;
    for (int i = 0; i < g->ambient_count && budget > 0; i++) {
        AmbientNpc* n = &g->ambient[i];
        if (!n->active) continue;
        float dx = n->pos.x - eye.x, dz = n->pos.z - eye.z;
        if (dx * dx + dz * dz > 95.0f * 95.0f) continue;
        budget--;
        static const Color PAL[6] = {
            {0.55f,0.42f,0.30f,1}, {0.30f,0.42f,0.52f,1}, {0.46f,0.30f,0.32f,1},
            {0.32f,0.46f,0.34f,1}, {0.52f,0.48f,0.28f,1}, {0.38f,0.34f,0.46f,1}
        };
        Color c = PAL[n->palette % 6];
        float bob = sinf(n->phase * 3.1f) * 0.04f;
        renderer_draw_model(r, &r->cube,
            mat4_from_trs(v3(n->pos.x, n->pos.y + 0.92f + bob, n->pos.z), v3(0, n->yaw, 0),
                          v3(0.44f, 1.05f, 0.30f)),
            c, 0, 0, 0.7f, false);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(n->pos.x, n->pos.y + 1.66f + bob, n->pos.z), v3(0, n->yaw, 0),
                          v3(0.34f, 0.38f, 0.34f)),
            col(0.72f, 0.58f, 0.46f, 1), 0, 0, 0.6f, false);
    }
}
void game_shutdown(Game* g) { memset(g, 0, sizeof(*g)); }

/* ------------------------------------------------------------------ */
/* Fortnite-style building                                             */
/* ------------------------------------------------------------------ */
#define BUILD_COST 10
#define BUILD_PIECE_MAX 3

static float build_hp_for(int mat) {
    static const float hp[BUILD_MATS] = { 90.0f, 160.0f, 240.0f };
    if (mat < 0 || mat >= BUILD_MATS) mat = 0;
    return hp[mat];
}
static Color build_color_for(int mat) {
    static const Color c[BUILD_MATS] = {
        { 0.54f, 0.36f, 0.19f, 1.0f },   /* wood  */
        { 0.48f, 0.47f, 0.43f, 1.0f },   /* stone */
        { 0.43f, 0.52f, 0.63f, 1.0f }    /* metal */
    };
    if (mat < 0 || mat >= BUILD_MATS) mat = 0;
    return c[mat];
}
static int build_material_index(int mat) {
    if (mat == 0) return 7;   /* rust / timber tint */
    if (mat == 1) return 4;   /* concrete          */
    return 0;                 /* metal panels      */
}
const char* game_build_material_name(int mat) {
    static const char* n[BUILD_MATS] = { "HOLZ", "STEIN", "METALL" };
    if (mat < 0 || mat >= BUILD_MATS) mat = 0;
    return n[mat];
}
const char* game_build_piece_name(int type) {
    static const char* n[BUILD_PIECE_MAX] = { "WAND", "BODEN", "RAMPE" };
    if (type < 0 || type >= BUILD_PIECE_MAX) type = 0;
    return n[type];
}
int game_build_index_for_actor(Game* g, int actor) {
    if (!g || actor < 0) return -1;
    for (int i = 0; i < MAX_BUILDS; i++) {
        if (g->builds[i].active && g->builds[i].actor == actor) return i;
    }
    return -1;
}
void game_build_toggle(Player* p) {
    if (!p || !p->alive || p->in_vehicle) return;
    p->build_mode = !p->build_mode;
    p->build_yaw = p->yaw;
    p->build_cd = 0.03f;
}
void game_build_select(Player* p, int piece) {
    if (!p) return;
    if (piece < 0) piece = 0;
    if (piece >= BUILD_PIECE_MAX) piece = BUILD_PIECE_MAX - 1;
    p->build_sel = piece;
    p->build_cd = 0.02f;
}
void game_build_material(Player* p, int dir) {
    if (!p || dir == 0) return;
    p->build_mat = (p->build_mat + (dir > 0 ? 1 : BUILD_MATS - 1)) % BUILD_MATS;
    p->build_cd = 0.02f;
}
void game_build_rotate(Player* p) {
    if (!p) return;
    p->build_yaw += (float)PI * 0.5f;
    while (p->build_yaw >= (float)PI * 2.0f) p->build_yaw -= (float)PI * 2.0f;
    while (p->build_yaw < 0.0f) p->build_yaw += (float)PI * 2.0f;
    p->build_cd = 0.02f;
}
static float build_snap(float v, float step) { return floorf(v / step + 0.5f) * step; }

bool game_build_preview(Game* g, Player* p, Vec3* pos, Vec3* size, float* yaw, int* shape, bool* valid) {
    if (!g || !p || !pos || !size || !yaw || !shape || !valid) return false;
    *valid = false;
    float snap_rad = build_snap(p->build_yaw, (float)PI * 0.5f);
    Vec3 fwd = v3(sinf(snap_rad), 0, -cosf(snap_rad));
    Vec3 base = p->pos;

    Ray r; r.origin = player_eye(p);
    dir_from_angles(p->yaw, p->pitch, &r.dir);
    RayHit h;
    bool hit = world_raycast(g->world, r, 7.5f, true, &h);
    Vec3 target = hit ? h.point : v3add(r.origin, v3mul(r.dir, 4.0f));

    int piece = p->build_sel;
    if (piece == 0) {
        /* Wall: If the player looks at a vertical surface, extend that surface;
         * otherwise put a wall in front of the feet. */
        bool on_wall = hit && fabsf(h.normal.y) < 0.45f;
        float wyaw = snap_rad;
        Vec3 c;
        if (on_wall) {
            float ny = atan2f(h.normal.x, h.normal.z);
            wyaw = build_snap(ny, (float)PI * 0.5f);
            c = v3add(h.point, v3(h.normal.x * 0.22f, 0, h.normal.z * 0.22f));
        } else {
            c = v3add(base, v3mul(fwd, 2.6f));
        }
        c.x = build_snap(c.x, 2.0f);
        c.z = build_snap(c.z, 2.0f);
        bool ok = false;
        float gy = nature_ground_at(g->world, c.x, c.z, fmaxf(base.y, target.y) + 8.0f, &ok);
        if (!ok) gy = base.y;
        *pos = v3(c.x, gy + 2.02f, c.z);
        /* Wall boxes are axis-aligned in the collision/raycast path, so bake
         * the 90-degree orientation into the footprint instead of relying on
         * yaw (which would render rotated but collide as a 4m slab). */
        bool odd = fabsf(sinf(wyaw)) > 0.5f;
        *size = odd ? v3(0.35f, 4.0f, 4.0f) : v3(4.0f, 4.0f, 0.35f);
        *yaw = 0.0f;
        *shape = SHAPE_BOX;
    } else if (piece == 1) {
        Vec3 c = target;
        c.x = build_snap(c.x, 2.0f);
        c.z = build_snap(c.z, 2.0f);
        bool ok = false;
        float gy = nature_ground_at(g->world, c.x, c.z, fmaxf(base.y, target.y) + 8.0f, &ok);
        if (!ok || fabsf(gy - target.y) > 3.5f) gy = base.y;
        *pos = v3(c.x, gy + 0.195f, c.z);
        *size = v3(4.0f, 0.35f, 4.0f);
        *yaw = snap_rad;
        *shape = SHAPE_BOX;
    } else {
        Vec3 c = v3add(base, v3mul(fwd, 3.2f));
        c.x = build_snap(c.x, 2.0f);
        c.z = build_snap(c.z, 2.0f);
        bool ok = false;
        float gy = nature_ground_at(g->world, c.x, c.z, base.y + 10.0f, &ok);
        if (!ok) gy = base.y;
        *pos = v3(c.x, gy + 2.02f, c.z);
        *size = v3(4.0f, 4.0f, 4.0f);
        *yaw = snap_rad;
        *shape = SHAPE_RAMP;
    }

    if (p->mats[p->build_mat] < BUILD_COST) return true;
    for (int i = 0; i < MAX_BUILDS; i++) {
        BuildPiece* b = &g->builds[i];
        if (!b->active) continue;
        AABB ob = aabb_center_size(b->pos, b->size);
        AABB nb = aabb_center_size(*pos, *size);
        ob.min = v3add(ob.min, v3s(0.06f)); ob.max = v3sub(ob.max, v3s(0.06f));
        nb.min = v3add(nb.min, v3s(0.06f)); nb.max = v3sub(nb.max, v3s(0.06f));
        if (aabb_overlap(ob, nb)) return true;
    }
    AABB nb = aabb_center_size(*pos, *size);
    nb.min = v3add(nb.min, v3s(0.06f));
    nb.max = v3sub(nb.max, v3s(0.06f));
    if (world_overlap_box(g->world, nb, -1)) return true;
    /* Never let a wall/floor spawn inside another living player: that is the
     * classic "stuck in a box" bug. The builder may build under their own feet
     * (floors are useful), but opponents/allies must not be entombed. */
    for (int i = 0; i < g->num_players; i++) {
        Player* o = &g->players[i];
        if (o == p || !o->active || !o->alive || o->zombie) continue;
        AABB pb = aabb_center_size(v3(o->pos.x, o->pos.y + 0.9f, o->pos.z), v3(1.0f, 1.8f, 1.0f));
        if (aabb_overlap(nb, pb)) return true;
    }
    *valid = true;
    return true;
}

bool game_build_attempt(Game* g, Player* p) {
    if (!g || !p || !p->alive || p->in_vehicle || p->build_cd > 0.0f) return false;
    Vec3 pos, size; float yaw; int shape; bool valid;
    if (!game_build_preview(g, p, &pos, &size, &yaw, &shape, &valid) || !valid) {
        p->build_cd = 0.05f;
        return false;
    }
    if (p->mats[p->build_mat] < BUILD_COST) {
        if (p->id == g->local_player) {
            game_pickup_msg(g, "Nicht genug %s - sammle mit E", game_build_material_name(p->build_mat));
            audio_play(SFX_UI_CLICK, 0.25f, 0.0f);
        }
        p->build_cd = 0.05f;
        return false;
    }
    int slot = -1;
    for (int i = 0; i < MAX_BUILDS; i++) if (!g->builds[i].active) { slot = i; break; }
    if (slot < 0) {
        if (p->id == g->local_player) game_pickup_msg(g, "Bau-Limit erreicht");
        p->build_cd = 0.05f;
        return false;
    }
    Actor a;
    world_actor_defaults(&a, ACT_BRUSH, 0);
    a.pos = pos;
    a.size = size;
    a.yaw = yaw * RAD2DEG;
    a.shape = shape;
    a.flags = FLAG_SOLID;
    a.material = build_material_index(p->build_mat);
    a.color = build_color_for(p->build_mat);
    snprintf(a.name, MAX_NAME, "%s %s", game_build_piece_name(p->build_sel), game_build_material_name(p->build_mat));
    int actor = world_add(g->world, a);
    if (actor < 0) { p->build_cd = 0.3f; return false; }

    BuildPiece* b = &g->builds[slot];
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->actor = actor;
    b->type = p->build_sel;
    b->material = p->build_mat;
    b->owner = p->id;
    b->max_hp = build_hp_for(p->build_mat);
    b->hp = b->max_hp;
    b->build_t = 0.18f;
    b->pos = pos;
    b->size = size;
    b->yaw = yaw * RAD2DEG;
    p->mats[p->build_mat] -= BUILD_COST;
    p->build_cd = 0.03f;
    p->fire_timer = 0.06f;

    for (int i = 0; i < 10; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.2f, 1), frandr(-1, 1)));
        game_add_particle(g, v3(pos.x, pos.y, pos.z), v3mul(d, frandr(1.0f, 3.2f)),
                          build_color_for(p->build_mat), frandr(0.08f, 0.2f), frandr(0.2f, 0.45f), true, 5.0f, 1.2f);
    }
    if (p->id == g->local_player) {
        audio_play(SFX_PICKUP_AMMO, 0.35f, 0.0f);
        char msg[96];
        snprintf(msg, sizeof(msg), "%s aus %s gebaut  (%d)", game_build_piece_name(b->type),
                 game_build_material_name(b->material), p->mats[p->build_mat]);
        game_pickup_msg(g, "%s", msg);
    }
    return true;
}

void game_build_damage(Game* g, int piece, float dmg, int attacker, Vec3 point) {
    (void)attacker;
    if (!g || piece < 0 || piece >= MAX_BUILDS || dmg <= 0.0f) return;
    BuildPiece* b = &g->builds[piece];
    if (!b->active) return;
    b->hp -= dmg;
    b->damage_flash = 0.35f;
    int n = dmg > 30.0f ? 9 : 4;
    for (int i = 0; i < n; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.3f, 1), frandr(-1, 1)));
        game_add_particle(g, point, v3mul(d, frandr(1.0f, 4.0f)), build_color_for(b->material),
                          frandr(0.06f, 0.16f), frandr(0.18f, 0.4f), true, 5.0f, 1.0f);
    }
    if (b->hp > 0.0f) return;
    int actor = b->actor;
    if (actor >= 0 && actor < g->world->count) world_delete(g->world, actor);
    for (int i = 0; i < MAX_BUILDS; i++) {
        if (g->builds[i].active && g->builds[i].actor > actor) g->builds[i].actor--;
    }
    b->active = false;
    for (int i = 0; i < 18; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(0.0f, 1.2f), frandr(-1, 1)));
        game_add_particle(g, point, v3mul(d, frandr(2.0f, 7.0f)), build_color_for(b->material),
                          frandr(0.12f, 0.3f), frandr(0.35f, 0.8f), true, 6.0f, 1.4f);
    }
    if (g->local_player >= 0 && g->local_player < g->num_players) {
        Player* lp = &g->players[g->local_player];
        audio_play_at(SFX_IMPACT, 0.7f, point.x, point.y, point.z,
                      lp->pos.x, lp->pos.y, lp->pos.z,
                      sinf(lp->yaw), -cosf(lp->yaw));
    }
}

void game_build_clear(Game* g) {
    if (!g) return;
    for (;;) {
        int best = -1, best_actor = -1;
        for (int i = 0; i < MAX_BUILDS; i++) {
            if (g->builds[i].active && g->builds[i].actor > best_actor) {
                best_actor = g->builds[i].actor;
                best = i;
            }
        }
        if (best < 0) break;
        int actor = g->builds[best].actor;
        if (actor >= 0 && actor < g->world->count) world_delete(g->world, actor);
        for (int i = 0; i < MAX_BUILDS; i++)
            if (g->builds[i].active && g->builds[i].actor > actor) g->builds[i].actor--;
        g->builds[best].active = false;
    }
    memset(g->builds, 0, sizeof(g->builds));
    g->build_count = 0;
}

void game_build_update(Game* g, float dt) {
    if (!g) return;
    for (int i = 0; i < MAX_BUILDS; i++) {
        BuildPiece* b = &g->builds[i];
        if (!b->active) continue;
        if (b->build_t > 0.0f) b->build_t = fmaxf(0.0f, b->build_t - dt);
        if (b->damage_flash > 0.0f) b->damage_flash = fmaxf(0.0f, b->damage_flash - dt);
    }
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->build_cd > 0.0f) p->build_cd -= dt;
        if (p->harvest_cd > 0.0f) p->harvest_cd -= dt;
    }
    int count = 0;
    for (int i = 0; i < MAX_BUILDS; i++) if (g->builds[i].active) count++;
    g->build_count = count;
}

bool game_harvest(Game* g, Player* p) {
    if (!g || !p || !p->alive || p->harvest_cd > 0.0f) return false;
    int best = -1;
    float bestd = 4.2f * 4.2f;
    for (int i = 0; i < g->tree_count; i++) {
        NatureProp* t = &g->trees[i];
        if (!t->active) continue;
        float dx = t->pos.x - p->pos.x, dz = t->pos.z - p->pos.z;
        float dy = t->pos.y - p->pos.y;
        if (dy < -1.5f || dy > t->height + 2.0f) continue;
        float d = dx * dx + dz * dz;
        if (d < bestd) { bestd = d; best = i; }
    }
    if (best < 0) return false;
    NatureProp* t = &g->trees[best];
    int mat = 0, amount = 0;
    if (t->kind == 4) { mat = 1; amount = 16 + (int)(frand() * 10.0f); }
    else if (t->kind == 5) { mat = 2; amount = 14 + (int)(frand() * 9.0f); }
    else if (t->kind == 3) { mat = 0; amount = 7 + (int)(frand() * 5.0f); }
    else { mat = 0; amount = 15 + (int)(frand() * 10.0f); }
    p->mats[mat] += amount;
    p->harvest_cd = 0.32f;
    p->harvest_target = best;
    t->hp -= 34.0f;
    Vec3 fx = v3(t->pos.x, t->pos.y + fminf(t->height * 0.6f, 2.4f), t->pos.z);
    for (int i = 0; i < 10; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.2f, 1), frandr(-1, 1)));
        game_add_particle(g, fx, v3mul(d, frandr(1.0f, 4.0f)), build_color_for(mat),
                          frandr(0.07f, 0.18f), frandr(0.2f, 0.5f), true, 4.0f, 1.2f);
    }
    if (t->hp <= 0.0f) {
        t->active = 0;
        for (int i = 0; i < 22; i++) {
            Vec3 d = v3norm(v3(frandr(-1, 1), frandr(0.0f, 1.3f), frandr(-1, 1)));
            game_add_particle(g, fx, v3mul(d, frandr(2.0f, 6.0f)), build_color_for(mat),
                              frandr(0.12f, 0.3f), frandr(0.4f, 1.0f), true, 6.0f, 1.3f);
        }
    }
    if (p->id == g->local_player) {
        game_pickup_msg(g, "+%d %s  (%d)", amount, game_build_material_name(mat), p->mats[mat]);
        audio_play(SFX_PICKUP_AMMO, 0.35f, 0.0f);
    }
    return true;
}

void game_render_builds(Game* g, Renderer* r) {
    if (!g || !r) return;
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) return;
    Player* p = &g->players[li];
    if (!p->alive || !p->build_mode) return;
    Vec3 pos, size; float yaw; int shape; bool valid;
    if (!game_build_preview(g, p, &pos, &size, &yaw, &shape, &valid)) return;
    Color c = valid ? col(0.30f, 1.0f, 0.45f, 0.95f) : col(1.0f, 0.25f, 0.20f, 0.95f);
    lines_begin(r);
    AABB b = aabb_center_size(pos, size);
    line_box(r, b, c);
    /* diagonal hints make a ramp read as a ramp, not an empty box */
    if (shape == SHAPE_RAMP) {
        Vec3 f = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 lo = v3(pos.x - f.x * size.z * 0.5f, pos.y - size.y * 0.5f, pos.z - f.z * size.z * 0.5f);
        Vec3 hi = v3(pos.x + f.x * size.z * 0.5f, pos.y + size.y * 0.5f, pos.z + f.z * size.z * 0.5f);
        line_add(r, v3(lo.x - size.x * 0.5f, lo.y, lo.z), v3(hi.x - size.x * 0.5f, hi.y, hi.z), c);
        line_add(r, v3(lo.x + size.x * 0.5f, lo.y, lo.z), v3(hi.x + size.x * 0.5f, hi.y, hi.z), c);
        line_add(r, v3(lo.x - size.x * 0.5f, lo.y, lo.z), v3(lo.x + size.x * 0.5f, lo.y, lo.z), c);
        line_add(r, v3(hi.x - size.x * 0.5f, hi.y, hi.z), v3(hi.x + size.x * 0.5f, hi.y, hi.z), c);
    }
    lines_render(r, true, 2.0f);
}

/* Battle Royale storm wall: a visible ring so players can read the shrinking
 * zone instead of only seeing the damage vignette. */
void game_render_zone(Game* g, Renderer* r) {
    if (!g || !r) return;
    if (g->game_mode != GM_BATTLE_ROYALE || g->zone_radius <= 2.0f) return;
    Vec3 c;
    float e;
    map_bounds(g->world, &c, &e);
    float y0 = c.y + 0.05f;
    float y1 = c.y + 46.0f;
    Color wall = col(0.58f, 0.34f, 1.0f, 0.85f);
    Color ring = col(0.72f, 0.52f, 1.0f, 0.95f);
    lines_begin(r);
    const int SEG = 72;
    Vec3 prev = v3(g->zone_center_x + g->zone_radius, y0, g->zone_center_z);
    for (int i = 1; i <= SEG; i++) {
        float a = (float)i * 6.28318f / (float)SEG;
        Vec3 p = v3(g->zone_center_x + cosf(a) * g->zone_radius, y0, g->zone_center_z + sinf(a) * g->zone_radius);
        line_add(r, prev, p, ring);
        if ((i % 2) == 0)
            line_add(r, v3(p.x, y0, p.z), v3(p.x, y1, p.z), wall);
        prev = p;
    }
    lines_render(r, true, 1.6f);
}

/* ------------------------------------------------------------------ */
/* Altis Life - lightweight Arma 3 style roleplay layer               */
/* ------------------------------------------------------------------ */
const char* game_life_point_name(int type) {
    switch (type) {
    case LIFE_JOB: return "JOB CENTER";
    case LIFE_DELIVERY: return "DELIVERY DEPOT";
    case LIFE_MINE: return "ORE MINE";
    case LIFE_SHOP: return "24/7 SHOP";
    case LIFE_ATM: return "BANK ATM";
    case LIFE_HOSPITAL: return "HOSPITAL";
    default: return "POINT";
    }
}
static Color life_point_color(int type) {
    switch (type) {
    case LIFE_JOB: return col3(1.0f, 0.82f, 0.22f);
    case LIFE_DELIVERY: return col3(1.0f, 0.48f, 0.14f);
    case LIFE_MINE: return col3(0.62f, 0.55f, 0.45f);
    case LIFE_SHOP: return col3(0.28f, 0.92f, 0.48f);
    case LIFE_ATM: return col3(0.25f, 0.62f, 1.0f);
    case LIFE_HOSPITAL: return col3(1.0f, 0.28f, 0.30f);
    }
    return col3(0.8f, 0.8f, 0.8f);
}
static Vec3 life_ground_spot(Game* g, float x, float z) {
    Vec3 c; float e;
    map_bounds(g->world, &c, &e);
    bool ok = false;
    float gy = nature_ground_at(g->world, x, z, c.y + 400.0f, &ok);
    if (!ok) gy = c.y + 0.2f;
    Vec3 p = v3(x, gy + 0.06f, z);
    if (!spawn_clear(g->world, p, 0.95f)) p = spawn_safe_pos(g->world, p);
    return p;
}
void game_life_setup(Game* g) {
    if (!g) return;
    g->life_point_count = 0;
    g->life_near = -1;
    if (g->game_mode != GM_ALTIS) return;
    Vec3 c; float e;
    map_bounds(g->world, &c, &e);
    if (e < 22.0f) e = 22.0f;
    struct { int type; float x, z; } defs[] = {
        { LIFE_JOB,      -0.30f, -0.20f },
        { LIFE_DELIVERY,  0.34f,  0.16f },
        { LIFE_MINE,      0.40f, -0.38f },
        { LIFE_SHOP,     -0.34f,  0.30f },
        { LIFE_ATM,       0.16f,  0.36f },
        { LIFE_HOSPITAL, -0.10f, -0.38f },
        { LIFE_SHOP,      0.42f,  0.34f },
        { LIFE_ATM,      -0.40f, -0.02f },
        { LIFE_JOB,       0.04f,  0.42f },
        { LIFE_HOSPITAL,  0.42f, -0.12f }
    };
    int n = (int)(sizeof(defs) / sizeof(defs[0]));
    static const char* ARMA_NAMES[] = {
        "KAVALA JOB CENTER", "ATHIRA DELIVERY", "PYRGOS MINE", "SOFIA 24/7",
        "KAVALA BANK ATM", "KAVALA HOSPITAL", "NEOCHORI 24/7", "ATHIRA BANK ATM",
        "PYRGOS JOB CENTER", "PYRGOS HOSPITAL"
    };
    for (int i = 0; i < n && g->life_point_count < MAX_LIFE_POINTS; i++) {
        LifePoint* lp = &g->life_points[g->life_point_count];
        memset(lp, 0, sizeof(*lp));
        lp->type = defs[i].type;
        lp->pos = life_ground_spot(g, c.x + e * defs[i].x, c.z + e * defs[i].z);
        lp->radius = 3.0f;
        snprintf(lp->name, sizeof(lp->name), "%s", ARMA_NAMES[i]);
        g->life_point_count++;
    }
}
int game_life_nearest(Game* g, Player* p, float maxdist) {
    if (!g || !p || g->game_mode != GM_ALTIS) return -1;
    int best = -1;
    float bd = maxdist * maxdist;
    for (int i = 0; i < g->life_point_count; i++) {
        LifePoint* lp = &g->life_points[i];
        float dx = lp->pos.x - p->pos.x, dz = lp->pos.z - p->pos.z;
        float dy = fabsf(lp->pos.y - p->pos.y);
        if (dy > 5.0f) continue;
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static void life_msg(Game* g, Player* p, const char* fmt, ...) {
    if (!p || p->id != g->local_player) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->life_msg, sizeof(p->life_msg), fmt, ap);
    va_end(ap);
    p->life_msg_t = 4.0f;
    game_pickup_msg(g, "%s", p->life_msg);
}
static void life_reward(Game* g, Player* p, int cash, const char* what) {
    if (cash == 0) return;
    p->cash += cash;
    if (p->cash < 0) p->cash = 0;
    if (p->id == g->local_player) {
        game_add_killfeed(g, "%s: %+d $", what, cash);
        audio_play(SFX_PICKUP_AMMO, 0.4f, 0.0f);
    }
}
bool game_life_interact(Game* g, Player* p) {
    if (!g || !p || !p->alive || g->game_mode != GM_ALTIS) return false;
    int pi = game_life_nearest(g, p, 3.6f);
    if (pi < 0) return false;
    LifePoint* lp = &g->life_points[pi];
    if (p->life_cd > 0.0f) return false;
    switch (lp->type) {
    case LIFE_JOB:
        if (p->life_job == 1) {
            life_msg(g, p, "Du hast bereits einen Lieferauftrag");
        } else {
            p->life_job = 1;
            p->life_stage = 0;
            p->life_cd = 0.4f;
            life_msg(g, p, "AUFTRAG: Fracht zum DELIVERY DEPOT bringen");
            if (p->id == g->local_player) game_announce(g, "DELIVERY CONTRACT", col3(0.35f, 0.95f, 0.55f), 2.0f);
        }
        break;
    case LIFE_DELIVERY:
        if (p->life_job == 1 && p->life_stage == 0) {
            int pay = 260 + (int)(frand() * 180.0f);
            p->life_job = 0;
            p->life_stage = 0;
            p->life_cd = 0.5f;
            life_reward(g, p, pay, "DELIVERY");
            life_msg(g, p, "Fracht geliefert: +%d $", pay);
            if (p->id == g->local_player) game_announce(g, "DELIVERY COMPLETE", col3(0.35f, 0.95f, 0.55f), 2.0f);
        } else {
            life_msg(g, p, "Kein Auftrag - hole einen Vertrag im JOB CENTER");
        }
        break;
    case LIFE_MINE:
        if (p->life_cd <= 0.0f) {
            int add = 1 + (int)(frand() * 2.0f);
            p->ore += add;
            p->life_cd = 0.55f;
            life_msg(g, p, "+%d Erz  (%d im Rucksack)", add, p->ore);
            if (p->id == g->local_player) audio_play(SFX_PICKUP_AMMO, 0.3f, 0.0f);
        }
        break;
    case LIFE_SHOP:
        if (p->ore > 0) {
            int pay = p->ore * 55;
            p->ore = 0;
            p->life_cd = 0.4f;
            life_reward(g, p, pay, "ORE SOLD");
            life_msg(g, p, "Erz verkauft: +%d $", pay);
        } else if (p->cash >= 120) {
            p->cash -= 120;
            p->mats[0] += 30;
            p->mats[1] += 20;
            p->mats[2] += 10;
            p->life_cd = 0.4f;
            life_msg(g, p, "Baumaterial gekauft: +30 Holz +20 Stein +10 Metall");
        } else {
            life_msg(g, p, "Nichts zu verkaufen - Erz im MINE abbauen");
        }
        break;
    case LIFE_ATM:
        if (p->cash > 0) {
            int dep = p->cash;
            p->bank += dep;
            p->cash = 0;
            p->life_cd = 0.4f;
            life_msg(g, p, "%d $ eingezahlt  (Bank: %d $)", dep, p->bank);
        } else if (p->bank > 0) {
            int take = p->bank < 500 ? p->bank : 500;
            p->bank -= take;
            p->cash += take;
            p->life_cd = 0.4f;
            life_msg(g, p, "%d $ abgehoben  (Bank: %d $)", take, p->bank);
        } else {
            life_msg(g, p, "Konto leer");
        }
        break;
    case LIFE_HOSPITAL:
        if (p->wanted > 0) {
            /* Arma-style consequence: the hospital will patch you up and clear
             * your record, but only if you can pay the police fine. */
            int fine = p->wanted * 120;
            if (p->cash >= fine) {
                p->cash -= fine;
                p->wanted = 0;
                p->life_cd = 0.5f;
                life_msg(g, p, "Krankenhaus: Strafe %d $ bezahlt, Akte bereinigt", fine);
            } else {
                life_msg(g, p, "Strafe %d $ offen - Wanted bleibt (Bargeld: %d $)", fine, p->cash);
            }
        } else if (p->health < game_player_max_health(p)) {
            int cost = 100;
            if (p->cash < cost) {
                life_msg(g, p, "Behandlung kostet %d $ - zu wenig Bargeld", cost);
            } else {
                p->cash -= cost;
                p->health = game_player_max_health(p);
                p->armor = fmaxf(p->armor, 25.0f);
                p->life_cd = 0.5f;
                life_msg(g, p, "Behandelt: -%d $", cost);
            }
        } else {
            life_msg(g, p, "Du bist gesund");
        }
        break;
    }
    return true;
}
void game_life_update(Game* g, float dt) {
    if (!g) return;
    if (g->game_mode != GM_ALTIS) { g->life_near = -1; return; }
    g->life_tick += dt;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->life_cd > 0.0f) p->life_cd -= dt;
        if (p->life_msg_t > 0.0f) p->life_msg_t -= dt;
    }
    int li = g->local_player;
    if (li >= 0 && li < g->num_players)
        g->life_near = game_life_nearest(g, &g->players[li], 3.6f);
}
void game_render_life(Game* g, Renderer* r) {
    if (!g || !r || g->game_mode != GM_ALTIS) return;
    for (int i = 0; i < g->life_point_count; i++) {
        LifePoint* lp = &g->life_points[i];
        Color c = life_point_color(lp->type);
        float pulse = 0.85f + 0.15f * sinf(g->time * 2.0f + (float)i);
        renderer_draw_model(r, &r->cylinder,
            mat4_from_trs(v3(lp->pos.x, lp->pos.y + 0.06f, lp->pos.z), v3(0, g->time * 0.6f, 0),
                          v3(1.7f, 0.12f, 1.7f)), c, 5, 1.4f, 1.0f, false);
        renderer_draw_model(r, &r->cylinder,
            mat4_from_trs(v3(lp->pos.x, lp->pos.y + 3.2f, lp->pos.z), v3(0, 0, 0),
                          v3(0.10f, 3.2f, 0.10f)), c, 5, 1.2f * pulse, 1.0f, false);
        renderer_draw_model(r, &r->sphere,
            mat4_from_trs(v3(lp->pos.x, lp->pos.y + 6.7f + sinf(g->time * 2.2f + i) * 0.22f, lp->pos.z),
                          v3(0, g->time, 0), v3(0.75f, 0.75f, 0.75f)), c, 5, 1.8f, 1.0f, false);
    }
}

static int pick_spawn(Game* g, Player* p) {
    int best = -1; float best_score = -1e9f;
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_PLAYER_START) continue;
        float score = frandr(0, 6);
        /* Never pick an authored start that is buried in a brush or squeezed
         * into a decorative pocket; those are the "spawned in a barrel" cases. */
        Vec3 feet = v3add(a->pos, v3(0, 0.15f, 0));
        if (spawn_clear(g->world, feet, 1.15f)) score += 500.0f;
        else if (spawn_pos_free(g->world, feet) && spawn_has_floor(g->world, feet)) score += 20.0f;
        else score -= 500.0f;
        for (int j = 0; j < g->num_players; j++) {
            Player* o = &g->players[j];
            if (o == p || !o->active || !o->alive) continue;
            float d = v3len(v3sub(o->pos, a->pos));
            score += d * 2.2f;
            /* Never stack two players on the same start: the clearance bonus
             * above would otherwise outweigh the proximity penalty. */
            if (d < 3.0f) score -= 5000.0f;
            else if (d < 8.0f) score -= 120.0f;
        }
        if (p->team >= 0) {
            Vec3 own = g->team_base[p->team], foe = g->team_base[1 - p->team];
            float do2 = (a->pos.x - own.x) * (a->pos.x - own.x) + (a->pos.z - own.z) * (a->pos.z - own.z);
            float df2 = (a->pos.x - foe.x) * (a->pos.x - foe.x) + (a->pos.z - foe.z) * (a->pos.z - foe.z);
            if (do2 < df2) score += 55.0f; else score -= 55.0f;
        }
        if (score > best_score) { best_score = score; best = i; }
    }
    return best;
}

void game_respawn(Game* g, Player* p) {
    if (g->game_mode == GM_BATTLE_ROYALE && g->match_time > 0.2f) {
        p->alive = false;
        p->respawn_timer = 1e9f;
        return;
    }
    int altis_hospital = -1;
    if (g->game_mode == GM_ALTIS) {
        for (int i = 0; i < g->life_point_count; i++) {
            if (g->life_points[i].type == LIFE_HOSPITAL) { altis_hospital = i; break; }
        }
    }
    if (altis_hospital >= 0) {
        LifePoint* lp = &g->life_points[altis_hospital];
        p->pos = spawn_safe_pos(g->world, v3add(lp->pos, v3(frandr(-2.0f, 2.0f), 0.6f, frandr(-2.0f, 2.0f))));
        p->yaw = atan2f(lp->pos.x - p->pos.x, -(lp->pos.z - p->pos.z));
    } else {
        int si = pick_spawn(g, p);
        if (si >= 0) {
            Actor* a = &g->world->actors[si];
            p->pos = spawn_safe_pos(g->world, v3add(a->pos, v3(0, 0.15f, 0)));
            p->yaw = a->yaw * DEG2RAD;
            if (getenv("A2K4_CAMDBG"))
                printf("[spawn] player=%d start=%s authored=(%.2f,%.2f,%.2f) -> pos=(%.2f,%.2f,%.2f) yaw=%.0f\n",
                       p->id, a->name, a->pos.x, a->pos.y, a->pos.z,
                       p->pos.x, p->pos.y, p->pos.z, p->yaw * RAD2DEG);
        } else {
            p->pos = v3(0, 0.5f, 20);
            p->yaw = 0;
        }
    }
    p->vel = v3(0, 0, 0);
    p->pitch = 0;
    p->health = game_player_max_health(p);
    p->armor = 0;
    p->alive = true;
    p->in_vehicle = false;
    p->vehicle = -1;
    p->death_time = 0;
    p->respawn_timer = 0;
    p->protection = 2.0f;
    p->weapon = WPN_AR;
    p->has[WPN_AR] = true;
    p->ammo[WPN_AR] = 60;
    p->mag[WPN_AR] = game_player_mag_cap(WPN_AR);
    p->reload_timer = 0; p->reload_total = 0; p->reload_weapon = -1;
    p->fire_timer = 0; p->alt_timer = 0;
    p->jump_count = 0;
    p->crouch = false;
    /* A dead builder must not respawn still holding a ghost blueprint. */
    p->build_mode = false;
    p->build_cd = 0.0f;
    p->grounded = false;
    p->bob = 0; p->land_kick = 0; p->recoil = 0;
    p->render_pos = p->pos;
    p->render_yaw = p->yaw;
    p->render_pitch = p->pitch;
    p->bot_path_len = 0; p->bot_path_idx = 0; p->bot_target = -1; p->bot_repath = 0;
    p->bot_vehicle_target = -1; p->bot_vehicle_t = frandr(0.2f, 1.2f);
    p->use_latch = false;
    /* Fortnite-style loadout: spawn with a starter stack of wood so the first
     * wall can go up immediately; stone/metal must be harvested. */
    if (g->game_mode != GM_ALTIS && p->mats[0] <= 0 && p->mats[1] <= 0 && p->mats[2] <= 0)
        p->mats[0] = 60;
    if (g->local_player == p->id) g->damage_flash = 0;
    if (g->local_player == p->id) audio_play(SFX_RESPAWN, 0.7f, 0);
}

static void finish_match_team(Game* g, int team, const char* reason) {
    if (g->match_over) return;
    g->match_over = true;
    int best = -1;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->team != team) continue;
        if (best < 0 || p->frags > g->players[best].frags) best = i;
    }
    g->winner = best;
    snprintf(g->result, sizeof(g->result), "%s TEAM WINS - %s", team == 0 ? "RED" : "BLUE", reason ? reason : "score limit");
    game_announce(g, g->result, team_base_color(team), 5.0f);
}

static void setup_objectives(Game* g) {
    find_bases(g, &g->team_base[0], &g->team_base[1]);
    g->team_score[0] = g->team_score[1] = 0;
    g->team_kills[0] = g->team_kills[1] = 0;
    g->node_tick = 0;
    g->node_count = 0;
    g->vehicle_count = 0;
    g->flags[0].active = g->flags[1].active = false;
    g->result[0] = 0;
    Vec3 a = g->team_base[0], b = g->team_base[1];
    Vec3 mid = v3lerp(a, b, 0.5f);

    if (g->game_mode == GM_CTF || g->game_mode == GM_VCTF) {
        for (int t = 0; t < 2; t++) {
            Flag* f = &g->flags[t];
            memset(f, 0, sizeof(*f));
            f->active = true;
            f->team = t;
            f->home = v3(g->team_base[t].x, g->team_base[t].y + 1.1f, g->team_base[t].z);
            f->pos = f->home;
            f->carrier = -1;
        }
    }
    if (g->game_mode == GM_ASSAULT) {
        g->node_count = 2;
        for (int t = 0; t < 2; t++) {
            ControlNode* n = &g->nodes[t];
            memset(n, 0, sizeof(*n));
            n->active = true;
            n->team = t;
            n->pos = v3(g->team_base[t].x, g->team_base[t].y + 1.0f, g->team_base[t].z);
            n->radius = 5.5f;
            n->progress = 0;
            snprintf(n->name, sizeof(n->name), "%s CORE", t == 0 ? "RED" : "BLUE");
        }
    } else if (g->game_mode == GM_ONSLAUGHT) {
        g->node_count = 3;
        Vec3 pts[3] = { a, mid, b };
        for (int i = 0; i < 3; i++) {
            ControlNode* n = &g->nodes[i];
            memset(n, 0, sizeof(*n));
            n->active = true;
            n->team = (i == 0) ? 0 : (i == 2 ? 1 : -1);
            n->pos = v3(pts[i].x, pts[i].y + 1.0f, pts[i].z);
            n->radius = 6.0f;
            snprintf(n->name, sizeof(n->name), "%s NODE", n->team == 0 ? "RED" : (n->team == 1 ? "BLUE" : "NEUTRAL"));
        }
    } else if (g->game_mode == GM_CONQUEST) {
        g->node_count = 5;
        Vec3 pts[5] = { a, b, mid, v3lerp(a, mid, 0.5f), v3lerp(mid, b, 0.5f) };
        int own[5] = { 0, 1, -1, -1, -1 };
        for (int i = 0; i < 5; i++) {
            ControlNode* n = &g->nodes[i];
            memset(n, 0, sizeof(*n));
            n->active = true;
            n->team = own[i];
            n->pos = v3(pts[i].x, pts[i].y + 1.0f, pts[i].z);
            n->radius = 7.0f;
            snprintf(n->name, sizeof(n->name), "POINT %s", i == 0 ? "A" : i == 1 ? "B" : i == 2 ? "C" : i == 3 ? "D" : "E");
        }
    }
    if (g->game_mode == GM_VCTF) {
        /* all three classes per team: fast Scorpion, air Manta, heavy Goliath */
        for (int t = 0; t < 2; t++)
            for (int k = 0; k < VEH_COUNT; k++) game_spawn_vehicle_type(g, t, k);
    } else if (g->game_mode == GM_ONSLAUGHT || g->game_mode == GM_CONQUEST) {
        /* Battlefield-style modes get a light motor pool as well. */
        for (int t = 0; t < 2; t++) {
            game_spawn_vehicle_type(g, t, 0);
            game_spawn_vehicle_type(g, t, 1);
        }
    } else if (g->game_mode == GM_ALTIS) {
        /* Arma-flavoured civilian motor pool: parked cars scattered around the
         * Altis towns so roleplayers can actually drive between POIs. */
        Vec3 lc; float le;
        map_bounds(g->world, &lc, &le);
        if (le < 22.0f) le = 22.0f;
        static const float spots[5][2] = {
            { -0.18f, -0.05f }, { 0.22f, 0.10f }, { 0.05f, 0.30f },
            { -0.30f, 0.24f }, { 0.34f, -0.24f }
        };
        for (int i = 0; i < 5; i++) {
            int vi = game_spawn_vehicle_type(g, -1, 0);
            if (vi < 0) break;
            Vec3 p = v3(lc.x + le * spots[i][0], lc.y + 0.05f, lc.z + le * spots[i][1]);
            Ray down; down.origin = v3(p.x, lc.y + 80.0f, p.z); down.dir = v3(0, -1, 0);
            RayHit h;
            if (world_raycast(g->world, down, 220.0f, true, &h) && h.normal.y > 0.4f)
                p.y = h.point.y + 0.05f;
            p = spawn_safe_pos(g->world, p);
            g->vehicles[vi].home = p;
            g->vehicles[vi].pos = p;
            g->vehicles[vi].yaw = (float)i * 1.21f;
        }
    }
    if (g->game_mode == GM_BATTLE_ROYALE) {
        Vec3 c; float e;
        map_bounds(g->world, &c, &e);
        g->zone_center_x = c.x;
        g->zone_center_z = c.z;
        g->zone_radius = e * 1.25f;
        g->zone_target = e * 0.28f;
        g->zone_timer = 45.0f;
        g->zone_stage = 0;
        g->br_total = g->num_players;
        g->br_alive = g->num_players;
    } else {
        g->zone_radius = 0;
    }
}

void game_reset(Game* g) {
    /* Player-built structures are runtime actors: remove them before the map
     * actors are indexed for pickups/spawns. */
    game_build_clear(g);
    /* Leaving Invasion (or restarting it) must remove every zombie slot before
     * the normal player team/spawn setup runs. Survivor slots are always the
     * compact prefix, zombies are appended after them. */
    int survivors = 0;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->active && !p->zombie) survivors++;
    }
    if (g->game_mode != GM_INVASION || survivors > 0) {
        for (int i = 0; i < g->num_players; i++) {
            Player* p = &g->players[i];
            if (p->zombie) { p->active = false; p->alive = false; p->zombie = false; }
        }
        if (survivors > 0 && survivors < g->num_players) g->num_players = survivors;
        g->invasion_survivors = survivors;
    }
    if (g->game_mode == GM_INVASION) {
        if (g->invasion_survivors < 1 || g->invasion_survivors > g->num_players)
            g->invasion_survivors = g->num_players;
        g->invasion_wave = 0;
        g->invasion_wave_max = 12;
        g->invasion_spawn_left = 0;
        g->invasion_alive = 0;
        g->invasion_killed = 0;
        g->invasion_score = 0;
        g->invasion_wave_timer = 4.0f;
        g->invasion_spawn_timer = 0.0f;
        g->invasion_victory = false;
    }
    g->match_over = false;
    g->match_over_t = 0.0f;
    g->rewards_granted = false;
    g->reward_total_points = 0;
    g->reward_total_credits = 0;
    memset(g->projs, 0, sizeof(g->projs));
    memset(g->particles, 0, sizeof(g->particles));
    memset(g->beams, 0, sizeof(g->beams));
    memset(g->explosions, 0, sizeof(g->explosions));
    memset(g->killfeed, 0, sizeof(g->killfeed));
    g->particle_count = 0;
    g->frag_limit = 20;
    g->time_limit = 600.0f;
    g->match_time = 0;
    g->elapsed = 0;
    g->match_over = false;
    g->winner = -1;
    g->started = true;
    g->start_countdown = 0;
    g->hitmarker = 0; g->damage_flash = 0; g->shake = 0;
    g->announce_t = 0;
    g->announce[0] = 0;
    g->pickup_msg_t = 0;
    g->multikill = 0; g->multikill_t = 0;
    g->local_kills_this_life = 0;
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->trigger_state, 0, sizeof(g->trigger_state));
    memset(g->trigger_timer, 0, sizeof(g->trigger_timer));
    g->event_count = 0;
    g->pickup_actor_count = 0;
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type == ACT_PICKUP) {
            g->pickups[i].actor = i; g->pickups[i].active = true; g->pickups[i].respawn = 0;
            if (g->pickup_actor_count < MAX_ACTORS) g->pickup_actors[g->pickup_actor_count++] = i;
        }
    }
    assign_teams(g);
    setup_objectives(g);
    game_life_setup(g);
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active) continue;
        p->frags = 0; p->deaths = 0; p->spree = 0;
        p->alive = false; p->respawn_timer = 0;
        p->in_vehicle = false; p->vehicle = -1;
        game_respawn(g, p);
    }
    g->br_alive = game_alive_count(g);
    if (g->game_mode == GM_INVASION) {
        g->time_limit = 1e9f;
        g->frag_limit = 1000000;
        g->score_limit = 0.0f;
        game_announce(g, "ZOMBIE INVASION - HOLD THE LINE", col3(0.55f, 0.95f, 0.35f), 3.0f);
    }
    if (g->game_mode == GM_ALTIS) {
        g->time_limit = 1e9f;
        g->frag_limit = 1000000;
        g->score_limit = 0.0f;
        game_announce(g, "ALTIS LIFE - ARBEITE, HANDLE, UEBERLEBE", col3(0.35f, 0.95f, 0.55f), 3.5f);
    }
    g->start_countdown = 1.6f;
    g->started = true;
    game_build_nav(g);
    if (g->game_mode == GM_DM || g->game_mode == GM_TDM || g->game_mode == GM_RPG)
        g->score_limit = (float)g->frag_limit;
}

void game_start(Game* g) { game_reset(g); }

/* ------------------------------------------------------------------ */
/* fx helpers                                                          */
/* ------------------------------------------------------------------ */
void game_add_particle(Game* g, Vec3 pos, Vec3 vel, Color c, float size, float life, bool additive, float gravity, float drag) {
    if (g->particle_count >= MAX_PARTICLES) {
        /* replace random old */
        int i = rand() % MAX_PARTICLES;
        Particle* p = &g->particles[i];
        p->active = true; p->pos = pos; p->vel = vel; p->color = c; p->size = size;
        p->life = life; p->max_life = life; p->additive = additive; p->gravity = gravity; p->drag = drag; p->stretch = 1.0f;
        return;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g->particles[i];
        if (p->active) continue;
        p->active = true; p->pos = pos; p->vel = vel; p->color = c; p->size = size;
        p->life = life; p->max_life = life; p->additive = additive; p->gravity = gravity; p->drag = drag; p->stretch = 1.0f;
        if (i + 1 > g->particle_count) g->particle_count = i + 1;
        return;
    }
}

void game_beam_fx(Game* g, Vec3 a, Vec3 b, Color c, float width, float life) {
    for (int i = 0; i < MAX_BEAMS; i++) {
        Beam* be = &g->beams[i];
        if (be->active) continue;
        be->active = true; be->a = a; be->b = b; be->color = c; be->width = width; be->life = life; be->max_life = life;
        return;
    }
    Beam* be = &g->beams[0];
    be->active = true; be->a = a; be->b = b; be->color = c; be->width = width; be->life = life; be->max_life = life;
}

void game_explosion(Game* g, Vec3 pos, float radius, float damage, int attacker, Color color) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion* e = &g->explosions[i];
        if (e->active) continue;
        e->active = true; e->pos = pos; e->color = color; e->radius = radius;
        e->life = 0.55f; e->max_life = 0.55f; e->intensity = 2.4f;
        break;
    }
    int n = (int)(radius * 7.0f);
    if (n > 70) n = 70;
    for (int i = 0; i < n; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.6f, 1), frandr(-1, 1)));
        float sp = frandr(2.5f, 9.0f);
        Vec3 v = v3mul(d, sp);
        game_add_particle(g, pos, v, col_lerp(color, col3(1, 0.9f, 0.5f), frandr(0, 0.8f)), frandr(0.35f, 1.1f), frandr(0.35f, 0.9f), true, 4.0f, 1.7f);
    }
    for (int i = 0; i < n / 3; i++) {
        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.1f, 1), frandr(-1, 1)));
        game_add_particle(g, pos, v3mul(d, frandr(1.0f, 3.5f)), col(0.18f, 0.17f, 0.16f, 0.5f), frandr(0.9f, 2.2f), frandr(1.0f, 2.0f), false, 0.8f, 1.0f);
    }
    if (damage > 0) {
        for (int j = 0; j < g->num_players; j++) {
            Player* p = &g->players[j];
            if (!p->active || !p->alive || j == attacker) continue;
            Vec3 c = v3(p->pos.x, p->pos.y + 0.8f, p->pos.z);
            float d = v3len(v3sub(c, pos));
            if (d > radius) continue;
            Vec3 dir = d > 0.01f ? v3mul(v3sub(c, pos), 1.0f / d) : v3(0, 1, 0);
            Ray r; r.origin = pos; r.dir = dir;
            RayHit h;
            if (world_raycast(g->world, r, d - 0.2f, true, &h)) continue;
            float f = 1.0f - d / radius;
            game_damage(g, p, damage * (0.35f + 0.65f * f), attacker, dir, pos, WPN_ROCKET);
            p->vel = v3add(p->vel, v3mul(dir, 6.0f * f));
            p->vel.y += 4.0f * f;
        }
        for (int i = 0; i < g->vehicle_count; i++) {
            Vehicle* v = &g->vehicles[i];
            if (!v->active) continue;
            Vec3 c = v3(v->pos.x, v->pos.y + 0.7f, v->pos.z);
            float d = v3len(v3sub(c, pos));
            if (d > radius) continue;
            float f = 1.0f - d / radius;
            game_damage_vehicle(g, i, damage * (0.45f + 0.55f * f), attacker, pos);
        }
        /* Fortnite building: explosions chew through player structures too. */
        for (int i = 0; i < MAX_BUILDS; i++) {
            BuildPiece* b = &g->builds[i];
            if (!b->active) continue;
            Vec3 c = b->pos;
            float d = v3len(v3sub(c, pos));
            if (d > radius + fmaxf(b->size.x, b->size.z) * 0.5f) continue;
            float f = 1.0f - clamp01(d / (radius + 1.0f));
            game_build_damage(g, i, damage * (0.45f + 0.75f * f), attacker, c);
        }
    }
    if (g->local_player >= 0 && g->local_player < g->num_players) {
        Player* lp = &g->players[g->local_player];
        audio_play_at(SFX_EXPLODE, 1.0f, pos.x, pos.y, pos.z, lp->pos.x, lp->pos.y, lp->pos.z,
                      sinf(lp->yaw), -cosf(lp->yaw));
    } else {
        audio_play_at(SFX_EXPLODE, 1.0f, pos.x, pos.y, pos.z, pos.x, pos.y, pos.z, 0, 1);
    }
}

void game_damage_vehicle(Game* g, int vi, float dmg, int attacker, Vec3 point) {
    game_damage_vehicle_part(g, vi, dmg, attacker, point, -1);
}

void game_damage_vehicle_part(Game* g, int vi, float dmg, int attacker, Vec3 point, int part) {
    if (!g || vi < 0 || vi >= g->vehicle_count || dmg <= 0.0f) return;
    /* Clients predict weapons locally, but the host owns vehicle HP and
     * destruction. Ignoring client-side vehicle damage prevents ghost
     * explosions; the next vehicle packet restores the authoritative state. */
    if (g->networked && !g->server_mode) return;
    Vehicle* v = &g->vehicles[vi];
    if (!v->active) return;
    if (attacker >= 0 && attacker < g->num_players && attacker != v->driver &&
        game_mode_team_based(g->game_mode) && g->players[attacker].team == v->team) return;

    /* Modular damage: a hit on a wheel/engine/weapon puts 55% into that
     * module and 45% into the hull. Destroying a module changes handling or
     * firepower without instantly killing the vehicle. */
    float hull_dmg = dmg;
    if (part >= 0 && part < VEH_MODULES) {
        VehicleModule* m = &v->modules[part];
        if (!m->destroyed && m->max_hp > 0.0f) {
            m->hp -= dmg * 0.55f;
            hull_dmg = dmg * 0.45f;
            if (m->hp <= 0.0f) {
                m->hp = 0.0f;
                m->destroyed = true;
                Player* lp2 = (g->local_player >= 0 && g->local_player < g->num_players)
                              ? &g->players[g->local_player] : NULL;
                if (lp2 && v3len(v3sub(v->pos, lp2->pos)) < 80.0f) {
                    for (int k = 0; k < 12; k++) {
                        Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.1f, 1), frandr(-1, 1)));
                        game_add_particle(g, point, v3mul(d, frandr(2.0f, 6.0f)),
                                          col3(1.0f, 0.62f, 0.2f), frandr(0.08f, 0.22f),
                                          frandr(0.25f, 0.6f), true, 5.0f, 1.2f);
                    }
                }
                if (lp2) {
                    if (v->driver == lp2->id)
                        game_pickup_msg(g, "YOUR %s IS DESTROYED", game_vehicle_module_name(part));
                    else if (attacker == lp2->id)
                        game_pickup_msg(g, "ENEMY %s DESTROYED", game_vehicle_module_name(part));
                }
            }
        }
    }
    v->hp -= hull_dmg;
    v->hit_flash = fminf(1.0f, v->hit_flash + 0.35f + dmg * 0.015f);
    v->killer = attacker;
    /* cheap hit sparks, only near the local player */
    Player* lp = (g->local_player >= 0 && g->local_player < g->num_players) ? &g->players[g->local_player] : NULL;
    if (lp && v3len(v3sub(v->pos, lp->pos)) < 70.0f) {
        int n = vi % 3 + 3;
        for (int k = 0; k < n; k++) {
            Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.3f, 1), frandr(-1, 1)));
            game_add_particle(g, point, v3mul(d, frandr(2, 5)), col3(1.0f, 0.75f, 0.3f), frandr(0.06f, 0.16f), frandr(0.15f, 0.35f), true, 4.0f, 1.0f);
        }
    }
    if (v->hp > 0.0f) return;

    /* destroyed: eject driver, explode, then respawn at home after a delay */
    v->hp = 0.0f;
    int driver = v->driver;
    int type = v->type;
    int team = v->team;
    Vec3 pos = v3(v->pos.x, v->pos.y + 0.9f, v->pos.z);
    if (driver >= 0 && driver < g->num_players) {
        g->players[driver].in_vehicle = false;
        g->players[driver].vehicle = -1;
        g->players[driver].use_latch = true; /* do not instantly re-enter */
    }
    v->driver = -1;
    v->active = false;
    v->respawn = 12.0f;
    v->hit_flash = 0.0f;
    v->smoke = 0.0f;
    game_explosion(g, pos, 7.0f, 85.0f, attacker, team_base_color(team < 0 ? 0 : team));
    if (attacker >= 0 && attacker < g->num_players && attacker != driver) {
        game_add_killfeed(g, "%s destroyed %s's %s", g->players[attacker].name,
                          (driver >= 0 && driver < g->num_players) ? g->players[driver].name : "enemy",
                          game_vehicle_name(type));
    } else {
        game_add_killfeed(g, "%s was destroyed", game_vehicle_name(type));
    }
    if (lp && v3len(v3sub(pos, lp->pos)) < 90.0f) audio_play(SFX_EXPLODE, 0.8f, 0.0f);
}

void game_add_killfeed(Game* g, const char* fmt, ...) {
    char buf[96];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = MAX_KILLFEED - 1; i > 0; i--) g->killfeed[i] = g->killfeed[i - 1];
    str_copy(g->killfeed[0].text, buf, sizeof(g->killfeed[0].text));
    g->killfeed[0].time = 5.0f;
    g->killfeed[0].color = col3(0.85f, 0.9f, 1.0f);
}

void game_announce(Game* g, const char* text, Color c, float dur) {
    str_copy(g->announce, text, sizeof(g->announce));
    g->announce_color = c;
    g->announce_t = dur;
}

void game_pickup_msg(Game* g, const char* fmt, ...) {
    char buf[96];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_copy(g->pickup_msg, buf, sizeof(g->pickup_msg));
    g->pickup_msg_t = 2.2f;
}

/* ------------------------------------------------------------------ */
/* damage / scoring                                                    */
/* ------------------------------------------------------------------ */
void game_damage(Game* g, Player* victim, float dmg, int attacker, Vec3 dir, Vec3 point, int weapon) {
    (void)weapon;
    if (!victim->active || !victim->alive) return;
    if (victim->protection > 0 && attacker != victim->id) return;
    if (g->game_mode == GM_INVASION && attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
        Player* atk = &g->players[attacker];
        /* survivors never friendly-fire each other; zombies never eat zombies */
        if (atk->zombie == victim->zombie) return;
    }
    if (attacker == victim->id) dmg *= 0.6f;
    if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
        Player* atk = &g->players[attacker];
        if (game_mode_team_based(g->game_mode) && atk->team >= 0 && atk->team == victim->team) return;
        dmg *= (atk->dmg_mult > 0.01f ? atk->dmg_mult : 1.0f);
    }
    /* Occupied vehicles soak most of the damage. The driver still takes a
     * small share, and the vehicle explodes when its HP reaches zero. */
    if (victim->in_vehicle && victim->vehicle >= 0 && victim->vehicle < g->vehicle_count) {
        Vehicle* veh = &g->vehicles[victim->vehicle];
        if (veh->active && veh->driver == victim->id) {
            game_damage_vehicle(g, victim->vehicle, dmg * 0.82f, attacker, point);
            if (!victim->alive) return;
            if (!victim->in_vehicle) return; /* destroyed -> explosion handles it */
            dmg *= 0.18f;
        }
    }

    /* Armor Plating upgrade: flat damage reduction, capped at 30%. */
    float reduc = 0.035f * (float)victim->upg[UPG_ARMOR];
    if (reduc > 0.30f) reduc = 0.30f;
    dmg *= 1.0f - reduc;

    float armor_absorb = victim->armor > 0 ? 0.55f : 0.0f;
    float to_armor = dmg * armor_absorb;
    if (to_armor > victim->armor) to_armor = victim->armor;
    victim->armor -= to_armor;
    victim->health -= (dmg - to_armor);

    victim->damage_flash = 1.0f;
    victim->damage_dir = v3norm(v3sub(victim->pos, point));
    victim->shake = fminf(victim->shake + dmg * 0.012f, 1.0f);
    if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
        Player* a = &g->players[attacker];
        if (a->alive) {
            float lx = a->pos.x, ly = a->pos.y, lz = a->pos.z;
            audio_play_at(SFX_HIT, 0.8f, victim->pos.x, victim->pos.y + 1.2f, victim->pos.z, lx, ly, lz, sinf(a->yaw), -cosf(a->yaw));
        }
    }
    if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
        victim->last_attacker = attacker;
        victim->last_damage_t = g->time;
        Player* vp = &g->players[attacker];
        if (!vp->zombie && vp->upg[UPG_VAMP] > 0 && vp->alive) {
            float heal = fminf(14.0f, dmg * 0.02f * (float)vp->upg[UPG_VAMP]);
            vp->health = fminf(game_player_max_health(vp), vp->health + heal);
            vp->vamp_flash = fminf(1.0f, vp->vamp_flash + heal * 0.12f);
        }
    }
    if (g->local_player == victim->id) { g->damage_flash = 1.0f; g->damage_dir = dir; g->shake = fminf(g->shake + dmg * 0.02f, 1.4f); }
    if (g->local_player == attacker) g->hitmarker = 0.28f;

    if (victim->health <= 0) {
        victim->health = 0;
        victim->alive = false;
        victim->deaths++;
        victim->spree = 0;
        victim->death_time = 0;
        victim->respawn_timer = victim->zombie ? 1.15f :
                                ((g->game_mode == GM_BATTLE_ROYALE) ? 1e9f : 2.4f);
        victim->vel = v3(0, 0, 0);
        victim->build_mode = false;
        victim->build_cd = 0.0f;
        if (victim->in_vehicle) {
            int vi = victim->vehicle;
            if (vi >= 0 && vi < g->vehicle_count) g->vehicles[vi].driver = -1;
            victim->in_vehicle = false;
            victim->vehicle = -1;
        }
        victim->killer = attacker;
        if (g->game_mode == GM_ALTIS) {
            if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
                Player* a = &g->players[attacker];
                a->wanted = a->wanted < 5 ? a->wanted + 1 : 5;
                if (a->id == g->local_player)
                    game_announce(g, "WANTED LEVEL ERHOEHT", col3(1.0f, 0.35f, 0.25f), 2.2f);
            }
            int lost = victim->cash / 5;
            victim->cash -= lost;
            victim->ore = 0;
            victim->life_job = 0;
            victim->life_stage = 0;
            if (victim->id == g->local_player) {
                char lc[96];
                snprintf(lc, sizeof(lc), "HOSPITALRECHNUNG -%d $", lost);
                game_announce(g, lc, col3(1.0f, 0.55f, 0.30f), 2.4f);
            }
        }
        int wi = victim->weapon;
        (void)wi;
        /* death fx */
        for (int i = 0; i < 26; i++) {
            Vec3 d = v3norm(v3(frandr(-1, 1), frandr(0.0f, 1.4f), frandr(-1, 1)));
            game_add_particle(g, v3(victim->pos.x, victim->pos.y + 1.1f, victim->pos.z), v3mul(d, frandr(2, 6)),
                              team_color(victim->id), frandr(0.18f, 0.5f), frandr(0.5f, 1.1f), true, 6.0f, 1.4f);
        }
        if (getenv("A2K4_COMBATLOG")) {
            fprintf(stderr, "[kill] victim=%s killer=%d weapon=%d pos=%.1f,%.1f,%.1f\n",
                    victim->name, attacker, weapon, victim->pos.x, victim->pos.y, victim->pos.z);
        }
        if (attacker >= 0 && attacker < g->num_players && attacker != victim->id &&
            !g->players[attacker].zombie) {
            Player* a = &g->players[attacker];
            a->frags++;
            a->spree++;
            if (g->game_mode == GM_INVASION && victim->zombie) {
                a->xp += 20 + victim->zombie_type * 20;
                g->invasion_killed++;
                g->invasion_score += 10 + victim->zombie_type * 10;
                game_add_reward(g, a, 25 + victim->zombie_type * 20, 8 + victim->zombie_type * 4, "ZOMBIE KILL");
            } else if (!victim->zombie) {
                game_add_reward(g, a, 100 + victim->level * 10, 20, "KILL");
                /* Fortnite loop: eliminations feed the build economy. */
                if (g->game_mode != GM_ALTIS) {
                    a->mats[0] += 25;
                    a->mats[1] += 15;
                    a->mats[2] += 10;
                    if (a->id == g->local_player)
                        game_pickup_msg(g, "BEUTE: +25 Holz  +15 Stein  +10 Metall");
                }
            }
            /* assist for the last other player who damaged the victim */
            if (victim->last_attacker >= 0 && victim->last_attacker < g->num_players &&
                victim->last_attacker != attacker && victim->last_attacker != victim->id &&
                g->time - victim->last_damage_t < 8.0f) {
                Player* as = &g->players[victim->last_attacker];
                if (as->active && !as->zombie) {
                    as->assists++;
                    game_add_reward(g, as, 40, 8, "ASSIST");
                }
            }
            if (g->game_mode == GM_RPG || (g->game_mode == GM_INVASION && victim->zombie)) {
                a->xp += 100 + victim->level * 25;
                bool leveled = false;
                while (a->xp >= a->xp_next && a->level < 60) {
                    a->xp -= a->xp_next;
                    a->level++;
                    a->xp_next = 100 + a->level * 60;
                    game_player_refresh_stats(a);
                    a->health = game_player_max_health(a);
                    leveled = true;
                }
                if (leveled) {
                    char lb[64];
                    snprintf(lb, sizeof(lb), "LEVEL %d", a->level);
                    game_announce(g, lb, col3(0.4f, 1.0f, 0.55f), 2.2f);
                    game_add_killfeed(g, "%s reached level %d", a->name, a->level);
                }
            }
            if (game_mode_team_based(g->game_mode) && a->team >= 0) {
                g->team_kills[a->team]++;
                if (g->game_mode == GM_TDM || g->game_mode == GM_RPG) {
                    g->team_score[a->team]++;
                    if (g->team_score[a->team] >= (int)g->score_limit)
                        finish_match_team(g, a->team, "frag limit");
                }
            }
            a->health = fminf(game_player_max_health(a), a->health + 12.0f);
            a->armor = fminf(game_player_max_armor(a), a->armor + 8.0f);
            if (a->id == g->local_player) {
                g->local_kills_this_life++;
                g->multikill_t = 3.0f;
                g->multikill++;
                if (g->multikill == 2) game_announce(g, "DOUBLE KILL", col3(1.0f, 0.85f, 0.2f), 2.0f);
                else if (g->multikill == 3) game_announce(g, "TRIPLE KILL", col3(1.0f, 0.6f, 0.1f), 2.2f);
                else if (g->multikill >= 4) game_announce(g, "MULTI KILL!", col3(1.0f, 0.35f, 0.2f), 2.4f);
                audio_play(SFX_DEATH, 0.5f, 0);
            }
            if (a->spree == 5) game_announce(g, "KILLING SPREE", col3(1, 0.5f, 0.15f), 2.2f);
            if (a->spree == 10) game_announce(g, "RAMPAGE!", col3(1, 0.35f, 0.2f), 2.4f);
            if (a->spree == 15) game_announce(g, "UNSTOPPABLE!", col3(1, 0.2f, 0.5f), 2.6f);
            if (a->spree == 20) game_announce(g, "GODLIKE!", col3(0.8f, 0.3f, 1), 2.8f);
            if (a->id == g->local_player && victim->id != a->id) audio_play(SFX_HEADSHOT, 0.4f, 0);
            if (!victim->zombie || victim->zombie_type >= 2)
                game_add_killfeed(g, "%s  >>  %s", a->name, victim->name);
        } else if (attacker >= 0 && attacker < g->num_players && g->players[attacker].zombie) {
            game_add_killfeed(g, "%s  >>  %s", g->players[attacker].name, victim->name);
        } else {
            game_add_killfeed(g, "%s died", victim->name);
        }
        /* dropped weapon */
        if (victim->ammo[victim->weapon] > 0 || victim->weapon != WPN_AR) {
            /* optional: no actor spawn to keep map static */
        }
        if (victim->id == g->local_player && g->demo_mode) {
            g->local_player = (g->local_player + 1) % g->num_players;
        }
        for (int i = g->num_players - 1; i >= 0; i--) {
            if (g->game_mode == GM_INVASION) break;
            if (g->players[i].frags >= g->frag_limit) { g->match_over = true; g->winner = i; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* firing                                                              */
/* ------------------------------------------------------------------ */
static bool trace_players(Game* g, Vec3 a, Vec3 b, int ignore, int* hit_player, Vec3* hit_point) {
    float best = v3len(v3sub(b, a));
    int bestp = -1;
    Vec3 seg = v3sub(b, a);
    float seglen = v3len(seg);
    if (seglen < 1e-4f) return false;
    Vec3 dir = v3mul(seg, 1.0f / seglen);
    for (int i = 0; i < g->num_players; i++) {
        if (i == ignore) continue;
        Player* p = &g->players[i];
        if (!p->active || !p->alive) continue;
        Vec3 c = v3(p->pos.x, p->pos.y + 0.95f, p->pos.z);
        float t = ray_sphere(a, dir, c, 0.72f);
        if (t >= 0 && t < best) { best = t; bestp = i; }
    }
    if (bestp >= 0) { if (hit_player) *hit_player = bestp; if (hit_point) *hit_point = v3add(a, v3mul(dir, best)); return true; }
    return false;
}

/* Which module did a shot hit? Corners are wheels/thrusters, the rear is the
 * engine, the front/top is the weapon. This is intentionally cheap: one dot
 * product per axis, no per-part collision meshes. */
static int veh_module_from_hit(Vehicle* v, Vec3 hit) {
    Vec3 f = v3(sinf(v->yaw), 0, -cosf(v->yaw));
    Vec3 r = v3(cosf(v->yaw), 0, sinf(v->yaw));
    Vec3 rel = v3sub(hit, v->pos);
    float lf = v3dot(rel, f);
    float ls = v3dot(rel, r);
    float lu = rel.y;
    if (v->type == 1) {
        if (fabsf(ls) > 0.75f) return (lf >= 0.0f ? 0 : 2) + (ls >= 0.0f ? 1 : 0);
        if (lf < -0.40f) return VEH_ENGINE;
        return VEH_WEAPON;
    }
    float half_w = (v->type == 2) ? 1.30f : 0.95f;
    if (fabsf(ls) > half_w && lu < 0.95f)
        return (lf >= 0.0f ? 0 : 2) + (ls >= 0.0f ? 1 : 0);
    if (lu > (v->type == 2 ? 0.95f : 0.68f) && lf > -0.35f) return VEH_WEAPON;
    return VEH_ENGINE;
}

static bool trace_vehicles(Game* g, Vec3 a, Vec3 b, int ignore_player, int* hit_vehicle, Vec3* hit_point, int* hit_module) {
    float seglen = v3len(v3sub(b, a));
    if (seglen < 1e-4f) return false;
    Vec3 dir = v3mul(v3sub(b, a), 1.0f / seglen);
    float best = seglen;
    int bestv = -1;
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active) continue;
        if (ignore_player >= 0 && v->driver == ignore_player) continue;
        const VehicleDef* vd = &vehicle_defs[v->type];
        Vec3 c = v3(v->pos.x, v->pos.y + (v->type == 1 ? 0.55f : 0.75f), v->pos.z);
        float t = ray_sphere(a, dir, c, vd->hit_radius);
        if (t >= 0.0f && t < best) { best = t; bestv = i; }
    }
    if (bestv >= 0) {
        Vec3 hp = v3add(a, v3mul(dir, best));
        if (hit_vehicle) *hit_vehicle = bestv;
        if (hit_point) *hit_point = hp;
        if (hit_module) *hit_module = veh_module_from_hit(&g->vehicles[bestv], hp);
        return true;
    }
    return false;
}

static void fire_hitscan(Game* g, Player* p, float damage, float spread, Color color, float beam_width, int weapon) {
    Vec3 eye = player_eye(p);
    Vec3 dir; dir_from_angles(p->yaw, p->pitch, &dir);
    if (spread > 0) {
        Vec3 r = v3norm(v3cross(dir, v3(0, 1, 0)));
        Vec3 u = v3cross(r, dir);
        float a = frand() * 2 * PI, b = sqrtf(frand()) * spread;
        dir = v3norm(v3add(dir, v3add(v3mul(r, cosf(a) * b), v3mul(u, sinf(a) * b))));
    }
    float maxd = 140.0f;
    Ray r; r.origin = eye; r.dir = dir;
    RayHit h;
    float wd = maxd;
    Vec3 end = v3add(eye, v3mul(dir, maxd));
    if (world_raycast(g->world, r, maxd, true, &h)) { wd = h.t; end = h.point; }
    /* Shock combo: primary hitscan detonates a shock ball on its path. */
    bool combo = false;
    if (weapon == WPN_SHOCK) {
        int bi = -1; float bt = wd;
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            Projectile* pr = &g->projs[i];
            if (!pr->active || pr->type != WPN_SHOCK) continue;
            float t = ray_sphere(eye, dir, pr->pos, 0.55f);
            if (t >= 0.0f && t < bt) { bt = t; bi = i; }
        }
        if (bi >= 0) {
            Projectile* pr = &g->projs[bi];
            pr->active = false;
            end = pr->pos;
            wd = bt;
            combo = true;
            game_explosion(g, pr->pos, 7.5f, 100.0f, p->id, col3(0.35f, 0.8f, 1.0f));
            if (p->id == g->local_player) audio_play(SFX_EXPLODE, 0.6f, 0);
            game_add_killfeed(g, "%s triggered a shock combo", p->name);
        }
    }
    int hp = -1; Vec3 hpt; bool hitp = false;
    if (!combo) hitp = trace_players(g, eye, end, p->id, &hp, &hpt);
    int hv = -1, hmod = -1; Vec3 hvt; bool hitv = false;
    if (!combo) {
        Vec3 limit = hitp ? hpt : end;
        hitv = trace_vehicles(g, eye, limit, p->id, &hv, &hvt, &hmod);
    }
    if (!combo && h.hit && !hitp && !hitv) {
        int bp = game_build_index_for_actor(g, h.actor);
        if (bp >= 0) {
            game_build_damage(g, bp, damage, p->id, h.point);
            for (int i = 0; i < 6; i++) {
                Vec3 d = v3norm(v3add(dir, v3(frandr(-0.75f, 0.75f), frandr(-0.75f, 0.75f), frandr(-0.75f, 0.75f))));
                game_add_particle(g, h.point, v3mul(d, frandr(1.4f, 4.2f)),
                                  build_color_for(g->builds[bp].material),
                                  frandr(0.08f, 0.2f), frandr(0.2f, 0.45f), true, 4.0f, 1.2f);
            }
        }
    }
    if (hitv) {
        end = hvt; wd = v3len(v3sub(hvt, eye));
        game_damage_vehicle_part(g, hv, damage, p->id, hvt, hmod);
        for (int i = 0; i < 7; i++) {
            Vec3 d = v3norm(v3add(dir, v3(frandr(-0.6f, 0.6f), frandr(-0.6f, 0.6f), frandr(-0.6f, 0.6f))));
            Vec3 hvpos = g->vehicles[hv].pos;
            game_add_particle(g, hvt, v3mul(d, frandr(1.5f, 4.5f)), team_base_color(g->vehicles[hv].team < 0 ? 0 : g->vehicles[hv].team), frandr(0.12f, 0.3f), frandr(0.25f, 0.5f), true, 2.0f, 1.2f);
            (void)hvpos;
        }
        if (p->id == g->local_player) audio_play(SFX_HIT, 0.5f, 0);
    } else if (hitp) {
        end = hpt; wd = v3len(v3sub(hpt, eye));
        Player* v = &g->players[hp];
        game_damage(g, v, damage, p->id, dir, hpt, weapon);
        for (int i = 0; i < 7; i++) {
            Vec3 d = v3norm(v3add(dir, v3(frandr(-0.6f, 0.6f), frandr(-0.6f, 0.6f), frandr(-0.6f, 0.6f))));
            game_add_particle(g, hpt, v3mul(d, frandr(1.5f, 4.5f)), team_color(v->id), frandr(0.12f, 0.3f), frandr(0.25f, 0.5f), true, 2.0f, 1.2f);
        }
        if (p->id == g->local_player) audio_play(SFX_HIT, 0.5f, 0);
    } else if (!combo) {
        /* impact spark */
        Vec3 n = h.hit ? h.normal : v3(0, 1, 0);
        for (int i = 0; i < 5; i++) {
            Vec3 d = v3norm(v3add(n, v3(frandr(-0.8f, 0.8f), frandr(-0.2f, 0.8f), frandr(-0.8f, 0.8f))));
            game_add_particle(g, end, v3mul(d, frandr(1.0f, 3.5f)), color, frandr(0.06f, 0.16f), frandr(0.15f, 0.35f), true, 5.0f, 1.0f);
        }
    }
    game_beam_fx(g, v3add(eye, v3mul(dir, 0.4f)), end, color, beam_width, 0.09f);
}

static void vehicle_fire(Game* g, Player* p, bool alt) {
    int vi = p->vehicle;
    if (vi < 0 || vi >= g->vehicle_count) return;
    Vehicle* v = &g->vehicles[vi];
    if (!v->active || v->driver != p->id) return;
    if (v->modules[VEH_WEAPON].destroyed) return;
    const VehicleDef* vd = &vehicle_defs[v->type];
    if (v->fire_timer > 0.0f) return;
    v->fire_timer = vd->fire_delay * (alt ? 1.55f : 1.0f);

    Vec3 dir; dir_from_angles(p->yaw, p->pitch, &dir);
    Vec3 right = v3(cosf(p->yaw), 0, sinf(p->yaw));
    float gun_h = v->type == 1 ? 0.05f : (v->type == 2 ? 1.05f : 0.72f);
    Vec3 muzzle = v3add(v3add(v->pos, v3(0, gun_h, 0)), v3mul(dir, v->type == 2 ? 1.9f : 1.35f));
    if (v->type == 0) muzzle = v3add(muzzle, v3mul(right, (alt ? -0.55f : 0.55f)));

    Projectile* pr = NULL;
    for (int i = 0; i < MAX_PROJECTILES; i++) if (!g->projs[i].active) { pr = &g->projs[i]; break; }
    if (!pr) return;
    memset(pr, 0, sizeof(*pr));
    pr->active = true;
    pr->type = v->type == 2 ? WPN_ROCKET : WPN_SHOCK;
    pr->owner = p->id;
    pr->pos = muzzle;
    pr->vel = v3mul(dir, vd->proj_speed * (alt ? 1.15f : 1.0f));
    pr->life = 5.0f;
    pr->damage = vd->damage * (alt ? 1.45f : 1.0f);
    pr->splash = vd->splash * (alt ? 1.35f : 1.0f);
    pr->radius = vd->splash > 0.1f ? (v->type == 2 ? 5.0f : 4.0f) : 0.0f;
    pr->gravity = vd->gravity;
    pr->color = v->type == 2 ? col3(1.0f, 0.5f, 0.15f) : col3(0.35f, 0.8f, 1.0f);
    p->muzzle_flash = 0.10f;
    p->recoil = fminf(p->recoil + 0.35f, 1.2f);
    p->view_kick += v->type == 2 ? 0.10f : 0.03f;
    if (p->id == g->local_player) {
        audio_play(v->type == 2 ? SFX_ROCKET : SFX_SHOCK, 0.55f, 0.0f);
        if (g->shake < 0.25f) g->shake = v->type == 2 ? 0.32f : 0.12f;
    }
}

void game_fire(Game* g, Player* p, bool alt) {
    if (!p->alive || p->protection > 1.7f) return;
    if (p->in_vehicle) { vehicle_fire(g, p, alt); return; }
    int wi = p->weapon;
    if (wi < 0 || wi >= WPN_COUNT) return;
    WeaponDef* wd = &weapon_defs[wi];
    if (!p->has[wi]) return;
    if (p->fire_timer > 0) return;
    if (p->ammo[wi] <= 0) {
        if (p->id == g->local_player) audio_play(SFX_UI_CLICK, 0.35f, 0);
        return;
    }
    if (p->reload_timer > 0.0f) return;
    if (p->mag[wi] <= 0) {
        /* Pulling the trigger on an empty magazine starts a reload. */
        reload_begin(p);
        if (p->id == g->local_player) audio_play(SFX_UI_CLICK, 0.3f, 0);
        return;
    }
    float delay = alt ? wd->alt_delay : wd->fire_delay;
    float rate = game_player_fire_mult(p);
    if (rate < 0.25f) rate = 0.25f;
    p->fire_timer = delay * rate;
    p->ammo[wi] -= 1;
    p->mag[wi] -= 1;
    p->muzzle_flash = 0.09f;
    p->recoil = fminf(p->recoil + 0.6f, 1.5f);
    p->view_kick += alt ? 0.05f : 0.035f;
    Vec3 eye = player_eye(p);
    Vec3 dir; dir_from_angles(p->yaw, p->pitch, &dir);

    float vol = wi == WPN_FLAK ? 0.55f : (wi == WPN_LIGHTNING ? 0.4f : 0.5f);
    if (p->id == g->local_player) audio_play((SfxId)(SFX_AR + wi), vol, 0);

    switch (wi) {
    case WPN_AR:
        fire_hitscan(g, p, alt ? wd->alt_damage : wd->damage, wd->spread, col3(1.0f, 0.85f, 0.5f), 0.035f, wi);
        break;
    case WPN_SHOCK:
        if (alt) {
            /* shock ball */
            Projectile* pr = NULL;
            for (int i = 0; i < MAX_PROJECTILES; i++) if (!g->projs[i].active) { pr = &g->projs[i]; break; }
            if (!pr) return;
            memset(pr, 0, sizeof(*pr));
            pr->active = true; pr->type = WPN_SHOCK; pr->owner = p->id;
            pr->pos = v3add(eye, v3mul(dir, 0.9f)); pr->vel = v3mul(dir, wd->alt_speed);
            pr->life = 4.0f; pr->damage = wd->alt_damage; pr->splash = wd->alt_splash; pr->radius = 3.6f;
            pr->color = col3(0.25f, 0.65f, 1.0f); pr->gravity = 3.0f;
        } else {
            fire_hitscan(g, p, wd->damage, 0.0f, col3(0.4f, 0.75f, 1.0f), 0.06f, wi);
            for (int i = 0; i < 10; i++) {
                Vec3 d = v3norm(v3add(dir, v3(frandr(-0.25f, 0.25f), frandr(-0.25f, 0.25f), frandr(-0.25f, 0.25f))));
                game_add_particle(g, v3add(eye, v3mul(dir, 0.7f)), v3mul(d, frandr(2, 6)), col3(0.35f, 0.7f, 1.0f), frandr(0.1f, 0.25f), 0.3f, true, 0, 2);
            }
        }
        break;
    case WPN_ROCKET: {
        Projectile* pr = NULL;
        for (int i = 0; i < MAX_PROJECTILES; i++) if (!g->projs[i].active) { pr = &g->projs[i]; break; }
        if (!pr) return;
        memset(pr, 0, sizeof(*pr));
        pr->active = true; pr->type = WPN_ROCKET; pr->owner = p->id;
        pr->pos = v3add(eye, v3mul(dir, 0.9f));
        pr->vel = v3mul(dir, alt ? wd->alt_speed : wd->speed);
        pr->life = 6.0f; pr->damage = alt ? wd->alt_damage : wd->damage; pr->splash = alt ? wd->alt_splash : wd->splash;
        pr->radius = alt ? 4.6f : 5.2f; pr->color = col3(1.0f, 0.45f, 0.15f);
        pr->gravity = alt ? 16.0f : 0.0f;
        pr->bounces = alt ? 3 : 0;
        break;
    }
    case WPN_FLAK: {
        if (alt) {
            Projectile* pr = NULL;
            for (int i = 0; i < MAX_PROJECTILES; i++) if (!g->projs[i].active) { pr = &g->projs[i]; break; }
            if (!pr) return;
            memset(pr, 0, sizeof(*pr));
            pr->active = true; pr->type = WPN_FLAK; pr->owner = p->id;
            pr->pos = v3add(eye, v3mul(dir, 0.9f)); pr->vel = v3mul(dir, wd->alt_speed);
            pr->life = 4.0f; pr->damage = wd->alt_damage; pr->splash = wd->alt_splash; pr->radius = 4.2f;
            pr->color = col3(1.0f, 0.65f, 0.2f); pr->gravity = 14.0f;
        } else {
            for (int k = 0; k < wd->pellets; k++) {
                fire_hitscan(g, p, wd->damage, wd->spread, col3(1.0f, 0.72f, 0.3f), 0.02f, wi);
            }
        }
        break;
    }
    case WPN_LIGHTNING:
        fire_hitscan(g, p, alt ? wd->alt_damage : wd->damage, 0.0f, alt ? col3(0.85f, 0.65f, 1.0f) : col3(0.75f, 0.5f, 1.0f), alt ? 0.14f : 0.08f, wi);
        for (int i = 0; i < 6; i++) {
            Vec3 d = v3norm(v3add(dir, v3(frandr(-0.12f, 0.12f), frandr(-0.12f, 0.12f), frandr(-0.12f, 0.12f))));
            game_add_particle(g, v3add(eye, v3mul(dir, frandr(0.6f, 1.4f))), v3mul(d, frandr(1, 4)), col3(0.7f, 0.45f, 1.0f), frandr(0.05f, 0.14f), 0.22f, true, 0, 3);
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* pickups                                                             */
/* ------------------------------------------------------------------ */
static void give_weapon(Game* g, Player* p, int type, int wi) {
    (void)type;
    p->has[wi] = true;
    p->ammo[wi] = game_player_ammo_max(p, wi);
    p->mag[wi] = game_player_mag_cap(wi);
    p->weapon = wi;
    p->reload_timer = 0; p->reload_total = 0; p->reload_weapon = -1;
    if (p->id == g->local_player) {
        game_pickup_msg(g, "Picked up %s", weapon_defs[wi].name);
        audio_play(SFX_PICKUP_WEAPON, 0.7f, 0);
    }
}

static void apply_pickup(Game* g, Player* p, Actor* a) {
    int sub = a->subtype;
    switch (sub) {
    case PICK_HEALTH_SMALL: p->health = fminf(game_player_max_health(p), p->health + 10.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+10 Health"); audio_play(SFX_PICKUP_HEALTH, 0.6f, 0); } break;
    case PICK_HEALTH_BIG: p->health = fminf(game_player_max_health(p), p->health + 25.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+25 Health"); audio_play(SFX_PICKUP_HEALTH, 0.8f, 0); } break;
    case PICK_ARMOR_SMALL: p->armor = fminf(game_player_max_armor(p), p->armor + 25.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+25 Shield"); audio_play(SFX_PICKUP_ARMOR, 0.6f, 0); } break;
    case PICK_ARMOR_BIG: p->armor = fminf(game_player_max_armor(p), p->armor + 50.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+50 Shield"); audio_play(SFX_PICKUP_ARMOR, 0.8f, 0); } break;
    case PICK_AMMO: {
        int wi = p->weapon;
        p->ammo[wi] = (int)fminf((float)game_player_ammo_max(p, wi), p->ammo[wi] + 20.0f);
        if (p->id == g->local_player) { game_pickup_msg(g, "+20 %s Ammo", weapon_defs[wi].short_name); audio_play(SFX_PICKUP_AMMO, 0.6f, 0); }
        break;
    }
    default:
        if (sub >= PICK_WEAPON_AR && sub <= PICK_WEAPON_LIGHTNING) give_weapon(g, p, sub, sub - PICK_WEAPON_AR);
        break;
    }
}

static void update_pickups(Game* g, float dt) {
    for (int pi = 0; pi < g->pickup_actor_count; pi++) {
        int i = g->pickup_actors[pi];
        if (i < 0 || i >= g->world->count) continue;
        Actor* a = &g->world->actors[i];
        PickupState* ps = &g->pickups[i];
        ps->actor = i;
        if (!ps->active) {
            ps->respawn -= dt;
            if (ps->respawn <= 0) ps->active = true;
            continue;
        }
        for (int j = 0; j < g->num_players; j++) {
            Player* p = &g->players[j];
            if (!p->active || !p->alive) continue;
            float dx = p->pos.x - a->pos.x, dz = p->pos.z - a->pos.z;
            float dy = (p->pos.y + 0.8f) - a->pos.y;
            if (dx * dx + dz * dz < 1.45f * 1.45f && fabsf(dy) < 1.6f) {
                apply_pickup(g, p, a);
                int wi = p->weapon;
                if (a->subtype < PICK_WEAPON_AR) {
                    /* ammo only for current weapon; allow re-pickup */
                }
                (void)wi;
                ps->active = false;
                ps->respawn = a->respawn > 0 ? a->respawn : 20.0f;
                for (int k = 0; k < 10; k++) {
                    Vec3 d = v3norm(v3(frandr(-1, 1), frandr(0, 1.4f), frandr(-1, 1)));
                    game_add_particle(g, a->pos, v3mul(d, frandr(1, 3.5f)), a->color, frandr(0.1f, 0.28f), frandr(0.3f, 0.6f), true, 1.0f, 2.0f);
                }
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* projectiles / particles / effects                                   */
/* ------------------------------------------------------------------ */
static void update_projectiles(Game* g, float dt) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* pr = &g->projs[i];
        if (!pr->active) continue;
        pr->age += dt;
        pr->life -= dt;
        if (pr->life <= 0) {
            if (pr->splash > 0) game_explosion(g, pr->pos, pr->radius, 0, pr->owner, pr->color);
            pr->active = false;
            continue;
        }
        pr->vel.y -= pr->gravity * dt;
        Vec3 prev = pr->pos;
        Vec3 step = v3mul(pr->vel, dt);
        float sl = v3len(step);
        bool boom = false;
        int build_hit = -1;
        if (sl > 0.0001f) {
            Ray r; r.origin = prev; r.dir = v3mul(step, 1.0f / sl);
            RayHit h;
            if (world_raycast(g->world, r, sl, true, &h)) {
                if (pr->type == WPN_ROCKET && pr->bounces > 0) {
                    Vec3 n = h.normal;
                    pr->pos = v3add(h.point, v3mul(n, 0.06f));
                    float vn = v3dot(pr->vel, n);
                    pr->vel = v3sub(pr->vel, v3mul(n, 1.65f * vn));
                    pr->vel = v3mul(pr->vel, 0.55f);
                    pr->bounces--;
                } else { pr->pos = h.point; boom = true; build_hit = game_build_index_for_actor(g, h.actor); }
            }
            else pr->pos = v3add(prev, step);
        } else pr->pos = v3add(prev, step);

        int hp = -1; Vec3 hpt; bool hitp = false;
        if (!boom) hitp = trace_players(g, prev, pr->pos, pr->owner, &hp, &hpt);
        int hv = -1, hmod = -1; Vec3 hvt; bool hitv = false;
        if (!boom) {
            Vec3 limit = hitp ? hpt : pr->pos;
            hitv = trace_vehicles(g, prev, limit, pr->owner, &hv, &hvt, &hmod);
        }
        if (hitv) { pr->pos = hvt; boom = true; hp = -1; }
        else if (hitp) { pr->pos = hpt; boom = true; }
        if (boom) {
            if (build_hit >= 0 && pr->splash <= 0.0f)
                game_build_damage(g, build_hit, pr->damage, pr->owner, pr->pos);
            if (pr->splash > 0) game_explosion(g, pr->pos, pr->radius, pr->damage, pr->owner, pr->color);
            else {
                if (hp >= 0) game_damage(g, &g->players[hp], pr->damage, pr->owner, v3norm(pr->vel), pr->pos, pr->type);
                if (hv >= 0) game_damage_vehicle_part(g, hv, pr->damage, pr->owner, pr->pos, hmod);
                for (int k = 0; k < 8; k++) {
                    Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.4f, 1), frandr(-1, 1)));
                    game_add_particle(g, pr->pos, v3mul(d, frandr(1, 4)), pr->color, frandr(0.08f, 0.2f), frandr(0.2f, 0.5f), true, 4, 1.5f);
                }
            }
            pr->active = false;
            continue;
        }
        /* trail */
        Color tc = pr->type == WPN_SHOCK ? col3(0.35f, 0.7f, 1.0f) : pr->color;
        game_add_particle(g, pr->pos, v3mul(pr->vel, -0.02f), tc, frandr(0.14f, 0.3f), frandr(0.12f, 0.3f), true, 0, 2.0f);
        if (pr->type == WPN_ROCKET && frand() < 0.7f) {
            game_add_particle(g, pr->pos, v3(frandr(-0.5f, 0.5f), frandr(-0.3f, 0.6f), frandr(-0.5f, 0.5f)), col(0.25f, 0.24f, 0.23f, 0.35f), frandr(0.25f, 0.6f), frandr(0.4f, 0.9f), false, 0.6f, 1.0f);
        }
    }
}

static void update_effects(Game* g, float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g->particles[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0) { p->active = false; continue; }
        p->vel.y -= p->gravity * dt;
        p->vel = v3mul(p->vel, 1.0f - clampf(p->drag * dt, 0, 0.95f));
        p->pos = v3add(p->pos, v3mul(p->vel, dt));
        if (p->gravity > 0) {
            if (p->pos.y < 0.03f) {
                p->pos.y = 0.03f;
                p->vel.y = -p->vel.y * 0.35f;
                p->vel.x *= 0.6f; p->vel.z *= 0.6f;
            }
        }
    }
    for (int i = 0; i < MAX_BEAMS; i++) {
        Beam* b = &g->beams[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0) b->active = false;
    }
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion* e = &g->explosions[i];
        if (!e->active) continue;
        e->life -= dt;
        if (e->life <= 0) e->active = false;
    }
    for (int i = 0; i < MAX_KILLFEED; i++) {
        if (g->killfeed[i].time > 0) g->killfeed[i].time -= dt;
    }
}

/* ------------------------------------------------------------------ */
/* movement                                                            */
/* ------------------------------------------------------------------ */
static void update_player_movement(Game* g, Player* p, float dt) {
    if (!p->alive) {
        p->death_time += dt;
        if (p->zombie) {
            /* Zombies dissolve and free their slot; only the wave director
             * ever brings them back. */
            p->respawn_timer -= dt;
            if (p->respawn_timer <= 0.0f) { p->active = false; p->alive = false; }
            return;
        }
        p->respawn_timer -= dt;
        if (p->respawn_timer <= 0) game_respawn(g, p);
        return;
    }
    if (p->fire_timer > 0) p->fire_timer -= dt;
    if (p->alt_timer > 0) p->alt_timer -= dt;
    if (p->protection > 0) p->protection -= dt;
    if (p->muzzle_flash > 0) p->muzzle_flash -= dt;
    if (p->recoil > 0) p->recoil = fmaxf(0, p->recoil - dt * 3.5f);
    if (p->view_kick > 0) p->view_kick = fmaxf(0, p->view_kick - dt * 2.6f);
    if (p->shake > 0) p->shake = fmaxf(0, p->shake - dt * 2.4f);
    if (p->damage_flash > 0) p->damage_flash = fmaxf(0, p->damage_flash - dt * 3.0f);
    if (p->land_kick > 0) p->land_kick = fmaxf(0, p->land_kick - dt * 5.0f);
    if (p->vamp_flash > 0) p->vamp_flash = fmaxf(0, p->vamp_flash - dt * 2.5f);
    if (p->dodge_cooldown > 0) p->dodge_cooldown -= dt;
    reload_tick(g, p, dt);
    if (p->reload_timer <= 0.0f) {
        bool want_reload = p->input.reload ||
            (p->bot && p->mag[p->weapon] <= 0 && p->ammo[p->weapon] > 0);
        if (want_reload) reload_begin(p);
    }

    float yaw = p->yaw;
    Vec3 fwd = v3(sinf(yaw), 0, -cosf(yaw));
    Vec3 right = v3(cosf(yaw), 0, sinf(yaw));
    Vec3 wish = v3add(v3mul(fwd, p->input.move_z), v3mul(right, p->input.move_x));
    float wl = v3len(wish);
    if (wl > 1.0f) wish = v3mul(wish, 1.0f / wl);
    wl = fminf(wl, 1.0f);

    int veh_type = -1;
    if (p->in_vehicle && p->vehicle >= 0 && p->vehicle < g->vehicle_count) {
        Vehicle* v = &g->vehicles[p->vehicle];
        if (v->active) veh_type = v->type;
    }

    bool want_jump = p->input.jump && veh_type < 0;
    if (want_jump && !p->jump_latch) {
        if (p->grounded) {
            p->vel.y = 8.6f;
            p->grounded = false;
            p->jump_count = 1;
            if (p->id == g->local_player) audio_play(SFX_JUMP, 0.45f, 0);
        } else if (p->jump_count < 1) {
            p->vel.y = 7.6f;
            p->jump_count = 1;
            for (int i = 0; i < 12; i++) {
                Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.5f, 0.2f), frandr(-1, 1)));
                game_add_particle(g, v3(p->pos.x, p->pos.y + 0.3f, p->pos.z), v3mul(d, frandr(1, 3)), col(0.6f, 0.8f, 1.0f, 0.6f), frandr(0.1f, 0.25f), frandr(0.2f, 0.45f), true, 1, 2);
            }
            if (p->id == g->local_player) audio_play(SFX_JUMP, 0.4f, 0.1f);
        }
    }
    p->jump_latch = want_jump;

    /* dodge */
    if (veh_type < 0 && p->input.dodge && p->grounded && p->dodge_cooldown <= 0 && wl > 0.1f) {
        p->vel = v3add(p->vel, v3mul(wish, 15.0f));
        p->vel.y = 3.2f;
        p->grounded = false;
        p->dodge_cooldown = 0.85f;
        p->dodge_timer = 0.32f;
        if (p->id == g->local_player) audio_play(SFX_DODGE, 0.5f, 0);
        for (int i = 0; i < 14; i++) {
            Vec3 d = v3norm(v3(frandr(-1, 1), frandr(-0.2f, 0.4f), frandr(-1, 1)));
            game_add_particle(g, v3(p->pos.x, p->pos.y + 0.4f, p->pos.z), v3mul(d, frandr(2, 5)), col(0.7f, 0.85f, 1.0f, 0.5f), frandr(0.1f, 0.3f), frandr(0.2f, 0.5f), false, 1, 2);
        }
    }
    if (p->dodge_timer > 0) p->dodge_timer -= dt;

    /* crouch */
    p->crouch = p->input.crouch && veh_type < 0;

    bool was_grounded = p->grounded;
    float accel = p->grounded ? 62.0f : 14.0f;
    float max_speed = p->grounded ? (p->crouch ? 3.0f : 9.6f) : 10.5f;
    if (veh_type >= 0) {
        const VehicleDef* vd = &vehicle_defs[veh_type];
        accel = vd->accel;
        max_speed = vd->max_speed;
        Vehicle* vv = &g->vehicles[p->vehicle];
        int lost_l = 0, lost_r = 0;
        for (int w = 0; w < VEH_WHEELS; w++) {
            if (!vv->modules[w].destroyed) continue;
            if ((w & 1) == 0) lost_l++; else lost_r++;
        }
        float health_mod = 1.0f - 0.12f * (float)(lost_l + lost_r);
        if (health_mod < 0.40f) health_mod = 0.40f;
        if (vv->modules[VEH_ENGINE].destroyed) health_mod *= 0.45f;
        accel *= health_mod;
        max_speed *= health_mod;
        /* A missing left/right wheel pulls the vehicle to the other side. */
        if ((veh_type == 0 || veh_type == 2) && wl > 0.05f) {
            float drift = (float)(lost_r - lost_l) * 0.35f;
            if (fabsf(drift) > 0.001f) {
                Vec3 rt = v3(cosf(p->yaw), 0, sinf(p->yaw));
                wish = v3add(wish, v3mul(rt, drift));
                float wl2 = v3len(wish);
                if (wl2 > 1.0f) wish = v3mul(wish, 1.0f / wl2);
            }
        }
    }
    if (p->dodge_timer > 0) { accel = 0; max_speed = 18.0f; }
    if (veh_type < 0) max_speed *= game_player_speed_mult(p);
    Vec3 hv = v3(p->vel.x, 0, p->vel.z);
    bool ground_friction = p->grounded && veh_type < 0 && p->dodge_timer <= 0.0f;
    /* Ground friction: momentum that is not in the wish direction is bled
     * off. Previously velocity was only damped when there was no input at
     * all, so changing direction kept the old sideways momentum and the
     * player appeared to "surf" across the floor. */
    if (ground_friction) {
        float spd = v3len(hv);
        if (spd > 0.001f) {
            float drop = fmaxf(spd, 2.0f) * 7.5f * dt;
            float ns = fmaxf(0.0f, spd - drop);
            float sc = ns / spd;
            p->vel.x *= sc;
            p->vel.z *= sc;
            hv = v3(p->vel.x, 0, p->vel.z);
        }
    }
    if (wl > 0.05f) {
        float cur = v3dot(hv, wish);
        float add = clampf(max_speed - cur, 0, accel * dt * max_speed);
        p->vel.x += wish.x * add;
        p->vel.z += wish.z * add;
        /* Also scrub the component perpendicular to the wish direction so
         * strafing turns the velocity instead of sliding sideways. */
        if (ground_friction) {
            Vec3 side = v3(-wish.z, 0, wish.x);
            float lat = v3dot(p->vel, side);
            float kill = clampf(12.0f * dt, 0.0f, 1.0f);
            p->vel.x -= side.x * lat * kill;
            p->vel.z -= side.z * lat * kill;
        }
    } else if (p->grounded && veh_type < 0) {
        float f = fmaxf(0.0f, 1.0f - 12.0f * dt);
        p->vel.x *= f;
        p->vel.z *= f;
        if (fabsf(p->vel.x) < 0.02f) p->vel.x = 0;
        if (fabsf(p->vel.z) < 0.02f) p->vel.z = 0;
    }
    if (veh_type == 1) {
        /* Manta: cheap hover, no aerodynamic simulation. Jump climbs,
         * crouch descends, otherwise it holds altitude. */
        p->vel.y = 0.0f;
        if (p->input.jump) p->vel.y = 9.5f;
        else if (p->input.crouch) p->vel.y = -8.5f;
        if (p->pos.y < 1.4f) p->vel.y = fmaxf(p->vel.y, 2.0f);
    } else {
        p->vel.y -= (g->gravity > 1.0f ? g->gravity : 24.0f) * dt;
        if (p->vel.y < -55.0f) p->vel.y = -55.0f;
    }

    bool grounded = p->grounded, hit_wall = false;
    int ga = -1;
    Vec3 pre_move = p->pos;
    /* Sub-step long moves: at terminal velocity a single frame could skip a
     * thin platform or tunnel through a wall. 0.25 m per step keeps every
     * collision thick enough to catch. */
    {
        float travel = (fabsf(p->vel.x) + fabsf(p->vel.z) + fabsf(p->vel.y)) * dt;
        int subs = (int)ceilf(travel / 0.25f);
        if (subs < 1) subs = 1;
        if (subs > 10) subs = 10;
        float sdt = dt / (float)subs;
        bool wall_any = false;
        for (int s = 0; s < subs; s++) {
            bool step_wall = false;
            world_move_box(g->world, &p->pos, &p->vel, sdt, 0.40f, player_height(p),
                           &grounded, &step_wall, &ga);
            wall_any = wall_any || step_wall;
        }
        hit_wall = wall_any;
    }
    if (getenv("A2K4_COMBATLOG") && (fabsf(p->pos.x) > 40.0f || fabsf(p->pos.z) > 40.0f) && fabsf(pre_move.x) <= 40.0f && fabsf(pre_move.z) <= 40.0f) {
        fprintf(stderr, "[bounds] %s from %.1f,%.1f,%.1f to %.1f,%.1f,%.1f vel %.1f,%.1f,%.1f pad=%d\n",
                p->name, pre_move.x, pre_move.y, pre_move.z, p->pos.x, p->pos.y, p->pos.z,
                p->vel.x, p->vel.y, p->vel.z, p->grounded);
    }
    p->grounded = grounded;
    p->hit_wall_last_frame = hit_wall;
    /* If the player keeps pushing into geometry without moving at all, flag
     * it for the F3 overlay and auto-unstuck. */
    if (wl > 0.2f) {
        float dx = p->pos.x - pre_move.x, dz = p->pos.z - pre_move.z;
        bool blocked = (dx * dx + dz * dz) < 0.000001f;
        if (blocked && !grounded) p->stuck_last_frame = true;
        else p->stuck_last_frame = false;
    } else {
        p->stuck_last_frame = false;
    }
    if (grounded) {
        p->jump_count = 0;
        if (!was_grounded) {
            p->land_kick = fminf(0.18f + fabsf(p->vel.y) * 0.01f, 1.0f);
            if (p->id == g->local_player && fabsf(p->vel.y) > 3) audio_play(SFX_LAND, 0.4f, 0);
        }
    }
    /* jump pads */
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_JUMPPAD) continue;
        float dx = p->pos.x - a->pos.x, dz = p->pos.z - a->pos.z;
        if (dx * dx + dz * dz < 1.7f * 1.7f && fabsf(p->pos.y - a->pos.y) < 0.7f && p->vel.y <= 3.0f) {
            float yaw2 = a->yaw * DEG2RAD;
            Vec3 dir = v3(sinf(yaw2), 0, -cosf(yaw2));
            p->vel = v3add(v3mul(dir, a->power), v3(0, a->extra, 0));
            p->grounded = false;
            if (p->id == g->local_player) audio_play(SFX_JUMPPAD, 0.7f, 0);
            for (int k = 0; k < 18; k++) {
                Vec3 d = v3norm(v3(frandr(-1, 1), frandr(0, 1), frandr(-1, 1)));
                game_add_particle(g, a->pos, v3mul(d, frandr(2, 6)), a->color, frandr(0.12f, 0.3f), frandr(0.3f, 0.7f), true, 0, 2);
            }
        }
    }
    /* lava */
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_LAVA) continue;
        if (p->pos.x > a->pos.x - a->size.x * 0.5f && p->pos.x < a->pos.x + a->size.x * 0.5f &&
            p->pos.z > a->pos.z - a->size.z * 0.5f && p->pos.z < a->pos.z + a->size.z * 0.5f &&
            p->pos.y < a->pos.y + a->size.y * 0.5f + 0.6f) {
            game_damage(g, p, 42.0f * dt, -1, v3(0, 1, 0), p->pos, WPN_COUNT);
            p->vel.y = fmaxf(p->vel.y, 2.0f);
            if (frand() < dt * 12) {
                for (int k = 0; k < 3; k++)
                    game_add_particle(g, p->pos, v3(frandr(-1, 1), frandr(2, 5), frandr(-1, 1)), col3(1.0f, 0.4f, 0.05f), frandr(0.15f, 0.4f), 0.4f, true, 3, 1);
            }
            if (p->pos.y < -7.0f) game_damage(g, p, 200.0f, -1, v3(0, 1, 0), p->pos, WPN_COUNT);
        }
    }
    /* footsteps + bob */
    float spd = v3len(v3(p->vel.x, 0, p->vel.z));
    p->bob += spd * dt * 1.35f;
    if (p->grounded && spd > 3.2f) {
        float prev = p->bob - spd * dt * 1.35f;
        if (floorf(p->bob / PI) != floorf(prev / PI) && p->id == g->local_player) audio_play(SFX_STEP, 0.16f, frandr(-0.2f, 0.2f));
    }
    /* arena bounds: never leave the playable box even if a collision edge misbehaves */
    if (p->pos.x < -38.0f) { p->pos.x = -38.0f; p->vel.x = fmaxf(p->vel.x, 0); }
    if (p->pos.x >  38.0f) { p->pos.x =  38.0f; p->vel.x = fminf(p->vel.x, 0); }
    if (p->pos.z < -38.0f) { p->pos.z = -38.0f; p->vel.z = fmaxf(p->vel.z, 0); }
    if (p->pos.z >  38.0f) { p->pos.z =  38.0f; p->vel.z = fminf(p->vel.z, 0); }
    if (p->pos.y > 14.8f) { p->pos.y = 14.8f; p->vel.y = fminf(p->vel.y, 0); }
    /* sanity */
    if (p->pos.y < -20.0f) game_damage(g, p, 500.0f, -1, v3(0, 1, 0), p->pos, WPN_COUNT);
    p->yaw = p->input.yaw;
    p->pitch = clampf(p->input.pitch, -1.45f, 1.45f);
    if (p->build_mode) {
        if (p->input.fire && !p->fire_latch) game_build_attempt(g, p);
        if (p->input.alt_fire && !p->alt_latch) game_build_rotate(p);
    } else {
        if (p->input.fire && !p->fire_latch) game_fire(g, p, false);
        if (p->input.alt_fire && !p->alt_latch) game_fire(g, p, true);
    }
    p->fire_latch = p->input.fire;
    p->alt_latch = p->input.alt_fire;
}

/* ------------------------------------------------------------------ */
/* nav graph                                                           */
/* ------------------------------------------------------------------ */
void game_nav_nearest_impl(Game* g, Vec3 pos, float maxdist, int* out, float* bestd);
int game_nav_nearest(Game* g, Vec3 pos, float maxdist) {
    int best = -1; float bd = maxdist * maxdist;
    for (int i = 0; i < g->nav.count; i++) {
        Vec3 d = v3sub(g->nav.nodes[i].pos, pos);
        float dd = v3dot(d, d);
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}
void game_nav_nearest_impl(Game* g, Vec3 pos, float maxdist, int* out, float* bestd) {
    int best = -1; float bd = maxdist * maxdist;
    for (int i = 0; i < g->nav.count; i++) {
        Vec3 d = v3sub(g->nav.nodes[i].pos, pos);
        float dd = v3dot(d, d);
        if (dd < bd) { bd = dd; best = i; }
    }
    if (out) *out = best;
    if (bestd) *bestd = sqrtf(bd);
}

/* True when the straight segment between two nav nodes is safely walkable:
 * it must not cross a lava volume and there must be floor below its samples. */
static bool nav_link_ok(Game* g, Vec3 a, Vec3 b) {
    for (int k = 1; k <= 4; k++) {
        float t = (float)k / 5.0f;
        Vec3 p = v3add(a, v3mul(v3sub(b, a), t));
        for (int i = 0; i < g->world->count; i++) {
            Actor* act = &g->world->actors[i];
            if (act->type != ACT_LAVA) continue;
            if (p.x > act->pos.x - act->size.x * 0.5f && p.x < act->pos.x + act->size.x * 0.5f &&
                p.z > act->pos.z - act->size.z * 0.5f && p.z < act->pos.z + act->size.z * 0.5f &&
                p.y < act->pos.y + act->size.y * 0.5f + 2.0f) return false;
        }
        Ray r; r.origin = v3add(p, v3(0, 1.0f, 0)); r.dir = v3(0, -1, 0);
        RayHit h;
        if (!world_raycast(g->world, r, 5.0f, true, &h)) return false;
        if (h.normal.y < 0.4f) return false;
    }
    return true;
}

/* Wall-free + walkable check used by link building and path smoothing. */
static bool nav_segment_ok(Game* g, Vec3 a, Vec3 b) {
    Vec3 d = v3sub(b, a);
    float dist = v3len(d);
    if (dist < 0.05f) return true;
    Vec3 dir = v3mul(d, 1.0f / dist);
    Ray r; r.origin = v3add(a, v3(0, 0.3f, 0)); r.dir = dir;
    RayHit h;
    if (world_raycast(g->world, r, dist - 0.35f, true, &h)) return false;
    return nav_link_ok(g, a, b);
}

static float nav_heuristic(Game* g, int a, int b) {
    return v3len(v3sub(g->nav.nodes[b].pos, g->nav.nodes[a].pos));
}

/* A* over the nav graph, then string-pull the result so bots do not zig-zag
 * from grid node to grid node. */
bool game_nav_path(Game* g, int start, int end, int* out, int* outlen) {
    if (start < 0 || end < 0 || start >= g->nav.count || end >= g->nav.count) return false;
    if (start == end) { out[0] = start; *outlen = 1; return true; }

    static int prev[MAX_NAV];
    static float gscore[MAX_NAV], fscore[MAX_NAV];
    static bool open[MAX_NAV], closed[MAX_NAV];
    for (int i = 0; i < g->nav.count; i++) {
        prev[i] = -1; gscore[i] = 1e30f; fscore[i] = 1e30f;
        open[i] = false; closed[i] = false;
    }
    gscore[start] = 0;
    fscore[start] = nav_heuristic(g, start, end);
    open[start] = true;
    int open_count = 1;

    for (int it = 0; it < g->nav.count && open_count > 0; it++) {
        int u = -1; float best = 1e30f;
        for (int i = 0; i < g->nav.count; i++)
            if (open[i] && fscore[i] < best) { best = fscore[i]; u = i; }
        if (u < 0) break;
        if (u == end) break;
        open[u] = false; open_count--;
        closed[u] = true;
        for (int k = 0; k < g->nav.nodes[u].nn; k++) {
            int v = g->nav.nodes[u].nb[k];
            if (v < 0 || v >= g->nav.count || closed[v]) continue;
            float w = v3len(v3sub(g->nav.nodes[u].pos, g->nav.nodes[v].pos));
            float tentative = gscore[u] + w;
            if (tentative < gscore[v]) {
                prev[v] = u;
                gscore[v] = tentative;
                fscore[v] = tentative + nav_heuristic(g, v, end);
                if (!open[v]) { open[v] = true; open_count++; }
            }
        }
    }
    if (gscore[end] >= 1e29f) return false;

    int raw[MAX_PATH]; int n = 0; int cur = end;
    while (cur >= 0 && n < MAX_PATH) { raw[n++] = cur; cur = prev[cur]; }
    int tmp[MAX_PATH];
    for (int i = 0; i < n; i++) tmp[i] = raw[n - 1 - i];

    int m = 0, i = 0;
    while (i < n && m < MAX_PATH) {
        out[m++] = tmp[i];
        if (i == n - 1) break;
        int j = n - 1;
        while (j > i + 1) {
            if (nav_segment_ok(g, g->nav.nodes[tmp[i]].pos, g->nav.nodes[tmp[j]].pos)) break;
            j--;
        }
        i = (j > i + 1) ? j : i + 1;
    }
    *outlen = m;
    return true;
}

/* Bounds and grid resolution come from the map itself, so editor-made maps
 * and stacked floors work without hand-placed path nodes. */
static void nav_world_bounds(Game* g, float* minx, float* maxx, float* minz, float* maxz,
                             float* miny, float* maxy) {
    float ax = 1e9f, bx = -1e9f, az = 1e9f, bz = -1e9f, ay = 1e9f, by = -1e9f;
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type == ACT_LIGHT || a->type == ACT_TRIGGER) continue;
        AABB bb = world_actor_aabb(g->world, a);
        if (bb.min.x < ax) ax = bb.min.x;
        if (bb.max.x > bx) bx = bb.max.x;
        if (bb.min.z < az) az = bb.min.z;
        if (bb.max.z > bz) bz = bb.max.z;
        if (bb.min.y < ay) ay = bb.min.y;
        if (bb.max.y > by) by = bb.max.y;
    }
    if (ax > bx - 1.0f || az > bz - 1.0f) {
        ax = -36.0f; bx = 36.0f; az = -36.0f; bz = 36.0f;
        ay = -4.0f; by = 14.0f;
    }
    /* keep very large editor maps bounded; the grid step grows instead */
    if (bx - ax > 400.0f) { float c = (ax + bx) * 0.5f; ax = c - 200.0f; bx = c + 200.0f; }
    if (bz - az > 400.0f) { float c = (az + bz) * 0.5f; az = c - 200.0f; bz = c + 200.0f; }
    *minx = ax - 2.0f; *maxx = bx + 2.0f;
    *minz = az - 2.0f; *maxz = bz + 2.0f;
    *miny = ay - 4.0f; *maxy = by + 1.5f;
}

static int nav_add_node(Game* g, Vec3 pos, float dedupe) {
    if (g->nav.count >= MAX_NAV) return -1;
    for (int k = 0; k < g->nav.count; k++) {
        Vec3 d = v3sub(g->nav.nodes[k].pos, pos);
        if (v3dot(d, d) < dedupe * dedupe) return k;
    }
    int idx = g->nav.count++;
    NavNode* n = &g->nav.nodes[idx];
    memset(n, 0, sizeof(*n));
    n->pos = pos;
    n->pad_landing = -1;
    return idx;
}

/* Make room for a jump-pad edge on a fully linked grid node. */
static bool nav_drop_farthest(Game* g, int idx) {
    NavNode* a = &g->nav.nodes[idx];
    if (a->nn <= 0) return false;
    int worst = 0; float wd = -1.0f;
    for (int k = 0; k < a->nn; k++) {
        float d = v3len(v3sub(g->nav.nodes[a->nb[k]].pos, a->pos));
        if (d > wd) { wd = d; worst = k; }
    }
    int v = a->nb[worst];
    for (int k = worst; k < a->nn - 1; k++) a->nb[k] = a->nb[k + 1];
    a->nn--;
    NavNode* b = &g->nav.nodes[v];
    for (int k = 0; k < b->nn; k++) {
        if (b->nb[k] == idx) {
            for (int j = k; j < b->nn - 1; j++) b->nb[j] = b->nb[j + 1];
            b->nn--;
            break;
        }
    }
    return true;
}

/* One-way links from jump pads to the first nav node along their arc. */
static void nav_link_jumppads(Game* g) {
    for (int i = 0; i < g->world->count; i++) {
        Actor* pad = &g->world->actors[i];
        if (pad->type != ACT_JUMPPAD) continue;
        int src = game_nav_nearest(g, v3(pad->pos.x, pad->pos.y + 0.4f, pad->pos.z), 4.0f);
        if (src < 0) continue;
        float yaw = pad->yaw * DEG2RAD;
        Vec3 dir = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 p = g->nav.nodes[src].pos;
        Vec3 vel = v3add(v3mul(dir, pad->power), v3(0, pad->extra, 0));
        int dst = -1;
        for (int s = 1; s <= 80; s++) {
            float t = (float)s * 0.05f;
            Vec3 q = v3add(v3add(p, v3mul(vel, t)), v3(0, -0.5f * g->gravity * t * t, 0));
            float dx = q.x - p.x, dz = q.z - p.z;
            if (t > 0.30f && dx * dx + dz * dz > 16.0f) {
                int n = game_nav_nearest(g, q, 2.8f);
                if (n >= 0 && n != src) { dst = n; break; }
            }
            if (q.y < p.y - 6.0f) break;
        }
        if (dst < 0) continue;
        bool already = false;
        for (int k = 0; k < g->nav.nodes[src].nn; k++)
            if (g->nav.nodes[src].nb[k] == dst) { already = true; break; }
        if (!already) {
            if (g->nav.nodes[src].nn >= MAX_NAV_LINKS) nav_drop_farthest(g, src);
            if (g->nav.nodes[src].nn < MAX_NAV_LINKS) {
                g->nav.nodes[src].nb[g->nav.nodes[src].nn++] = dst;
                g->nav.nodes[src].pad_landing = dst;
            }
        }
    }
}

static bool nav_has_link(Game* g, int i, int j) {
    NavNode* a = &g->nav.nodes[i];
    for (int k = 0; k < a->nn; k++) if (a->nb[k] == j) return true;
    return false;
}

/* Add a bidirectional edge, making room on already saturated nodes. */
static bool nav_add_link(Game* g, int i, int j) {
    if (i < 0 || j < 0 || i == j) return false;
    if (i >= g->nav.count || j >= g->nav.count) return false;
    if (nav_has_link(g, i, j)) return false;
    if (g->nav.nodes[i].nn >= MAX_NAV_LINKS) nav_drop_farthest(g, i);
    if (g->nav.nodes[j].nn >= MAX_NAV_LINKS) nav_drop_farthest(g, j);
    if (g->nav.nodes[i].nn >= MAX_NAV_LINKS || g->nav.nodes[j].nn >= MAX_NAV_LINKS) return false;
    g->nav.nodes[i].nb[g->nav.nodes[i].nn++] = j;
    g->nav.nodes[j].nb[g->nav.nodes[j].nn++] = i;
    return true;
}

/* Authored nav points (ACT_NAV_POINT) are UT-style hints. They snap to the
 * floor below them; a hint that cannot find ground is ignored so a misplaced
 * point never creates an unreachable floating node. */
static void nav_add_hint_nodes(Game* g) {
    for (int i = 0; i < g->world->count && g->nav.count < MAX_NAV; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_NAV_POINT) continue;
        Ray r; r.origin = v3add(a->pos, v3(0, 0.6f, 0)); r.dir = v3(0, -1, 0);
        RayHit h;
        if (!world_raycast(g->world, r, 8.0f, true, &h)) continue;
        if (h.normal.y < 0.5f) continue;
        int idx = nav_add_node(g, v3add(h.point, v3(0, 0.95f, 0)), 1.4f);
        if (idx >= 0) g->nav.nodes[idx].hint = 1;
    }
}

/* Bridge hint nodes with each other and anchor each one to the automatic
 * graph. The hint radius is deliberately larger than the grid link radius:
 * mappers use these points exactly where the raw geometry scan is too coarse
 * (stairs, ledges, narrow doorways). */
static void nav_link_hints(Game* g) {
    if (g->nav.count < 2) return;

    for (int i = 0; i < g->nav.count; i++) {
        NavNode* a = &g->nav.nodes[i];
        if (!a->hint) continue;
        for (int j = i + 1; j < g->nav.count; j++) {
            NavNode* b = &g->nav.nodes[j];
            if (!b->hint) continue;
            float dist = v3len(v3sub(b->pos, a->pos));
            if (dist > 28.0f) continue;
            if (!nav_segment_ok(g, a->pos, b->pos)) continue;
            nav_add_link(g, i, j);
        }
    }

    for (int i = 0; i < g->nav.count; i++) {
        NavNode* a = &g->nav.nodes[i];
        if (!a->hint) continue;
        bool anchored = false;
        for (int k = 0; k < a->nn && !anchored; k++) {
            int j = a->nb[k];
            if (j >= 0 && j < g->nav.count && !g->nav.nodes[j].hint) anchored = true;
        }
        if (anchored) continue;
        int best = -1; float bestd = 1e30f;
        for (int j = 0; j < g->nav.count; j++) {
            NavNode* b = &g->nav.nodes[j];
            if (b->hint) continue;
            Vec3 d = v3sub(b->pos, a->pos);
            float dist = v3len(d);
            if (dist > 18.0f || dist >= bestd) continue;
            if (!nav_segment_ok(g, a->pos, b->pos)) continue;
            best = j; bestd = dist;
        }
        if (best >= 0) nav_add_link(g, i, best);
    }
}

/* Keep the largest walkable island plus any island that contains an authored
 * hint. Roof/pillar tops and other decorative surfaces that no player can
 * reach would only attract bots into dead ends, but a hint island is
 * intentional and must survive pruning. */
static void nav_prune_islands(Game* g) {
    int n = g->nav.count;
    if (n <= 0) return;
    static int parent[MAX_NAV], size_[MAX_NAV], remap[MAX_NAV];
    static bool keep[MAX_NAV];
    for (int i = 0; i < n; i++) { parent[i] = i; size_[i] = 1; remap[i] = -1; keep[i] = false; }
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < g->nav.nodes[i].nn; k++) {
            int j = g->nav.nodes[i].nb[k];
            if (j < 0 || j >= n) continue;
            int a = i, b = j;
            while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
            while (parent[b] != b) { parent[b] = parent[parent[b]]; b = parent[b]; }
            if (a != b) {
                if (size_[a] < size_[b]) { int t = a; a = b; b = t; }
                parent[b] = a;
                size_[a] += size_[b];
            }
        }
    }
    int best = 0, bestn = 0;
    for (int i = 0; i < n; i++) {
        int r = i;
        while (parent[r] != r) r = parent[r];
        if (size_[r] > bestn) { bestn = size_[r]; best = r; }
    }
    if (bestn <= 0) return;
    keep[best] = true;
    for (int i = 0; i < n; i++) {
        if (!g->nav.nodes[i].hint) continue;
        int r = i;
        while (parent[r] != r) r = parent[r];
        keep[r] = true;
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        int r = i;
        while (parent[r] != r) r = parent[r];
        if (!keep[r]) continue;
        remap[i] = m;
        if (m != i) g->nav.nodes[m] = g->nav.nodes[i];
        m++;
    }
    for (int i = 0; i < m; i++) {
        NavNode* node = &g->nav.nodes[i];
        int nn = 0;
        for (int k = 0; k < node->nn; k++) {
            int old = node->nb[k];
            if (old >= 0 && old < n && remap[old] >= 0) node->nb[nn++] = remap[old];
        }
        node->nn = nn;
        if (node->pad_landing >= 0 && node->pad_landing < n)
            node->pad_landing = remap[node->pad_landing];
        else
            node->pad_landing = -1;
    }
    g->nav.count = m;
}

void game_build_nav(Game* g) {
    g->nav.count = 0;
    g->nav.built = 1;

    float minx, maxx, minz, maxz, miny, maxy;
    nav_world_bounds(g, &minx, &maxx, &minz, &maxz, &miny, &maxy);
    float area = (maxx - minx) * (maxz - minz);
    float step = sqrtf(area / 900.0f);
    if (step < 2.5f) step = 2.5f;
    if (step > 7.0f) step = 7.0f;
    float dedupe = fmaxf(1.6f, step * 0.55f);

    /* Scan each column from the top down, so catwalks and upper floors are
     * found as well as the main deck. */
    for (float x = minx; x <= maxx && g->nav.count < MAX_NAV; x += step) {
        for (float z = minz; z <= maxz && g->nav.count < MAX_NAV; z += step) {
            float y = maxy;
            for (int level = 0; level < 12 && g->nav.count < MAX_NAV; level++) {
                Vec3 o = v3(x, y, z);
                Ray r; r.origin = o; r.dir = v3(0, -1, 0);
                RayHit h;
                float reach = (y - miny) + 2.0f;
                if (reach < 0.5f) break;
                if (!world_raycast(g->world, r, reach, true, &h)) break;
                float fy = h.point.y;
                if (h.normal.y >= 0.55f && fy > miny - 1.0f) {
                    AABB box = aabb_center_size(v3(x, fy + 1.0f, z), v3(1.3f, 2.0f, 1.3f));
                    if (!world_overlap_box(g->world, box, -1))
                        nav_add_node(g, v3(x, fy + 0.95f, z), dedupe);
                }
                y = fy - 1.4f;
                if (y < miny) break;
            }
        }
    }

    /* optional authored hints join the grid before linking */
    nav_add_hint_nodes(g);

    /* links: short, walkable, wall-free segments */
    float linkdist = fmaxf(step * 2.2f, 8.0f);
    if (linkdist > 14.0f) linkdist = 14.0f;
    float linky = fmaxf(2.6f, step * 0.8f);
    for (int i = 0; i < g->nav.count; i++) {
        NavNode* a = &g->nav.nodes[i];
        for (int j = i + 1; j < g->nav.count; j++) {
            NavNode* b = &g->nav.nodes[j];
            if (a->nn >= MAX_NAV_LINKS || b->nn >= MAX_NAV_LINKS) continue;
            Vec3 d = v3sub(b->pos, a->pos);
            float dist = v3len(d);
            if (dist > linkdist) continue;
            if (fabsf(d.y) > linky) continue;
            if (!nav_segment_ok(g, a->pos, b->pos)) continue;
            a->nb[a->nn++] = j;
            b->nb[b->nn++] = i;
        }
    }

    nav_link_hints(g);
    nav_link_jumppads(g);
    nav_prune_islands(g);

    /* fallback: spawn nodes if the map has too little walkable geometry */
    if (g->nav.count < 12) {
        for (int i = 0; i < g->world->count && g->nav.count < MAX_NAV; i++) {
            Actor* a = &g->world->actors[i];
            if (a->type != ACT_PLAYER_START) continue;
            NavNode* n = &g->nav.nodes[g->nav.count];
            memset(n, 0, sizeof(*n));
            n->pos = v3add(a->pos, v3(0, 0.95f, 0));
            n->pad_landing = -1;
            if (g->nav.count > 0) {
                int p = g->nav.count - 1;
                n->nb[n->nn++] = p; g->nav.nodes[p].nb[g->nav.nodes[p].nn++] = g->nav.count;
            }
            g->nav.count++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* bot AI                                                              */
/* ------------------------------------------------------------------ */
static int bot_pick_weapon(Game* g, Player* p, float dist) {
    (void)g;
    int best = WPN_AR;
    float bestscore = -1;
    for (int i = 0; i < WPN_COUNT; i++) {
        if (!p->has[i] || p->ammo[i] <= 0) continue;
        float score = 1.0f;
        if (i == WPN_SHOCK) score = dist < 30 ? 3.0f : 2.0f;
        else if (i == WPN_ROCKET) score = dist < 25 ? 3.4f : 1.6f;
        else if (i == WPN_FLAK) score = dist < 16 ? 3.2f : 1.2f;
        else if (i == WPN_LIGHTNING) score = dist < 45 ? 2.8f : 1.4f;
        else if (i == WPN_AR) score = 1.5f;
        score += frandr(-0.2f, 0.2f);
        if (score > bestscore) { bestscore = score; best = i; }
    }
    return best;
}

void game_demo_input(Game* g, float dt) {
    (void)g; (void)dt;
}

/* Altis Life civilians: bots wander between POIs, never fight, and let the
 * player do the jobs. Kept separate from bot_think so combat AI never turns
 * the roleplay map into a deathmatch. */
static void bot_altis_civilian(Game* g, Player* p, float dt) {
    if (!p->alive) return;
    p->input.fire = false;
    p->input.alt_fire = false;
    p->input.use = false;
    p->bot_think_t -= dt;
    if (p->bot_think_t > 0) return;
    p->bot_think_t = frandr(0.6f, 1.8f);

    /* pick a random POI to stroll towards when idle or arrived */
    Vec3 target;
    if (g->life_point_count <= 0) {
        target = v3(0, p->pos.y, 0);
    } else {
        if (p->bot_target < 0 || p->bot_target >= g->life_point_count ||
            v3len(v3sub(p->pos, g->life_points[p->bot_target].pos)) < 5.0f) {
            p->bot_target = rand() % g->life_point_count;
        }
        target = g->life_points[p->bot_target].pos;
    }
    float dx = target.x - p->pos.x, dz = target.z - p->pos.z;
    float d = sqrtf(dx * dx + dz * dz);
    if (d > 3.0f) {
        float want = atan2f(dx, -dz);
        float diff = want - p->yaw;
        while (diff > PI) diff -= 2.0f * (float)PI;
        while (diff < -PI) diff += 2.0f * (float)PI;
        p->yaw += clampf(diff, -2.2f * dt, 2.2f * dt);
        p->input.move_z = 0.42f;   /* civilians stroll, they do not sprint */
        p->input.move_x = 0.0f;
        if (fabsf(diff) > 0.7f) p->input.move_z = 0.15f;
    } else {
        p->input.move_z = 0.0f;
        p->input.move_x = 0.0f;
    }
    if (fabsf(p->pitch) > 0.1f) p->pitch *= 0.9f;
}

static void bot_think(Game* g, Player* p, float dt) {
    if (!p->alive) return;
    p->bot_think_t -= dt;
    if (p->bot_think_t > 0) return;
    p->bot_think_t = frandr(0.08f, 0.16f);

    float skill = 0.35f + g->difficulty * 0.14f + (p->id == g->local_player && g->demo_mode ? 0.05f : 0);
    skill = clamp01(skill);

    /* target selection */
    Player* target = NULL;
    float bestscore = -1e9f;
    for (int i = 0; i < g->num_players; i++) {
        if (i == p->id) continue;
        Player* o = &g->players[i];
        if (!o->active || !o->alive) continue;
        if (o->protection > 0) continue;
        if (g->game_mode == GM_INVASION && !o->zombie) continue;
        if (game_mode_team_based(g->game_mode) && p->team >= 0 && o->team == p->team) continue;
        float ddx = o->pos.x - p->pos.x, ddy = o->pos.y - p->pos.y, ddz = o->pos.z - p->pos.z;
        if (ddx * ddx + ddy * ddy + ddz * ddz > 95.0f * 95.0f) continue;
        float dist; bool vis = player_is_visible(g, p, o, &dist);
        float score = (vis ? 100.0f : 20.0f) - dist * 0.8f + o->health * 0.02f;
        if (o->id == p->bot_target) score += 25.0f;
        if (score > bestscore) { bestscore = score; target = o; }
    }
    if (target) p->bot_target = target->id; else p->bot_target = -1;

    /* weapon prefer */
    float tdist = target ? v3len(v3sub(target->pos, p->pos)) : 100.0f;
    p->bot_weapon_pref = bot_pick_weapon(g, p, tdist);
    if (p->weapon != p->bot_weapon_pref && p->fire_timer <= 0) p->weapon = p->bot_weapon_pref;

    /* vehicles: board a free team vehicle when not in close combat, then
     * drive toward the objective. Only 16 vehicles exist, so only nearby
     * bots ever look for a ride. */
    int board_vehicle = -1;
    bool vehicle_map = (g->game_mode == GM_VCTF || g->game_mode == GM_ONSLAUGHT ||
                        g->game_mode == GM_CONQUEST) && g->vehicle_count > 0 &&
                       (!g->networked || g->server_mode);
    if (vehicle_map) {
        p->bot_vehicle_t -= dt;
        if (p->in_vehicle) {
            p->bot_vehicle_target = -1;
        } else {
            if (p->bot_vehicle_target >= 0 && p->bot_vehicle_target < g->vehicle_count) {
                Vehicle* bv = &g->vehicles[p->bot_vehicle_target];
                if (!bv->active || bv->driver >= 0) p->bot_vehicle_target = -1;
            }
            if (p->bot_vehicle_target < 0 && p->bot_vehicle_t <= 0 && (!target || tdist > 18.0f)) {
                p->bot_vehicle_t = frandr(2.0f, 4.0f);
                float best = 60.0f * 60.0f;
                for (int i = 0; i < g->vehicle_count; i++) {
                    Vehicle* v = &g->vehicles[i];
                    if (!v->active || v->driver >= 0) continue;
                    if (v->team >= 0 && p->team >= 0 && v->team != p->team) continue;
                    float dx = v->pos.x - p->pos.x, dz = v->pos.z - p->pos.z;
                    float d = dx * dx + dz * dz;
                    if (d < best) { best = d; p->bot_vehicle_target = i; }
                }
            }
            if (p->bot_vehicle_target >= 0 && p->bot_vehicle_target < g->vehicle_count) {
                Vehicle* bv = &g->vehicles[p->bot_vehicle_target];
                float dx = bv->pos.x - p->pos.x, dz = bv->pos.z - p->pos.z;
                if (dx * dx + dz * dz < 6.0f * 6.0f) {
                    p->input.use = true;          /* update_vehicle_use runs next frame */
                    p->bot_vehicle_target = -1;
                    p->bot_vehicle_t = frandr(3.0f, 5.0f);
                } else {
                    board_vehicle = p->bot_vehicle_target;
                }
            }
        }
    }

    /* navigation goal */
    Vec3 goal = p->pos;
    bool have_goal = false;
    if (target) { goal = target->pos; have_goal = true; }
    else {
        bool picked = false;
        if (g->game_mode == GM_BATTLE_ROYALE) {
            float dx = p->pos.x - g->zone_center_x, dz = p->pos.z - g->zone_center_z;
            float safe = g->zone_radius * 0.70f;
            if (dx * dx + dz * dz > safe * safe) {
                goal = v3(g->zone_center_x, p->pos.y, g->zone_center_z);
                picked = true;
            }
        }
        if (!picked && (g->game_mode == GM_CTF || g->game_mode == GM_VCTF) && p->team >= 0) {
            Flag* own = &g->flags[p->team];
            Flag* foe = &g->flags[1 - p->team];
            if (own->carried && own->carrier >= 0 && own->carrier < g->num_players) {
                goal = g->players[own->carrier].pos;
            } else {
                goal = foe->pos;
            }
            picked = true;
        }
        if (!picked && g->node_count > 0 && p->team >= 0) {
            float best = 1e9f;
            for (int i = 0; i < g->node_count; i++) {
                ControlNode* n = &g->nodes[i];
                if (!n->active || n->team == p->team) continue;
                float d = v3len(v3sub(n->pos, p->pos));
                if (d < best) { best = d; goal = n->pos; picked = true; }
            }
        }
        if (!picked) {
            /* roam toward nearest pickup */
            float best = 1e9f;
            for (int i = 0; i < g->pickup_actor_count; i++) {
                int idx = g->pickup_actors[i];
                if (idx < 0 || idx >= g->world->count) continue;
                Actor* a = &g->world->actors[idx];
                if (!g->pickups[idx].active) continue;
                float d = v3len(v3sub(a->pos, p->pos));
                if (d < best) { best = d; goal = a->pos; }
            }
        }
        have_goal = true;
    }
    if (board_vehicle >= 0 && board_vehicle < g->vehicle_count) {
        goal = g->vehicles[board_vehicle].pos;
        have_goal = true;
    }

    p->bot_repath -= dt;
    if (have_goal && p->bot_repath <= 0) {
        p->bot_repath = frandr(0.45f, 0.9f);
        int s = game_nav_nearest(g, p->pos, 8.0f);
        int e = game_nav_nearest(g, goal, 12.0f);
        bool found = false;
        if (s >= 0 && e >= 0 && s != e) {
            int path[MAX_PATH]; int n = 0;
            if (game_nav_path(g, s, e, path, &n)) {
                p->bot_path_len = n; p->bot_path_idx = 0;
                for (int k = 0; k < n && k < MAX_PATH; k++) p->bot_path[k] = path[k];
                found = true;
            }
        } else if (s >= 0 && e == s) {
            p->bot_path_len = 1; p->bot_path_idx = 0; p->bot_path[0] = s;
            found = true;
        }
        if (!found) {
            /* No route (e.g. target across the lava pit): roam to a reachable
             * nav node instead of walking straight into the hazard. */
            p->bot_path_len = 0;
            if (s >= 0 && g->nav.count > 2) {
                int pick = (p->id * 37 + (int)(g->elapsed * 0.7f) * 13) % g->nav.count;
                int path[MAX_PATH]; int n = 0;
                if (pick != s && game_nav_path(g, s, pick, path, &n)) {
                    p->bot_path_len = n; p->bot_path_idx = 0;
                    for (int k = 0; k < n && k < MAX_PATH; k++) p->bot_path[k] = path[k];
                }
            }
        }
    }

    Vec3 move_target = p->pos;
    bool have_move = false;
    if (p->bot_path_len > 0) {
        int node = p->bot_path[p->bot_path_idx];
        if (node >= 0 && node < g->nav.count) {
            move_target = g->nav.nodes[node].pos;
            if (v3len(v3sub(v3(p->pos.x, 0, p->pos.z), v3(move_target.x, 0, move_target.z))) < 1.4f) {
                p->bot_path_idx++;
                if (p->bot_path_idx >= p->bot_path_len) { p->bot_path_len = 0; p->bot_path_idx = 0; }
            }
            have_move = true;
        }
    }
    /* Fallback: no nav path? Steer directly toward the goal. The movement
     * collision code will stop the bot at walls, so it won't run through them. */
    if (!have_move && have_goal) { move_target = goal; have_move = true; }
    if (!have_move) { move_target = p->pos; have_move = true; }

    /* Boarding: nav nodes can end several units away from a parked vehicle.
     * Close the last metres with direct steering so bots actually reach it. */
    if (board_vehicle >= 0 && board_vehicle < g->vehicle_count) {
        Vehicle* bv = &g->vehicles[board_vehicle];
        float dx = bv->pos.x - p->pos.x, dz = bv->pos.z - p->pos.z;
        if (dx * dx + dz * dz < 16.0f * 16.0f) {
            move_target = bv->pos;
            have_move = true;
        }
    }

    /* edge probe: never walk straight into lava or off a ledge */
    bool edge_ahead = false;
    {
        Vec3 dd = v3sub(move_target, p->pos);
        Vec3 flat = v3(dd.x, 0, dd.z);
        if (v3len(flat) > 0.1f) {
            Vec3 f = v3norm(flat);
            Vec3 probe = v3add(p->pos, v3mul(f, 2.2f));
            bool floor_ok = false;
            for (int k = 0; k < g->world->count; k++) {
                Actor* act = &g->world->actors[k];
                if (act->type != ACT_LAVA) continue;
                if (probe.x > act->pos.x - act->size.x * 0.5f && probe.x < act->pos.x + act->size.x * 0.5f &&
                    probe.z > act->pos.z - act->size.z * 0.5f && probe.z < act->pos.z + act->size.z * 0.5f) { edge_ahead = true; break; }
            }
            if (!edge_ahead) {
                Ray r; r.origin = v3add(probe, v3(0, 1.6f, 0)); r.dir = v3(0, -1, 0);
                RayHit h;
                if (world_raycast(g->world, r, 5.0f, true, &h) && h.normal.y > 0.55f) floor_ok = true;
                if (!floor_ok) edge_ahead = true;
            }
        }
    }

    /* desired yaw toward move target, but aim at enemy when visible */
    float move_yaw = p->yaw;
    if (have_move) {
        Vec3 d = v3sub(move_target, p->pos);
        if (v3len(d) > 0.3f) move_yaw = atan2f(d.x, -d.z);
    }
    float aim_yaw = move_yaw, aim_pitch = 0;
    bool visible = false;
    float dist = 0;
    if (target) {
        visible = player_is_visible(g, p, target, &dist);
        if (visible) {
            Vec3 eye = player_eye(p);
            Vec3 te = player_eye(target);
            /* lead target */
            float proj_speed = weapon_defs[p->weapon].speed;
            float lead = 0;
            if (proj_speed > 1) lead = dist / proj_speed;
            Vec3 aim = v3add(te, v3mul(target->vel, lead * 0.9f));
            Vec3 d = v3sub(aim, eye);
            angles_from_dir(d, &aim_yaw, &aim_pitch);
            float err = (1.0f - skill) * 0.10f + 0.012f;
            aim_yaw += sinf(g->elapsed * 2.1f + p->id) * err * 1.6f;
            aim_pitch += cosf(g->elapsed * 1.7f + p->id * 2) * err;
        } else {
            Vec3 d = v3sub(target->pos, p->pos);
            move_yaw = atan2f(d.x, -d.z);
        }
    }
    /* smooth turn */
    float turn = 0.24f + skill * 0.52f;
    p->yaw = angle_lerp(p->yaw, aim_yaw, clamp01(turn));
    p->pitch = lerpf(p->pitch, aim_pitch, clamp01(turn));

    /* strafe + forward */
    float fwd = 0, strafe = 0;
    if (have_move) {
        Vec3 d = v3sub(move_target, p->pos);
        Vec3 f = v3(sinf(move_yaw), 0, -cosf(move_yaw));
        Vec3 r = v3(cosf(move_yaw), 0, sinf(move_yaw));
        float dd = v3len(d);
        float dotf = v3dot(v3norm(v3(d.x, 0, d.z)), f);
        float dotr = v3dot(v3norm(v3(d.x, 0, d.z)), r);
        fwd = clampf(dotf, -1, 1);
        strafe = clampf(dotr, -1, 1);
        if (dd < 2.0f && !visible) fwd *= 0.3f;
    }
    p->bot_strafe_timer -= dt;
    if (p->bot_strafe_timer <= 0) {
        p->bot_strafe_timer = frandr(0.5f, 1.8f);
        p->bot_strafe = frand() < 0.35f ? 0 : (frand() < 0.5f ? -1 : 1);
    }
    if (visible && target) {
        strafe = p->bot_strafe * (0.5f + skill * 0.5f);
        if (dist < 10.0f) fwd = -0.5f;
        else if (dist > 30.0f) fwd = 0.7f;
        else fwd *= 0.3f;
        /* dodge randomly */
        if (!edge_ahead && frand() < 0.004f + (1.0f - skill) * 0.003f) p->input.dodge = true;
    }
    if (edge_ahead) { fwd = -0.65f; strafe = p->id & 1 ? 0.8f : -0.8f; }

    /* Probe the direction the bot will actually move in (aim direction when
     * fighting) and refuse to step over lava or off a ledge. */
    {
        Vec3 f = v3(sinf(p->yaw), 0, -cosf(p->yaw));
        Vec3 r = v3(cosf(p->yaw), 0, sinf(p->yaw));
        Vec3 wish = v3add(v3mul(f, fwd), v3mul(r, strafe));
        float wl = v3len(wish);
        if (wl > 0.05f) {
            Vec3 dir = v3mul(wish, 1.0f / wl);
            Vec3 probe = v3add(p->pos, v3mul(dir, 2.1f));
            bool safe = true;
            for (int k = 0; k < g->world->count; k++) {
                Actor* act = &g->world->actors[k];
                if (act->type != ACT_LAVA) continue;
                if (probe.x > act->pos.x - act->size.x * 0.5f && probe.x < act->pos.x + act->size.x * 0.5f &&
                    probe.z > act->pos.z - act->size.z * 0.5f && probe.z < act->pos.z + act->size.z * 0.5f) { safe = false; break; }
            }
            if (safe) {
                Ray pr; pr.origin = v3add(probe, v3(0, 1.4f, 0)); pr.dir = v3(0, -1, 0);
                RayHit ph;
                if (!world_raycast(g->world, pr, 4.2f, true, &ph) || ph.normal.y < 0.5f) safe = false;
            }
            if (!safe) { fwd = 0; strafe = (p->id & 1) ? 0.7f : -0.7f; }
        }
    }
    p->input.move_x = strafe;
    p->input.move_z = fwd;

    /* jump over obstacles / randomness */
    if (!edge_ahead && frand() < 0.03f + (1.0f - skill) * 0.02f) p->input.jump = true;
    else p->input.jump = false;
    if (!edge_ahead && frand() < 0.02f) p->input.dodge = true;
    if (p->in_vehicle) { p->input.jump = false; p->input.dodge = false; p->input.crouch = false; }

    /* fire: bots call game_fire directly so fast weapons are not limited to
     * the edge-triggered input latch. */
    bool want_fire = false;
    if (target && visible && p->weapon >= 0 && p->has[p->weapon] && p->ammo[p->weapon] > 0) {
        float maxr = 90.0f;
        if (p->weapon == WPN_FLAK) maxr = 22.0f;
        if (p->weapon == WPN_ROCKET) maxr = 65.0f;
        if (dist < maxr) {
            float aimdot = v3dot(v3(sinf(p->yaw) * cosf(p->pitch), sinf(p->pitch), -cosf(p->yaw) * cosf(p->pitch)),
                                 v3norm(v3sub(player_eye(target), player_eye(p))));
            float need = 0.94f - (1.0f - skill) * 0.30f;
            if (aimdot > need) {
                want_fire = true;
                if (p->weapon == WPN_ROCKET && dist < 7.0f) want_fire = false;
            }
        }
    }
    p->input.fire = false;
    if (want_fire && p->fire_timer <= 0 && p->ammo[p->weapon] > 0) {
        bool alt = false;
        if (p->weapon == WPN_ROCKET && dist > 24.0f && frand() < 0.28f) alt = true;
        if (p->weapon == WPN_SHOCK && frand() < 0.30f) alt = true;
        if (p->weapon == WPN_FLAK && dist < 12.0f && frand() < 0.35f) alt = true;
        game_fire(g, p, alt);
    }
    /* vehicle weapons do not use player ammo */
    if (p->in_vehicle && target && visible && p->vehicle >= 0 && p->vehicle < g->vehicle_count) {
        Vehicle* v = &g->vehicles[p->vehicle];
        if (v->active && v->fire_timer <= 0.0f && dist < 95.0f) {
            Vec3 aim = v3(sinf(p->yaw) * cosf(p->pitch), sinf(p->pitch), -cosf(p->yaw) * cosf(p->pitch));
            Vec3 to = v3norm(v3sub(player_eye(target), player_eye(p)));
            float need = v->type == 2 ? 0.95f : 0.90f;
            if (v3dot(aim, to) > need) game_fire(g, p, false);
        }
    }
    /* reload / switch when empty */
    if (p->ammo[p->weapon] <= 0) {
        for (int i = 0; i < WPN_COUNT; i++) {
            if (p->has[i] && p->ammo[i] > 0) { p->weapon = i; break; }
        }
    }
    if (!target) { p->bot_target = -1; }
    p->input.yaw = p->yaw;
    p->input.pitch = p->pitch;
}

/* Cheap AI for the abstract part of very large matches. It keeps the
 * population moving and visible without per-frame pathfinding/raycasts. */
static void bot_think_lite(Game* g, Player* p, float dt) {
    if (!p->alive) return;
    p->bot_think_t -= dt;
    if (p->bot_think_t > 0) return;
    p->bot_think_t = frandr(0.30f, 0.75f);
    Vec3 goal = p->pos;
    if (g->game_mode == GM_BATTLE_ROYALE) {
        float dx = p->pos.x - g->zone_center_x, dz = p->pos.z - g->zone_center_z;
        if (dx * dx + dz * dz > (g->zone_radius * 0.65f) * (g->zone_radius * 0.65f))
            goal = v3(g->zone_center_x, p->pos.y, g->zone_center_z);
    }
    if (goal.x == p->pos.x && goal.z == p->pos.z && g->nav.count > 0) {
        int node = (p->id * 31 + (int)(g->elapsed * 0.08f)) % g->nav.count;
        goal = g->nav.nodes[node].pos;
    }
    Vec3 d = v3sub(goal, p->pos);
    if (v3len(v3(d.x, 0, d.z)) > 1.0f) {
        float yaw = atan2f(d.x, -d.z);
        p->yaw = angle_lerp(p->yaw, yaw, 0.12f);
        p->pitch = 0;
    }
    p->input.move_z = 0.75f;
    p->input.move_x = 0;
    p->input.use = false;
    p->input.fire = false;
    p->input.alt_fire = false;
    p->input.jump = false;
    p->input.dodge = false;
    p->input.yaw = p->yaw;
    p->input.pitch = p->pitch;
}

/* ------------------------------------------------------------------ */
/* update                                                              */
/* ------------------------------------------------------------------ */
static void game_push_event(Game* g, const char* cmd) {
    if (!cmd || !cmd[0]) return;
    if (g->event_count >= (int)(sizeof(g->events) / sizeof(g->events[0]))) return;
    str_copy(g->events[g->event_count++], cmd, sizeof(g->events[0]));
}

/* Trigger volumes execute their stored console event. extra > 0 means repeat. */
static void update_triggers(Game* g, float dt) {
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) return;
    Player* p = &g->players[li];
    if (!p->active || !p->alive) return;
    AABB pb;
    pb.min = v3(p->pos.x - 0.42f, p->pos.y, p->pos.z - 0.42f);
    pb.max = v3(p->pos.x + 0.42f, p->pos.y + player_height(p), p->pos.z + 0.42f);
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_TRIGGER) continue;
        bool inside = aabb_overlap(pb, world_actor_aabb(g->world, a));
        if (!inside) { g->trigger_state[i] = 0; continue; }
        if (g->trigger_state[i] == 0) {
            game_push_event(g, a->name);
            g->trigger_state[i] = 1;
            g->trigger_timer[i] = a->extra > 0.0f ? a->extra : a->respawn;
        } else if (a->extra > 0.0f) {
            g->trigger_timer[i] -= dt;
            if (g->trigger_timer[i] <= 0.0f) {
                game_push_event(g, a->name);
                g->trigger_timer[i] = a->extra;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* CTF flags                                                           */
/* ------------------------------------------------------------------ */
static void flag_return(Flag* f) {
    f->carried = false;
    f->dropped = false;
    f->carrier = -1;
    f->pos = f->home;
    f->return_timer = 0;
}

static void update_flags(Game* g, float dt) {
    if (g->game_mode != GM_CTF && g->game_mode != GM_VCTF) return;
    for (int t = 0; t < 2; t++) {
        Flag* f = &g->flags[t];
        if (!f->active) continue;
        f->bob += dt;
        if (f->carried) {
            int ci = f->carrier;
            bool ok = ci >= 0 && ci < g->num_players && g->players[ci].active && g->players[ci].alive;
            if (!ok) {
                if (ci >= 0 && ci < g->num_players) {
                    f->pos = v3(g->players[ci].pos.x, g->players[ci].pos.y + 0.5f, g->players[ci].pos.z);
                    f->return_timer = 20.0f;
                }
                f->carried = false;
                f->dropped = true;
                f->carrier = -1;
                continue;
            }
            Player* c = &g->players[ci];
            f->pos = v3(c->pos.x, c->pos.y + 1.55f, c->pos.z);
            Vec3 own = g->team_base[c->team >= 0 ? c->team : 0];
            float dx = c->pos.x - own.x, dz = c->pos.z - own.z;
            if (dx * dx + dz * dz < 4.8f * 4.8f && !g->flags[c->team].carried) {
                g->team_score[c->team]++;
                char msg[96];
                snprintf(msg, sizeof(msg), "%s TEAM CAPTURES THE FLAG", c->team == 0 ? "RED" : "BLUE");
                game_announce(g, msg, team_base_color(c->team), 2.6f);
                game_add_killfeed(g, "%s captured the flag", c->name);
                flag_return(f);
                if (g->team_score[c->team] >= (int)g->score_limit)
                    finish_match_team(g, c->team, "flag captures");
            }
        } else {
            if (f->dropped) {
                f->return_timer -= dt;
                if (f->return_timer <= 0) flag_return(f);
            }
            for (int i = 0; i < g->num_players; i++) {
                Player* p = &g->players[i];
                if (!p->active || !p->alive || p->team < 0) continue;
                float dx = p->pos.x - f->pos.x, dz = p->pos.z - f->pos.z;
                float dy = p->pos.y - f->pos.y;
                if (dx * dx + dz * dz > 2.4f * 2.4f || fabsf(dy) > 3.0f) continue;
                if (p->team != f->team) {
                    f->carried = true;
                    f->dropped = false;
                    f->carrier = i;
                    char msg[96];
                    snprintf(msg, sizeof(msg), "%s FLAG TAKEN", p->team == 0 ? "RED" : "BLUE");
                    game_announce(g, msg, team_base_color(p->team), 1.8f);
                    break;
                }
                if (f->dropped) { flag_return(f); break; }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Conquest / Onslaught / Assault control nodes                        */
/* ------------------------------------------------------------------ */
static void update_nodes(Game* g, float dt) {
    if (g->node_count <= 0) return;
    bool assault = g->game_mode == GM_ASSAULT;
    for (int i = 0; i < g->node_count; i++) {
        ControlNode* n = &g->nodes[i];
        if (!n->active) continue;
        n->pulse += dt;
        int cnt[2] = { 0, 0 };
        float r2 = n->radius * n->radius;
        for (int j = 0; j < g->num_players; j++) {
            Player* p = &g->players[j];
            if (!p->active || !p->alive || p->team < 0) continue;
            float dx = p->pos.x - n->pos.x, dz = p->pos.z - n->pos.z;
            if (dx * dx + dz * dz < r2) cnt[p->team]++;
        }
        int cap = -1;
        if (cnt[0] > 0 && cnt[1] == 0) cap = 0;
        else if (cnt[1] > 0 && cnt[0] == 0) cap = 1;
        if (cap >= 0 && cap != n->team) {
            if (n->attacker != cap) { n->attacker = cap; n->progress = 0; }
            n->progress += dt * (0.22f + 0.09f * (float)cnt[cap]);
            if (n->progress >= 1.0f) {
                n->progress = 1.0f;
                if (assault) {
                    g->team_score[cap] = (int)g->score_limit + 1;
                    finish_match_team(g, cap, "objective destroyed");
                } else {
                    n->team = cap;
                    n->attacker = -1;
                    n->progress = 0;
                    char msg[96];
                    snprintf(msg, sizeof(msg), "%s captured %s", cap == 0 ? "RED" : "BLUE", n->name);
                    game_announce(g, msg, team_base_color(cap), 2.0f);
                }
            }
        } else {
            if (n->progress > 0) n->progress = fmaxf(0.0f, n->progress - dt * 0.18f);
            if (cnt[0] == 0 && cnt[1] == 0) n->attacker = -1;
        }
    }
    if (assault) return;
    g->node_tick += dt;
    if (g->node_tick >= 1.0f) {
        g->node_tick -= 1.0f;
        int gain = (g->game_mode == GM_ONSLAUGHT) ? 2 : 1;
        for (int i = 0; i < g->node_count; i++) {
            ControlNode* n = &g->nodes[i];
            if (!n->active || n->team < 0) continue;
            g->team_score[n->team] += gain;
        }
        for (int t = 0; t < 2; t++) {
            if (g->team_score[t] >= (int)g->score_limit)
                finish_match_team(g, t, g->game_mode == GM_ONSLAUGHT ? "power core" : "tickets");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Vehicles (VCTF)                                                     */
/* ------------------------------------------------------------------ */
static void update_vehicles(Game* g, float dt) {
    Player* lp = (g->local_player >= 0 && g->local_player < g->num_players) ? &g->players[g->local_player] : NULL;
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (v->fire_timer > 0.0f) v->fire_timer -= dt;
        if (v->hit_flash > 0.0f) v->hit_flash = fmaxf(0.0f, v->hit_flash - dt * 3.0f);
        if (!v->active) {
            v->respawn -= dt;
            /* wreck smoke: a few puffs per second, only near the local player */
            if (v->respawn > 0.0f && lp) {
                float wdx = v->pos.x - lp->pos.x, wdz = v->pos.z - lp->pos.z;
                if (wdx * wdx + wdz * wdz < 90.0f * 90.0f) {
                    v->smoke -= dt;
                    if (v->smoke <= 0.0f) {
                        v->smoke = 0.34f;
                        Vec3 wp = v3(v->pos.x + frandr(-0.6f, 0.6f), v->pos.y + 0.7f,
                                     v->pos.z + frandr(-0.6f, 0.6f));
                        game_add_particle(g, wp,
                                          v3(frandr(-0.4f, 0.4f), frandr(1.4f, 2.8f), frandr(-0.4f, 0.4f)),
                                          col(0.12f, 0.115f, 0.11f, 0.55f),
                                          frandr(0.7f, 1.4f), frandr(1.0f, 1.9f), false, 0.3f, 1.0f);
                    }
                }
            }
            /* Only the host (or offline game) respawns a wreck. Clients wait
             * for the authoritative vehicle packet so the timer is identical
             * for every player. */
            if (v->respawn <= 0.0f && (!g->networked || g->server_mode)) {
                v->active = true;
                v->pos = v->home;
                v->hp = v->max_hp;
                v->driver = -1;
                v->killer = -1;
                v->pitch = 0.0f;
                v->fire_timer = 0.0f;
                v->hit_flash = 0.0f;
                v->smoke = 0.0f;
                for (int k = 0; k < VEH_MODULES; k++) {
                    v->modules[k].hp = v->modules[k].max_hp;
                    v->modules[k].destroyed = false;
                }
            }
            continue;
        }
        v->bob += dt;
        if (v->driver >= 0) {
            Player* p = (v->driver < g->num_players) ? &g->players[v->driver] : NULL;
            if (!p || !p->active || !p->alive || !p->in_vehicle || p->vehicle != i) {
                v->driver = -1;
            } else {
                v->pos = p->pos;
                v->yaw = p->yaw;
                v->pitch = p->pitch * 0.45f;
                if (v->type == 1) v->pitch += clampf(-p->vel.y * 0.025f, -0.25f, 0.25f);
            }
        } else {
            /* parked vehicles slowly repair so a damaged motor pool does not
             * become useless; no collision/physics work while empty */
            if (v->hp < v->max_hp) v->hp = fminf(v->max_hp, v->hp + 9.0f * dt);
        }
        /* damage smoke: cheap, only for nearby vehicles and only a few
         * particles per second. A destroyed engine or wheel smokes too. */
        bool mod_broken = v->modules[VEH_ENGINE].destroyed;
        for (int k = 0; k < VEH_WHEELS; k++) if (v->modules[k].destroyed) mod_broken = true;
        if ((v->hp < v->max_hp * 0.42f || mod_broken) && lp) {
            float dx = v->pos.x - lp->pos.x, dz = v->pos.z - lp->pos.z;
            if (dx * dx + dz * dz < 80.0f * 80.0f) {
                v->smoke -= dt;
                if (v->smoke <= 0.0f) {
                    v->smoke = v->modules[VEH_ENGINE].destroyed ? 0.14f : 0.22f;
                    Vec3 pnt = v3(v->pos.x + frandr(-0.7f, 0.7f), v->pos.y + 0.8f, v->pos.z + frandr(-0.7f, 0.7f));
                    game_add_particle(g, pnt, v3(frandr(-0.4f, 0.4f), frandr(1.2f, 2.4f), frandr(-0.4f, 0.4f)),
                                      col(0.18f, 0.17f, 0.16f, 0.55f), frandr(0.5f, 1.1f), frandr(0.8f, 1.4f), false, 0.4f, 1.0f);
                }
            }
        }
    }
}

static void vehicle_exit(Game* g, Player* p) {
    int vi = p->vehicle;
    if (vi >= 0 && vi < g->vehicle_count) {
        Vehicle* v = &g->vehicles[vi];
        v->driver = -1;
        v->pos = p->pos;
        v->yaw = p->yaw;
    }
    p->in_vehicle = false;
    p->vehicle = -1;
    p->vel = v3(0, 0, 0);
    p->grounded = false;
    p->use_latch = true;
    /* step out to the side instead of standing inside the hull */
    Vec3 right = v3(cosf(p->yaw), 0, sinf(p->yaw));
    p->pos = v3add(p->pos, v3mul(right, 2.1f));
    p->pos.x = clampf(p->pos.x, -37.0f, 37.0f);
    p->pos.z = clampf(p->pos.z, -37.0f, 37.0f);
    (void)g;
}

static void update_vehicle_use(Game* g, Player* p) {
    if (!p->input.use) { p->use_latch = false; return; }
    if (p->use_latch) return;
    p->use_latch = true;
    if (p->in_vehicle) {
        vehicle_exit(g, p);
        if (p->id == g->local_player) game_pickup_msg(g, "Vehicle left");
        return;
    }
    int best = -1;
    float bd = 12.0f;
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active || v->driver >= 0) continue;
        float dx = v->pos.x - p->pos.x, dz = v->pos.z - p->pos.z;
        float dy = v->pos.y - p->pos.y;
        float d = dx * dx + dz * dz;
        if (d < bd && fabsf(dy) < 4.0f) { bd = d; best = i; }
    }
    if (best >= 0) {
        Vehicle* v = &g->vehicles[best];
        v->driver = p->id;
        p->in_vehicle = true;
        p->vehicle = best;
        p->pos = v->pos;
        p->vel = v3(0, 0, 0);
        if (v->type == 1) p->pos.y = fmaxf(v->pos.y, v->home.y);
        if (p->id == g->local_player) {
            game_pickup_msg(g, "%s ready - E to exit, fire = %s", game_vehicle_name(v->type),
                            v->type == 2 ? "cannon" : "plasma");
            audio_play(SFX_PICKUP_WEAPON, 0.5f, 0);
        }
        return;
    }
    /* No vehicle nearby: E is the world interaction key. Altis POIs take
     * priority, then harvesting nature props for building materials. */
    if (game_life_interact(g, p)) return;
    if (game_harvest(g, p)) return;
}

/* ------------------------------------------------------------------ */
/* Zombie Invasion (Back4Blood / Unreal Invasion inspired)             */
/* ------------------------------------------------------------------ */
const char* game_zombie_name(int type) {
    switch (type) {
    case 1: return "RUNNER";
    case 2: return "BRUTE";
    case 3: return "SPITTER";
    default: return "WALKER";
    }
}

static void zombie_stats(int type, int wave, float* hp, float* speed, float* dmg, float* cd) {
    int w = wave < 1 ? 1 : wave;
    switch (type) {
    case 1: *hp = 45.0f + (float)w * 5.0f;  *speed = 0.95f; *dmg = 7.0f;  *cd = 0.75f; break;
    case 2: *hp = 300.0f + (float)w * 34.0f; *speed = 0.48f; *dmg = 26.0f; *cd = 1.45f; break;
    case 3: *hp = 75.0f + (float)w * 8.0f;   *speed = 0.55f; *dmg = 12.0f; *cd = 2.10f; break;
    default:*hp = 70.0f + (float)w * 7.0f;   *speed = 0.62f; *dmg = 10.0f; *cd = 1.10f; break;
    }
}

static Vec3 zombie_spawn_pos(Game* g, Vec3 near) {
    Vec3 c; float ext;
    map_bounds(g->world, &c, &ext);
    Vec3 best = near;
    for (int tries = 0; tries < 10; tries++) {
        float ang = frand() * 2.0f * PI;
        float rad = frandr(16.0f, 34.0f);
        Vec3 p = v3(near.x + cosf(ang) * rad, near.y + 7.0f, near.z + sinf(ang) * rad);
        p.x = clampf(p.x, c.x - ext * 0.92f, c.x + ext * 0.92f);
        p.z = clampf(p.z, c.z - ext * 0.92f, c.z + ext * 0.92f);
        Ray down; down.origin = v3(p.x, near.y + 24.0f, p.z); down.dir = v3(0, -1, 0);
        RayHit h;
        if (world_raycast(g->world, down, 60.0f, true, &h) && h.normal.y > 0.45f) {
            best = v3(p.x, h.point.y + 0.12f, p.z);
            if (v3len(v3sub(best, near)) > 8.0f) return best;
        }
    }
    return v3(near.x + frandr(-12, 12), near.y + 0.2f, near.z + frandr(-12, 12));
}

int game_spawn_zombie(Game* g, int type, Vec3 near) {
    if (!g || g->game_mode != GM_INVASION) return -1;
    if (type < 0 || type > 3) type = 0;
    int limit = g->networked ? 32 : MAX_PLAYERS;
    if (g->num_players >= limit) {
        int free_slot = -1;
        for (int i = g->invasion_survivors; i < g->num_players; i++) {
            if (!g->players[i].active) { free_slot = i; break; }
        }
        if (free_slot < 0) return -1;
    }
    int slot = -1;
    for (int i = g->invasion_survivors; i < g->num_players; i++) {
        if (!g->players[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        if (g->num_players >= limit) return -1;
        slot = g->num_players++;
    }
    Player* p = &g->players[slot];
    init_player_slot(g, p, slot);
    p->zombie = true;
    p->zombie_type = type;
    p->bot = true;
    p->team = -1;
    p->protection = 0.0f;
    p->weapon = WPN_AR;
    p->has[WPN_AR] = false;
    p->ammo[WPN_AR] = 0;
    p->pos = zombie_spawn_pos(g, near);
    p->render_pos = p->pos;
    Vec3 d = v3sub(near, p->pos);
    p->yaw = atan2f(d.x, -d.z);
    p->render_yaw = p->yaw;
    p->vel = v3(0, 0, 0);
    float hp, spd, dmg, cd;
    zombie_stats(type, g->invasion_wave, &hp, &spd, &dmg, &cd);
    p->health = hp;
    p->armor = 0.0f;
    p->zombie_attack = frandr(0.2f, 1.0f);
    p->zombie_growl = frandr(0.5f, 3.0f);
    p->bot_think_t = frandr(0.0f, 0.2f);
    (void)spd; (void)dmg; (void)cd;
    static const Color zc[4] = {
        {0.34f, 0.44f, 0.24f, 1}, {0.55f, 0.62f, 0.32f, 1},
        {0.42f, 0.28f, 0.20f, 1}, {0.35f, 0.72f, 0.30f, 1}
    };
    p->color = zc[type];
    char nm[32];
    snprintf(nm, sizeof(nm), "%s-%d", game_zombie_name(type), slot);
    str_copy(p->name, nm, sizeof(p->name));
    return slot;
}

int game_horde(Game* g, int count) {
    if (!g || g->game_mode != GM_INVASION || count < 1) return 0;
    int spawned = 0;
    int survivors = g->invasion_survivors > 0 ? g->invasion_survivors : g->num_players;
    for (int k = 0; k < count; k++) {
        int t = 0;
        float r = frand();
        if (g->invasion_wave >= 6 && r < 0.18f) t = 3;
        else if (g->invasion_wave >= 4 && r < 0.34f) t = 2;
        else if (g->invasion_wave >= 2 && r < 0.62f) t = 1;
        Vec3 near = g->team_base[0];
        for (int tries = 0; tries < 8; tries++) {
            int s = rand() % (survivors > 0 ? survivors : 1);
            if (g->players[s].active && !g->players[s].zombie) { near = g->players[s].pos; break; }
        }
        if (game_spawn_zombie(g, t, near) < 0) break;
        spawned++;
    }
    g->invasion_spawn_left = 0;
    return spawned;
}

static void invasion_start_wave(Game* g);

void game_next_wave(Game* g) {
    if (!g || g->game_mode != GM_INVASION) return;
    /* Force the next wave: clear the field, then run the normal director. */
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->active && p->zombie) { p->active = false; p->alive = false; }
    }
    g->invasion_spawn_left = 0;
    g->invasion_alive = 0;
    invasion_start_wave(g);
}

int game_invasion_survivors_alive(Game* g) {
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < g->num_players; i++)
        if (g->players[i].active && g->players[i].alive && !g->players[i].zombie) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Battle Royale zone                                                  */
/* ------------------------------------------------------------------ */
static void update_zone(Game* g, float dt) {
    if (g->game_mode != GM_BATTLE_ROYALE) return;
    g->br_alive = game_alive_count(g);
    g->zone_timer -= dt;
    if (g->zone_timer <= 0.0f && g->zone_stage < 6) {
        g->zone_stage++;
        g->zone_timer = 45.0f;
        g->zone_target = fmaxf(16.0f, g->zone_target * 0.66f);
        game_announce(g, "THE ZONE IS CLOSING", col3(1.0f, 0.45f, 0.25f), 2.4f);
    }
    if (g->zone_radius > g->zone_target) {
        float shrink = (g->zone_radius - g->zone_target) * 0.22f + 0.35f;
        g->zone_radius = fmaxf(g->zone_target, g->zone_radius - shrink * dt * 4.0f);
    }
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active || !p->alive) continue;
        float dx = p->pos.x - g->zone_center_x, dz = p->pos.z - g->zone_center_z;
        if (dx * dx + dz * dz > g->zone_radius * g->zone_radius) {
            Vec3 dir = v3(g->zone_center_x - p->pos.x, 0, g->zone_center_z - p->pos.z);
            game_damage(g, p, 18.0f * dt, -1, dir, p->pos, WPN_AR);
        }
    }
    if (!g->match_over && g->started && g->match_time > 1.0f && g->br_alive <= 1) {
        int winner = -1;
        for (int i = 0; i < g->num_players; i++)
            if (g->players[i].active && g->players[i].alive) { winner = i; break; }
        g->match_over = true;
        g->winner = winner;
        snprintf(g->result, sizeof(g->result), "%s WINS THE ROYALE", winner >= 0 ? g->players[winner].name : "NOBODY");
        game_announce(g, g->result, col3(1.0f, 0.85f, 0.25f), 5.0f);
    }
}

static void zombie_think(Game* g, Player* p, float dt, bool lite) {
    if (!p || !p->alive || !p->zombie) return;
    if (p->zombie_attack > 0.0f) p->zombie_attack -= dt;
    p->zombie_growl -= dt;
    p->bot_think_t -= dt;
    if (p->bot_think_t > 0.0f) return;
    p->bot_think_t = lite ? frandr(0.42f, 0.85f) : frandr(0.12f, 0.28f);

    float hp, spd, dmg, cd;
    zombie_stats(p->zombie_type, g->invasion_wave, &hp, &spd, &dmg, &cd);
    (void)hp;

    int survivors = g->invasion_survivors > 0 ? g->invasion_survivors : g->num_players;
    if (survivors > g->num_players) survivors = g->num_players;
    Player* target = NULL;
    float best = 1e18f;
    for (int i = 0; i < survivors; i++) {
        Player* o = &g->players[i];
        if (!o->active || !o->alive || o->zombie) continue;
        float dx = o->pos.x - p->pos.x, dy = o->pos.y - p->pos.y, dz = o->pos.z - p->pos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best) { best = d2; target = o; }
    }
    if (!target) {
        p->input.move_z = 0;
        p->input.move_x = 0;
        return;
    }
    Vec3 d = v3sub(target->pos, p->pos);
    float dist = sqrtf(d.x * d.x + d.z * d.z);
    float want = atan2f(d.x, -d.z);
    p->yaw = angle_lerp(p->yaw, want, lite ? 0.18f : 0.38f);
    float fwd = clampf((dist - 1.5f) / 5.0f, 0.12f, 1.0f) * spd;
    p->input.move_z = fwd;
    p->input.move_x = sinf(g->time * 1.7f + (float)p->id * 0.91f) * 0.18f;
    p->input.jump = false;
    p->input.crouch = false;
    p->input.fire = false;
    p->input.dodge = false;
    p->input.yaw = p->yaw;
    p->input.pitch = 0.0f;

    if (dist < 2.35f && p->zombie_attack <= 0.0f) {
        Vec3 hit = v3(target->pos.x, target->pos.y + 1.0f, target->pos.z);
        game_damage(g, target, dmg, p->id, v3norm(d), hit, WPN_FLAK);
        p->zombie_attack = cd;
        for (int k = 0; k < 5; k++) {
            Vec3 v = v3norm(v3(frandr(-1, 1), frandr(0.0f, 1.0f), frandr(-1, 1)));
            game_add_particle(g, hit, v3mul(v, frandr(1.0f, 3.0f)),
                              p->zombie_type == 3 ? col3(0.5f, 1.0f, 0.3f) : col3(0.65f, 0.12f, 0.08f),
                              frandr(0.08f, 0.2f), frandr(0.2f, 0.5f), true, 4.0f, 1.2f);
        }
    } else if (!lite && p->zombie_type == 3 && p->zombie_attack <= 0.0f &&
               dist > 4.0f && dist < 34.0f) {
        /* spitter: one cheap acid projectile, no complex ballistics */
        Vec3 eye = v3(p->pos.x, p->pos.y + 1.35f, p->pos.z);
        Vec3 aim = v3(target->pos.x, target->pos.y + 1.0f, target->pos.z);
        Vec3 dir = v3norm(v3sub(aim, eye));
        Ray r; r.origin = eye; r.dir = dir;
        RayHit h;
        if (!world_raycast(g->world, r, dist, true, &h) || h.t > dist - 0.8f) {
            for (int i = 0; i < MAX_PROJECTILES; i++) {
                Projectile* pr = &g->projs[i];
                if (pr->active) continue;
                memset(pr, 0, sizeof(*pr));
                pr->active = true;
                pr->type = WPN_SHOCK;
                pr->owner = p->id;
                pr->pos = eye;
                pr->vel = v3mul(dir, 24.0f);
                pr->life = 3.0f;
                pr->damage = dmg;
                pr->splash = 2.5f;
                pr->radius = 2.6f;
                pr->gravity = 3.0f;
                pr->color = col3(0.45f, 1.0f, 0.28f);
                p->zombie_attack = cd;
                break;
            }
        }
    }
    if (p->zombie_growl <= 0.0f) {
        p->zombie_growl = frandr(3.0f, 7.0f);
        Player* lp = (g->local_player >= 0 && g->local_player < g->num_players) ? &g->players[g->local_player] : NULL;
        if (lp && v3len(v3sub(lp->pos, p->pos)) < 26.0f) audio_play_at(SFX_HIT, 0.25f, p->pos.x, p->pos.y, p->pos.z,
                                                                       lp->pos.x, lp->pos.y, lp->pos.z, sinf(lp->yaw), -cosf(lp->yaw));
    }
}

static void invasion_start_wave(Game* g) {
    g->invasion_wave++;
    int survivors = g->invasion_survivors > 0 ? g->invasion_survivors : g->num_players;
    int count = 6 + g->invasion_wave * 5 + (survivors - 1) * 4;
    int free_slots = MAX_PLAYERS - g->num_players;
    for (int i = g->invasion_survivors; i < g->num_players; i++)
        if (!g->players[i].active) free_slots++;
    if (g->networked) {
        int room = 32 - survivors;
        if (room < 0) room = 0;
        if (free_slots > room) free_slots = room;
    }
    if (count > free_slots) count = free_slots;
    if (count < 0) count = 0;
    g->invasion_spawn_left = count;
    g->invasion_spawn_timer = 0.0f;
    g->invasion_alive = 0;
    g->invasion_wave_timer = 0.0f;
    char b[96];
    snprintf(b, sizeof(b), "WAVE %d / %d  -  %d HOSTILES", g->invasion_wave, g->invasion_wave_max, count);
    game_announce(g, b, col3(0.55f, 1.0f, 0.32f), 3.2f);
    if (g->invasion_wave > g->invasion_best_wave) g->invasion_best_wave = g->invasion_wave;
}

static void update_invasion(Game* g, float dt) {
    if (g->game_mode != GM_INVASION) return;
    if (g->networked && !g->server_mode) {
        int alive = 0;
        for (int i = 0; i < g->num_players; i++)
            if (g->players[i].active && g->players[i].alive && g->players[i].zombie) alive++;
        g->invasion_alive = alive;
        return;
    }
    if (g->match_over) return;
    int alive = 0;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (p->active && p->alive && p->zombie) alive++;
    }
    g->invasion_alive = alive;
    int survivors_alive = game_alive_count(g);
    if (g->started && g->match_time > 1.0f && survivors_alive <= 0) {
        g->match_over = true;
        g->invasion_victory = false;
        g->winner = -1;
        snprintf(g->result, sizeof(g->result), "OVERRUN ON WAVE %d", g->invasion_wave > 0 ? g->invasion_wave : 1);
        game_announce(g, "SURVIVORS OVERRUN", col3(1.0f, 0.22f, 0.16f), 5.0f);
        return;
    }
    if (g->invasion_spawn_left > 0) {
        g->invasion_spawn_timer -= dt;
        float interval = g->invasion_wave <= 2 ? 0.45f : 0.24f;
        if (g->invasion_wave >= 6) interval = 0.12f;
        if (g->num_players > 200) interval = 0.055f;
        int guard = 0;
        while (g->invasion_spawn_left > 0 && g->invasion_spawn_timer <= 0.0f && guard++ < 64) {
            int t = 0;
            float r = frand();
            if (g->invasion_wave >= 6 && r < 0.16f) t = 3;
            else if (g->invasion_wave >= 4 && r < 0.30f) t = 2;
            else if (g->invasion_wave >= 2 && r < 0.62f) t = 1;
            int s = rand() % (g->invasion_survivors > 0 ? g->invasion_survivors : 1);
            if (!g->players[s].active || g->players[s].zombie) {
                s = 0;
                for (int k = 0; k < g->invasion_survivors; k++)
                    if (g->players[k].active && !g->players[k].zombie) { s = k; break; }
            }
            if (game_spawn_zombie(g, t, g->players[s].pos) < 0) break;
            g->invasion_spawn_left--;
            g->invasion_spawn_timer += interval;
        }
    } else if (alive == 0) {
        g->invasion_wave_timer -= dt;
        if (g->invasion_wave_timer <= 0.0f) {
            if (g->invasion_wave >= g->invasion_wave_max) {
                g->match_over = true;
                g->invasion_victory = true;
                g->winner = g->local_player;
                snprintf(g->result, sizeof(g->result), "ALL %d WAVES CLEARED - SURVIVORS WIN", g->invasion_wave_max);
                game_announce(g, "INVASION CLEARED", col3(1.0f, 0.85f, 0.25f), 5.0f);
            } else {
                invasion_start_wave(g);
            }
        }
    } else {
        g->invasion_wave_timer = 8.0f;
    }
}

void game_update(Game* g, float dt) {
    if (dt > 0.05f) dt = 0.05f;
    g->time += dt;
    g->elapsed += dt;
    if (g->paused) return;
    game_update_nature(g, dt);
    game_build_update(g, dt);
    game_life_update(g, dt);
    if (g->match_over) g->match_over_t += dt;
    if (g->match_over && !g->rewards_granted) grant_match_rewards(g);

    if (g->hitmarker > 0) g->hitmarker -= dt;
    if (g->damage_flash > 0) g->damage_flash -= dt * 2.2f;
    if (g->shake > 0) g->shake = fmaxf(0, g->shake - dt * 2.0f);
    if (g->announce_t > 0) g->announce_t -= dt;
    if (g->pickup_msg_t > 0) g->pickup_msg_t -= dt;
    if (g->multikill_t > 0) { g->multikill_t -= dt; if (g->multikill_t <= 0) g->multikill = 0; }

    if (g->start_countdown > 0) {
        g->start_countdown -= dt;
        if (g->start_countdown <= 0) {
            game_announce(g, "FIGHT!", col3(1.0f, 0.4f, 0.15f), 1.7f);
        }
    }
    g->match_time += dt;

    if (!g->match_over && g->started) {
        if (g->match_time >= g->time_limit) {
            g->match_over = true;
            int best = 0;
            for (int i = 1; i < g->num_players; i++) if (g->players[i].frags > g->players[best].frags) best = i;
            g->winner = best;
            game_announce(g, "MATCH OVER", col3(1.0f, 0.8f, 0.2f), 5.0f);
        }
    }

    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active) continue;
        p->input.dodge = false;
        if (!p->zombie) update_vehicle_use(g, p);
        if (p->zombie) {
            /* Host/offline simulates. Clients mirror the snapshot. */
            if (!g->networked || g->server_mode) zombie_think(g, p, dt, g->num_players > 160);
        } else if (p->bot) {
            if (g->start_countdown > 0) { p->input.fire = false; p->input.move_x = 0; p->input.move_z = 0; }
            else if (g->game_mode == GM_ALTIS) bot_altis_civilian(g, p, dt);
            else if (g->num_players > 96 && i >= 64) bot_think_lite(g, p, dt);
            else bot_think(g, p, dt);
        }
        update_player_movement(g, p, dt);
        /* render interpolation targets */
        p->render_pos = p->pos;
        p->render_yaw = p->yaw;
        p->render_pitch = p->pitch;
        p->phase += dt;
    }
    /* second pass: dodge flags were consumed above; bots set them next think */
    update_projectiles(g, dt);
    update_effects(g, dt);
    update_pickups(g, dt);
    update_triggers(g, dt);
    update_flags(g, dt);
    update_nodes(g, dt);
    update_vehicles(g, dt);
    update_zone(g, dt);
    update_invasion(g, dt);
    if (g->demo_mode && g->local_player >= 0 && g->local_player < g->num_players) {
        /* follow */
    }
}

void game_update_camera_shake(Game* g, float dt) {
    if (g->shake > 0) g->shake = fmaxf(0, g->shake - dt * 2.0f);
}

void game_get_camera(Game* g, Camera* cam) {
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) li = 0;
    Player* p = &g->players[li];
    Vec3 eye = player_eye(p);
    float yaw = p->yaw;
    float pitch = p->pitch - p->view_kick * 0.35f;
    if (g->shake > 0.001f) {
        yaw += sinf(g->time * 44.0f) * g->shake * 0.012f;
        pitch += cosf(g->time * 51.0f) * g->shake * 0.012f;
    }
    if (!p->alive) {
        eye.y -= 1.0f;
        pitch -= 0.35f;
    }
    cam->pos = eye;
    cam->yaw = yaw;
    cam->pitch = pitch;
    cam->fov = g->fov > 20.0f ? g->fov : 90.0f;
    camera_update(cam);
}

void game_collect_lights(Game* g, Renderer* r) {
    int n = r->nlights;
    for (int i = 0; i < MAX_EXPLOSIONS && n < MAX_RENDER_LIGHTS; i++) {
        Explosion* e = &g->explosions[i];
        if (!e->active) continue;
        Light* l = &r->lights[n++];
        l->pos = e->pos;
        l->color = e->color;
        l->radius = e->radius * 3.4f;
        float t = e->life / e->max_life;
        l->intensity = e->intensity * t;
    }
    for (int i = 0; i < g->num_players && n < MAX_RENDER_LIGHTS; i++) {
        Player* p = &g->players[i];
        if (!p->alive || p->muzzle_flash <= 0) continue;
        Light* l = &r->lights[n++];
        Vec3 d; dir_from_angles(p->yaw, p->pitch, &d);
        l->pos = v3add(player_eye(p), v3mul(d, 0.8f));
        l->color = col3(1.0f, 0.72f, 0.3f);
        l->radius = 6.0f;
        l->intensity = 1.6f;
    }
    r->nlights = n;
    float lp[16 * 3], lc[16 * 3], lr[16];
    for (int i = 0; i < MAX_RENDER_LIGHTS; i++) {
        if (i < r->nlights) {
            lp[i * 3] = r->lights[i].pos.x; lp[i * 3 + 1] = r->lights[i].pos.y; lp[i * 3 + 2] = r->lights[i].pos.z;
            lc[i * 3] = r->lights[i].color.r * r->lights[i].intensity;
            lc[i * 3 + 1] = r->lights[i].color.g * r->lights[i].intensity;
            lc[i * 3 + 2] = r->lights[i].color.b * r->lights[i].intensity;
            lr[i] = r->lights[i].radius;
        } else { lp[i * 3] = lp[i * 3 + 1] = lp[i * 3 + 2] = 0; lc[i * 3] = lc[i * 3 + 1] = lc[i * 3 + 2] = 0; lr[i] = 0; }
    }
    glUseProgram(r->prog_scene);
    glUniform1i(glGetUniformLocation(r->prog_scene, "uLightCount"), r->nlights);
    glUniform3fv(glGetUniformLocation(r->prog_scene, "uLightPos"), 16, lp);
    glUniform3fv(glGetUniformLocation(r->prog_scene, "uLightColor"), 16, lc);
    glUniform1fv(glGetUniformLocation(r->prog_scene, "uLightRadius"), 16, lr);
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */
static Vec3 lerp3(Vec3 a, Vec3 b, float t) { return v3lerp(a, b, t); }
static void draw_box(Renderer* r, Vec3 pos, Vec3 size, Color c, int mat, float yaw, float emissive) {
    renderer_draw_model(r, &r->cube, mat4_from_trs(pos, v3(0, yaw, 0), size), c, mat, emissive, 0.5f, false);
}

void game_render_players(Game* g, Renderer* r) {
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) li = 0;
    Vec3 eye = g->players[li].pos;
    bool horde = g->num_players > 200;
    int full_budget = horde ? 14 : 28;
    float far_draw = horde ? 125.0f : 170.0f;
    float lod_draw = horde ? 38.0f : 55.0f;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active) continue;
        /* The local player is viewed in first person from inside their own
         * head. Drawing that body put the camera inside the head/helmet mesh,
         * which looked like being trapped in a tiled barrel (and crouching,
         * which lowers the head faster than the eye, appeared to free you).
         * Hide the local body; the viewmodel and weapons are drawn separately. */
        if (i == li) continue;
        float ddx = p->pos.x - eye.x, ddz = p->pos.z - eye.z;
        float d2 = ddx * ddx + ddz * ddz;
        if (d2 > far_draw * far_draw) continue;
        if (p->alive && i != li && (d2 > lod_draw * lod_draw || full_budget <= 0)) {
            Vec3 s = p->zombie ? v3(0.62f, 1.48f, 0.48f) : v3(0.72f, 1.55f, 0.5f);
            draw_box(r, v3(p->pos.x, p->pos.y + 0.9f, p->pos.z), s, p->color, 11, p->yaw, p->zombie ? 0.02f : 0.04f);
            continue;
        }
        if (!p->alive) {
            /* Corpse dissolve is driven by death_time, which the host sends in
             * the snapshot. Every client therefore sees the same lifetime and
             * the same final fade instead of a locally drifting ragdoll. */
            float ct = p->death_time;
            if (ct >= 12.0f) continue;
            float fade = ct > 9.0f ? clamp01(1.0f - (ct - 9.0f) / 3.0f) : 1.0f;
            float sink = ct > 9.0f ? (ct - 9.0f) / 3.0f * 0.22f : 0.0f;
            Color c = col_mul(p->color, 0.18f + 0.20f * fade);
            c.a = 0.9f * fade;
            draw_box(r, v3(p->pos.x, p->pos.y + 0.15f - sink, p->pos.z),
                     v3(1.1f, 0.3f * (0.55f + 0.45f * fade), 0.7f), c, 15, p->yaw, 0);
            continue;
        }
        if (i != li) full_budget--;
        Color team = p->color;
        Color dark = p->zombie ? col3(0.12f, 0.11f, 0.09f) : col3(0.18f, 0.20f, 0.24f);
        Color skin = p->zombie ? col3(0.40f, 0.52f, 0.25f) : col3(0.72f, 0.65f, 0.58f);
        float yaw = p->render_yaw;
        Vec3 f = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 rt = v3(cosf(yaw), 0, sinf(yaw));
        float speed = v3len(v3(p->vel.x, 0, p->vel.z));
        float swing = sinf(p->bob * 2.2f) * fminf(speed * 0.045f, 0.42f);
        Vec3 base = p->render_pos;
        float crouch = p->crouch ? -0.32f : 0.0f;

        /* legs - thigh + shin + foot, knee bends with swing */
        for (int s = -1; s <= 1; s += 2) {
            Vec3 hip = v3add(base, v3add(v3mul(rt, s * 0.19f), v3(0, 0.86f + crouch, 0)));
            float knee_swing = sinf(p->bob * 2.2f) * fminf(speed * 0.05f, 0.45f);
            Vec3 knee = v3add(hip, v3add(v3mul(f, knee_swing * s), v3(0, -0.50f, 0)));
            Vec3 shin = v3add(knee, v3add(v3mul(f, -knee_swing * s * 0.5f), v3(0, -0.45f, 0)));
            Vec3 foot = v3add(shin, v3(0, -0.25f, s * 0.05f));
            draw_box(r, lerp3(hip, knee, 0.5f), v3(0.22f, 0.44f, 0.24f), dark, 0, yaw, 0);
            draw_box(r, lerp3(knee, shin, 0.5f), v3(0.20f, 0.40f, 0.22f), col_mul(dark, 0.85f), 0, yaw, 0);
            draw_box(r, foot, v3(0.28f, 0.16f, 0.42f), col3(0.12f, 0.13f, 0.15f), 15, yaw, 0);
        }
        /* torso - chest + belly separation */
        Vec3 chest = v3add(base, v3(0, 1.20f + crouch, 0));
        draw_box(r, chest, v3(0.58f, 0.52f, 0.34f), team, 11, yaw, 0.05f);
        Vec3 belly = v3add(base, v3(0, 0.94f + crouch, 0));
        draw_box(r, belly, v3(0.52f, 0.28f, 0.30f), col_mul(team, 0.9f), 11, yaw, 0.04f);
        /* collar */
        draw_box(r, v3add(chest, v3(0, 0.22f, 0)), v3(0.42f, 0.14f, 0.36f), dark, 0, yaw, 0.02f);
        /* shoulders + arms - upper + forearm + hand */
        for (int s = -1; s <= 1; s += 2) {
            Vec3 sh = v3add(base, v3add(v3mul(rt, s * 0.50f), v3(0, 1.40f + crouch, 0)));
            float arm_swing = -swing * s;
            Vec3 elbow = v3add(sh, v3add(v3mul(f, arm_swing * 0.35f), v3(0, -0.34f, 0)));
            Vec3 wrist = v3add(elbow, v3add(v3mul(f, arm_swing * 0.35f), v3(0, -0.36f, 0)));
            Vec3 hand = v3add(wrist, v3add(v3mul(f, 0.30f), v3mul(rt, s * 0.04f)));
            hand.y -= 0.08f;
            draw_box(r, sh, v3(0.24f, 0.26f, 0.32f), team, 11, yaw, 0.06f);
            draw_box(r, lerp3(sh, elbow, 0.5f), v3(0.18f, 0.34f, 0.20f), dark, 0, yaw, 0);
            draw_box(r, lerp3(elbow, wrist, 0.5f), v3(0.16f, 0.30f, 0.18f), dark, 0, yaw, 0);
            draw_box(r, hand, v3(0.14f, 0.22f, 0.18f), skin, 0, yaw, 0);
        }
        /* head + neck */
        Vec3 neck = v3add(base, v3(0, 1.50f + crouch, 0));
        draw_box(r, neck, v3(0.30f, 0.16f, 0.30f), skin, 0, yaw, 0);
        Vec3 head = v3add(base, v3(0, 1.64f + crouch, 0));
        renderer_draw_model(r, &r->sphere, mat4_from_trs(head, v3(0, yaw, 0), v3(0.34f, 0.36f, 0.34f)), skin, 0, 0, 1, false);
        if (!p->zombie) {
            draw_box(r, v3add(head, v3(0, 0.13f, 0)), v3(0.40f, 0.18f, 0.40f), team, 11, yaw, 0.08f);
            Vec3 visor = v3add(head, v3mul(f, 0.15f));
            visor.y += 0.02f;
            draw_box(r, visor, v3(0.30f, 0.10f, 0.08f), col(0.15f, 0.85f, 1.0f, 1.0f), 5, yaw, 1.4f);
        } else {
            /* glowing predator eyes instead of a helmet visor */
            Color ec = p->zombie_type == 3 ? col(0.55f, 1.0f, 0.25f, 1.0f) : col(1.0f, 0.22f, 0.10f, 1.0f);
            for (int s = -1; s <= 1; s += 2) {
                Vec3 e = v3add(head, v3add(v3mul(f, 0.16f), v3mul(rt, s * 0.10f)));
                e.y += 0.03f;
                draw_box(r, e, v3(0.08f, 0.05f, 0.05f), ec, 5, yaw, 1.8f);
            }
        }
        /* weapon in hands */
        if (!p->zombie && p->weapon >= 0 && p->has[p->weapon]) {
            WeaponDef* wd = &weapon_defs[p->weapon];
            Vec3 gun = v3add(base, v3add(v3mul(f, 0.42f), v3(0, 1.08f + crouch, 0)));
            float gl = p->weapon == WPN_ROCKET ? 1.0f : (p->weapon == WPN_FLAK ? 0.8f : 0.9f);
            draw_box(r, gun, v3(0.16f, 0.16f, gl), wd->color, wd->material, yaw, 0.2f);
            Vec3 muz = v3add(gun, v3mul(f, gl * 0.55f));
            if (p->muzzle_flash > 0) {
                game_beam_fx(g, muz, v3add(muz, v3mul(f, 1.4f)), col3(1.0f, 0.8f, 0.35f), 0.16f, 0.06f);
            }
        }
        /* protection shield */
        if (p->protection > 0) {
            float a = 0.10f + 0.05f * sinf(g->time * 9.0f);
            renderer_draw_model(r, &r->sphere, mat4_from_trs(v3add(base, v3(0, 0.95f, 0)), v3(0, 0, 0), v3(1.9f, 2.1f, 1.9f)),
                                col(0.3f, 0.7f, 1.0f, a), 5, 0.4f, 1.0f, false);
        }
    }
}


void game_render_pickups(Game* g, Renderer* r) {
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_PICKUP) continue;
        if (!g->pickups[i].active) continue;
        float bob = sinf(g->time * 2.2f + i * 0.7f) * 0.12f;
        Vec3 pos = v3add(a->pos, v3(0, bob, 0));
        float yaw = g->time * 1.3f + i;
        Color c = a->color;
        /* glow halo */
        game_add_particle(g, pos, v3(0, 0, 0), col(c.r, c.g, c.b, 0.35f), 1.5f, 0.06f, true, 0, 1);
        Mesh* m = &r->cube;
        Vec3 sz = v3(0.52f, 0.52f, 0.52f);
        int sub = a->subtype;
        if (sub == PICK_HEALTH_SMALL || sub == PICK_HEALTH_BIG) {
            draw_box(r, pos, v3(0.42f, 0.42f, 0.42f), c, 5, yaw * 0.4f, 1.6f);
            draw_box(r, v3add(pos, v3(0, 0.32f, 0)), v3(0.16f, 0.2f, 0.16f), col3(1, 0.3f, 0.3f), 5, yaw, 1.8f);
            draw_box(r, v3add(pos, v3(0, 0.32f, 0)), v3(0.36f, 0.08f, 0.08f), col3(1, 0.3f, 0.3f), 5, yaw, 1.8f);
        } else if (sub >= PICK_WEAPON_AR) {
            WeaponDef* wd = &weapon_defs[sub - PICK_WEAPON_AR];
            renderer_draw_model(r, m, mat4_from_trs(v3add(pos, v3(0, 0.15f, 0)), v3(0, yaw, 0), v3(0.18f, 0.18f, 0.95f)), wd->color, wd->material, 0.5f, 0.6f, false);
            renderer_draw_model(r, &r->cylinder, mat4_from_trs(pos, v3(0, 0, 0), v3(0.9f, 0.06f, 0.9f)), col(c.r, c.g, c.b, 1), 5, 1.1f, 1, false);
        } else {
            renderer_draw_model(r, m, mat4_from_trs(pos, v3(0, yaw, 0), sz), c, 10, 1.1f, 0.7f, false);
            renderer_draw_model(r, m, mat4_from_trs(pos, v3(0, -yaw * 1.3f, 0), v3(0.3f, 0.3f, 0.3f)), col3(1, 1, 1), 5, 1.4f, 1, false);
        }
    }
    /* jump pad rings */
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_JUMPPAD) continue;
        float yaw = a->yaw * DEG2RAD;
        Vec3 dir = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 c0 = v3add(a->pos, v3(0, 0.05f, 0));
        for (int k = 0; k < 3; k++) {
            float t = fmodf(g->time * 1.2f + k * 0.33f, 1.0f);
            Vec3 p = v3add(c0, v3mul(dir, t * 2.4f));
            p.y += sinf(t * PI) * 0.8f;
            game_add_particle(g, p, v3(0, 0.2f, 0), col(a->color.r, a->color.g, a->color.b, 1.0f - t), 0.5f * (1.0f - t) + 0.1f, 0.1f, true, 0, 1);
        }
        renderer_draw_model(r, &r->cylinder, mat4_from_trs(c0, v3(0, 0, 0), v3(2.0f, 0.09f, 2.0f)), a->color, 5, 1.4f, 1, false);
    }
}

void game_render_projectiles(Game* g, Renderer* r) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* pr = &g->projs[i];
        if (!pr->active) continue;
        float s = pr->type == WPN_ROCKET ? 0.34f : 0.28f;
        renderer_draw_model(r, &r->sphere, mat4_from_trs(pr->pos, v3(0, 0, 0), v3(s, s, s)), pr->color, 5, 2.4f, 1, false);
        if (pr->type == WPN_ROCKET) {
            Vec3 back = v3mul(v3norm(pr->vel), -0.45f);
            game_add_particle(g, v3add(pr->pos, back), v3(0, 0, 0), col3(1.0f, 0.7f, 0.25f), 0.55f, 0.05f, true, 0, 1);
        }
    }
}

void game_render_particles(Game* g, Renderer* r) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g->particles[i];
        if (!p->active) continue;
        float t = clamp01(p->life / p->max_life);
        Color c = p->color;
        c.a *= t;
        float size = p->size * (0.35f + 0.65f * t);
        fx_sprite(r, p->pos, size, c, p->additive);
    }
    for (int i = 0; i < MAX_BEAMS; i++) {
        Beam* b = &g->beams[i];
        if (!b->active) continue;
        float t = clamp01(b->life / b->max_life);
        Color c = b->color; c.a = t;
        fx_beam(r, b->a, b->b, c, b->width * (0.4f + 0.6f * t));
    }
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion* e = &g->explosions[i];
        if (!e->active) continue;
        float t = 1.0f - e->life / e->max_life;
        float s = e->radius * (0.35f + 1.5f * t);
        Color c = e->color; c.a = (1.0f - t) * 0.9f;
        fx_sprite(r, e->pos, s, c, true);
        fx_sprite(r, e->pos, s * 2.4f, col(c.r, c.g * 0.8f, c.b * 0.6f, (1.0f - t) * 0.35f), true);
    }
}

/* viewmodel ------------------------------------------------------- */
void game_render_viewmodel(Game* g, Renderer* r, float time) {
    int li = g->local_player;
    if (li < 0 || li >= g->num_players) return;
    Player* p = &g->players[li];
    if (!p->alive) return;
    int wi = p->weapon;
    if (wi < 0 || wi >= WPN_COUNT) return;
    Color wc = weapon_defs[wi].color;
    Color dark = col3(0.16f, 0.17f, 0.20f);
    Color metal = col3(0.42f, 0.45f, 0.50f);
    float speed = v3len(v3(p->vel.x, 0, p->vel.z));
    float bobx = sinf(p->bob * 2.2f) * fminf(speed * 0.0035f, 0.03f);
    float boby = fabsf(cosf(p->bob * 2.2f)) * fminf(speed * 0.0035f, 0.025f);
    float kick = p->recoil * 0.05f + p->view_kick * 0.15f;
    Vec3 base = v3(0.30f + bobx, -0.28f + boby - kick, -0.55f - kick * 0.5f);
    Vec3 rot = v3(0.02f, 0.05f, -0.03f);

    /* hands */
    renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(-0.10f, -0.10f, 0.10f)), v3(0.13f, 0.13f, 0.24f), rot, col3(0.55f, 0.43f, 0.34f), 0, 0);
    renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0.09f, -0.06f, 0.18f)), v3(0.12f, 0.12f, 0.22f), rot, col3(0.55f, 0.43f, 0.34f), 0, 0);

    Mat4 m;
    switch (wi) {
    case WPN_AR:
        renderer_draw_weapon_part(r, &r->cube, base, v3(0.12f, 0.14f, 0.66f), rot, dark, 0, 0);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, 0.015f, -0.30f)), v3(0.055f, 0.055f, 0.38f), rot, metal, 0, 0);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, -0.12f, 0.06f)), v3(0.09f, 0.18f, 0.16f), rot, col3(0.24f, 0.26f, 0.30f), 0, 0);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, 0.09f, 0.05f)), v3(0.08f, 0.05f, 0.26f), rot, wc, 5, 0.6f);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, 0.0f, 0.42f)), v3(0.10f, 0.16f, 0.18f), rot, dark, 0, 0);
        break;
    case WPN_SHOCK:
        renderer_draw_weapon_part(r, &r->cube, base, v3(0.15f, 0.16f, 0.70f), rot, metal, 0, 0);
        for (int i = 0; i < 3; i++)
            renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.02f, -0.14f - i * 0.10f)), v3(0.17f, 0.06f, 0.17f), v3(PI * 0.5f, 0, 0), col3(0.25f, 0.65f, 1.0f), 5, 1.6f);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, -0.12f, 0.10f)), v3(0.11f, 0.2f, 0.15f), rot, dark, 0, 0);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, 0.03f, -0.38f)), v3(0.10f, 0.10f, 0.1f), rot, wc, 5, 1.2f);
        break;
    case WPN_ROCKET:
        renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.03f, -0.05f)), v3(0.20f, 0.8f, 0.20f), v3(PI * 0.5f, 0, 0), col3(0.32f, 0.34f, 0.30f), 0, 0);
        renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.03f, -0.5f)), v3(0.24f, 0.16f, 0.24f), v3(PI * 0.5f, 0, 0), wc, 11, 0.4f);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, -0.13f, 0.1f)), v3(0.12f, 0.2f, 0.2f), rot, dark, 0, 0);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, 0.15f, 0.02f)), v3(0.07f, 0.07f, 0.3f), rot, metal, 0, 0);
        break;
    case WPN_FLAK:
        renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.02f, -0.12f)), v3(0.22f, 0.55f, 0.22f), v3(PI * 0.5f, 0, 0), col3(0.38f, 0.30f, 0.22f), 0, 0);
        renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.02f, -0.44f)), v3(0.27f, 0.14f, 0.27f), v3(PI * 0.5f, 0, 0), wc, 10, 0.8f);
        renderer_draw_weapon_part(r, &r->cube, v3add(base, v3(0, -0.12f, 0.08f)), v3(0.12f, 0.18f, 0.2f), rot, dark, 0, 0);
        break;
    case WPN_LIGHTNING:
        renderer_draw_weapon_part(r, &r->cube, base, v3(0.12f, 0.13f, 0.6f), rot, dark, 0, 0);
        renderer_draw_weapon_part(r, &r->cylinder, v3add(base, v3(0, 0.02f, -0.34f)), v3(0.10f, 0.5f, 0.10f), v3(PI * 0.5f, 0, 0), metal, 0, 0);
        for (int i = 0; i < 2; i++)
            renderer_draw_weapon_part(r, &r->sphere, v3add(base, v3(0, 0.02f, -0.15f - i * 0.2f)), v3(0.16f, 0.16f, 0.16f), v3(0, 0, 0), wc, 5, 2.0f);
        break;
    default: break;
    }
    (void)m;
    /* muzzle flash */
    if (p->muzzle_flash > 0) {
        float f = p->muzzle_flash / 0.09f;
        Vec3 mpos = v3add(base, v3(0, 0.02f, -0.62f));
        renderer_draw_weapon_part(r, &r->sphere, mpos, v3(0.28f * f, 0.28f * f, 0.28f * f), v3(0, 0, 0), col3(1.0f, 0.8f, 0.3f), 5, 3.5f);
    }
    (void)time;
}

/* ------------------------------------------------------------------ */
/* flags, control nodes, vehicles                                      */
/* ------------------------------------------------------------------ */
void game_render_objectives(Game* g, Renderer* r) {
    for (int t = 0; t < 2; t++) {
        Flag* f = &g->flags[t];
        if (!f->active) continue;
        Color c = team_base_color(t);
        float wy = sinf(f->bob * 2.4f) * 0.12f;
        if (!f->carried) {
            draw_box(r, v3(f->pos.x, f->pos.y + 1.1f, f->pos.z), v3(0.09f, 2.4f, 0.09f), col3(0.55f, 0.58f, 0.62f), 0, 0, 0);
        }
        draw_box(r, v3(f->pos.x + 0.48f, f->pos.y + 1.75f + wy, f->pos.z), v3(0.95f, 0.62f, 0.06f), c, 11, 0, 0.25f);
        draw_box(r, v3(f->pos.x + 0.48f, f->pos.y + 1.75f + wy, f->pos.z), v3(0.35f, 0.24f, 0.08f), col_mul(c, 1.4f), 5, 0, 0.9f);
    }
    for (int i = 0; i < g->node_count; i++) {
        ControlNode* n = &g->nodes[i];
        if (!n->active) continue;
        Color c = n->team >= 0 ? team_base_color(n->team) : col3(0.85f, 0.85f, 0.85f);
        float pulse = 0.85f + 0.15f * sinf(n->pulse * 2.6f);
        renderer_draw_model(r, &r->cylinder, mat4_from_trs(v3(n->pos.x, n->pos.y + 0.35f, n->pos.z), v3(0, n->pulse, 0), v3(2.6f, 0.7f, 2.6f)), col_mul(c, 0.45f), 0, 0, 0.6f, false);
        renderer_draw_model(r, &r->sphere, mat4_from_trs(v3(n->pos.x, n->pos.y + 2.2f, n->pos.z), v3(0, 0, 0), v3(1.0f * pulse, 1.0f * pulse, 1.0f * pulse)), c, 5, 0.8f, 2.2f, false);
        draw_box(r, v3(n->pos.x, n->pos.y + 2.2f, n->pos.z), v3(0.25f, 3.2f, 0.25f), c, 5, 0, 1.1f);
        if (n->progress > 0.01f) {
            Color pc = n->attacker >= 0 ? team_base_color(n->attacker) : col3(1, 0.8f, 0.2f);
            draw_box(r, v3(n->pos.x, n->pos.y + 1.2f + n->progress * 1.4f, n->pos.z), v3(0.18f, n->progress * 2.8f, 0.18f), pc, 5, 0, 1.6f);
        }
    }
    Player* lp = (g->local_player >= 0 && g->local_player < g->num_players) ? &g->players[g->local_player] : NULL;
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active) {
            /* destroyed hull: deterministic wreck that fades home at the same
             * time on host and every client (respawn is replicated). */
            if (v->respawn <= 0.0f) continue;
            float fade = clamp01(v->respawn / 1.4f);
            float sink = (1.0f - clamp01(v->respawn / 3.0f)) * 0.35f;
            Vec3 wb = v3(v->pos.x, v->pos.y + (v->type == 1 ? -0.15f : 0.30f) - sink, v->pos.z);
            Color charred = col(0.11f, 0.105f, 0.10f, 0.95f * fade);
            Color ember = col(1.0f, 0.38f, 0.10f, 0.55f * fade);
            draw_box(r, wb, v3(v->type == 2 ? 2.9f : 2.3f, 0.42f, v->type == 2 ? 2.0f : 1.5f),
                     charred, 0, v->yaw, 0);
            if (v->type == 2)
                draw_box(r, v3(wb.x, wb.y + 0.45f, wb.z), v3(1.2f, 0.35f, 1.1f), charred, 0, v->yaw + 0.25f, 0);
            if (fade > 0.3f) {
                float pulse = 0.5f + 0.5f * sinf((float)i + v->respawn * 9.0f);
                draw_box(r, v3(wb.x + 0.3f, wb.y + 0.20f, wb.z - 0.2f), v3(0.35f, 0.08f, 0.35f),
                         col_mul(ember, 0.5f + pulse * 0.6f), 5, v->yaw, 1.1f);
            }
            continue;
        }
        Color c = team_base_color(v->team < 0 ? 0 : v->team);
        if (v->hit_flash > 0.01f) c = col_lerp(c, col3(1.0f, 0.92f, 0.7f), clamp01(v->hit_flash) * 0.55f);
        float yaw = v->yaw;
        Vec3 f = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 rt = v3(cosf(yaw), 0, sinf(yaw));
        float bob = sinf(v->bob * 2.0f + i) * (v->type == 1 ? 0.12f : 0.03f);
        Vec3 base = v3(v->pos.x, v->pos.y + (v->type == 1 ? 0.12f : 0.52f) + bob, v->pos.z);
        float dxp = lp ? v->pos.x - lp->pos.x : 0.0f;
        float dzp = lp ? v->pos.z - lp->pos.z : 0.0f;
        bool far = dxp * dxp + dzp * dzp > 130.0f * 130.0f;

        if (far) {
            draw_box(r, base, v3(2.4f, 0.8f, 1.5f), c, 11, yaw, 0.1f);
        } else if (v->type == 0) {
            /* Scorpion: low, fast buggy with four wheels and twin plasma */
            draw_box(r, base, v3(2.25f, 0.42f, 1.28f), c, 11, yaw, 0.18f);
            draw_box(r, v3add(base, v3add(v3mul(f, 0.95f), v3(0, 0.05f, 0))), v3(0.85f, 0.30f, 1.02f), col_mul(c, 0.72f), 0, yaw, 0.1f);
            draw_box(r, v3add(base, v3(0, 0.30f, 0)), v3(0.72f, 0.22f, 0.62f), col(0.10f, 0.14f, 0.18f, 1.0f), 5, yaw, 0.5f);
            if (!v->modules[VEH_WEAPON].destroyed) {
                for (int s = -1; s <= 1; s += 2) {
                    draw_box(r, v3add(base, v3add(v3mul(rt, s * 0.86f), v3mul(f, 0.55f))), v3(0.24f, 0.17f, 0.62f), col_mul(c, 1.15f), 4, yaw, 0.25f);
                }
            } else {
                /* dead weapon: one melted, drooping barrel */
                draw_box(r, v3add(base, v3add(v3mul(rt, 0.86f), v3mul(f, 0.50f))), v3(0.22f, 0.14f, 0.44f), col3(0.10f, 0.09f, 0.08f), 5, yaw + 0.2f, 0.1f);
            }
            for (int s = -1; s <= 1; s += 2) for (int k = -1; k <= 1; k += 2) {
                int part = (k > 0 ? 0 : 2) + (s > 0 ? 1 : 0);
                Vec3 w = v3add(base, v3add(v3mul(rt, s * 1.00f), v3mul(f, k * 0.78f)));
                w.y -= 0.36f;
                if (!v->modules[part].destroyed) {
                    draw_box(r, w, v3(0.40f, 0.40f, 0.40f), col3(0.07f, 0.08f, 0.09f), 0, yaw, 0);
                } else {
                    /* detached wheel lying on the ground beside the buggy */
                    Vec3 dw = v3(w.x + s * 0.55f, v->pos.y + 0.20f, w.z + (float)k * 0.35f);
                    draw_box(r, dw, v3(0.42f, 0.42f, 0.18f), col3(0.06f, 0.065f, 0.07f), 0, yaw + 0.9f + (float)k * 0.4f, 0);
                }
            }
        } else if (v->type == 1) {
            /* Manta: hover craft, broad wings, glow underneath */
            Vec3 tilt = v3add(base, v3mul(f, sinf(v->pitch) * 0.6f));
            draw_box(r, tilt, v3(1.35f, 0.38f, 1.05f), c, 11, yaw, 0.2f);
            for (int s = -1; s <= 1; s += 2) {
                Vec3 wing = v3add(tilt, v3add(v3mul(rt, s * 1.05f), v3(0, -0.04f, 0)));
                draw_box(r, wing, v3(1.15f, 0.16f, 0.78f), col_mul(c, 0.8f), 11, yaw + s * 0.18f, 0.15f);
            }
            for (int s = -1; s <= 1; s += 2) for (int k = -1; k <= 1; k += 2) {
                int part = (k > 0 ? 0 : 2) + (s > 0 ? 1 : 0);
                Vec3 pod = v3add(tilt, v3add(v3mul(rt, s * 1.05f), v3mul(f, k * 0.55f)));
                if (!v->modules[part].destroyed) {
                    draw_box(r, pod, v3(0.30f, 0.22f, 0.46f), col3(0.15f, 0.18f, 0.22f), 5, yaw, 0.4f);
                } else {
                    draw_box(r, v3(pod.x, v->pos.y + 0.22f, pod.z), v3(0.34f, 0.14f, 0.34f), col3(0.08f, 0.08f, 0.09f), 5, yaw + 0.7f, 0.1f);
                }
            }
            if (!v->modules[VEH_WEAPON].destroyed)
                draw_box(r, v3add(tilt, v3add(v3mul(f, -0.45f), v3(0, 0.22f, 0))), v3(0.55f, 0.26f, 0.55f), col(0.15f, 0.6f, 1.0f, 0.85f), 5, yaw, 0.9f);
            else
                draw_box(r, v3add(tilt, v3add(v3mul(f, -0.35f), v3(0, 0.14f, 0))), v3(0.45f, 0.18f, 0.42f), col3(0.10f, 0.10f, 0.11f), 5, yaw + 0.3f, 0.1f);
            renderer_draw_model(r, &r->cylinder, mat4_from_trs(v3(tilt.x, tilt.y - 0.42f, tilt.z), v3(0, 0, 0), v3(0.9f, 0.05f, 0.9f)),
                                col(0.25f, 0.7f, 1.0f, 0.55f), 5, 1.2f, 1.0f, false);
        } else {
            /* Goliath: heavy tank, tracks and turret barrel */
            draw_box(r, base, v3(2.95f, 0.78f, 2.05f), c, 11, yaw, 0.08f);
            for (int s = -1; s <= 1; s += 2) {
                int part_f = (s > 0 ? 1 : 0), part_b = (s > 0 ? 3 : 2);
                bool front_ok = !v->modules[part_f].destroyed;
                bool rear_ok = !v->modules[part_b].destroyed;
                Vec3 track = v3add(base, v3mul(rt, s * 1.32f));
                track.y -= 0.24f;
                if (front_ok || rear_ok) {
                    draw_box(r, track, v3(0.48f, 0.72f, 2.25f), col3(0.08f, 0.09f, 0.10f), 0, yaw, 0);
                    for (int k = -2; k <= 2; k++)
                        if ((k >= 0) ? front_ok : rear_ok)
                            draw_box(r, v3add(track, v3mul(f, (float)k * 0.42f)), v3(0.50f, 0.10f, 0.25f), col3(0.20f, 0.21f, 0.23f), 0, yaw, 0);
                } else {
                    /* both road wheels gone: broken track links in the dirt */
                    for (int k = -1; k <= 1; k++)
                        draw_box(r, v3(track.x + (float)k * 0.36f, v->pos.y + 0.16f, track.z + (float)k * 0.30f),
                                 v3(0.44f, 0.12f, 0.30f), col3(0.07f, 0.07f, 0.08f), 0, yaw + 0.4f * (float)k, 0);
                }
            }
            Vec3 turret = v3add(base, v3(0, 0.62f, 0));
            draw_box(r, turret, v3(1.35f, 0.55f, 1.20f), col_mul(c, 0.85f), 11, yaw, 0.1f);
            if (!v->modules[VEH_WEAPON].destroyed) {
                Vec3 barrel = v3add(turret, v3add(v3mul(f, 1.35f), v3(0, 0.10f, 0)));
                draw_box(r, barrel, v3(0.25f, 0.25f, 2.25f), col3(0.16f, 0.17f, 0.19f), 5, yaw, 0.2f);
                draw_box(r, v3add(barrel, v3mul(f, 0.95f)), v3(0.34f, 0.34f, 0.34f), col3(0.30f, 0.31f, 0.33f), 5, yaw, 0.1f);
            } else {
                Vec3 barrel = v3add(turret, v3add(v3mul(f, 0.70f), v3(0, 0.02f, 0)));
                draw_box(r, barrel, v3(0.24f, 0.20f, 1.10f), col3(0.09f, 0.09f, 0.09f), 5, yaw - 0.22f, 0.1f);
            }
        }
        /* driver visible in open vehicles */
        if (v->driver >= 0 && v->driver < g->num_players && !far) {
            draw_box(r, v3add(base, v3(0, v->type == 2 ? 0.95f : 0.72f, 0)), v3(0.50f, 0.50f, 0.50f), col3(0.9f, 0.70f, 0.40f), 0, yaw, 0);
        }
        /* damage / HP bar */
        if (!far && v->hp < v->max_hp - 0.5f) {
            float t = clamp01(v->hp / v->max_hp);
            Color hc = t > 0.5f ? col3(0.4f, 0.95f, 0.45f) : (t > 0.22f ? col3(1.0f, 0.75f, 0.2f) : col3(1.0f, 0.25f, 0.2f));
            draw_box(r, v3(v->pos.x, v->pos.y + (v->type == 2 ? 2.45f : 1.75f), v->pos.z), v3(1.6f, 0.09f, 0.09f), col(0.05f, 0.06f, 0.07f, 0.8f), 15, 0, 0);
            draw_box(r, v3(v->pos.x - 0.8f + 0.8f * t, v->pos.y + (v->type == 2 ? 2.45f : 1.75f), v->pos.z), v3(1.6f * t, 0.09f, 0.09f), hc, 5, 0, 1.0f);
        }
    }
}

/* full scene render ------------------------------------------------ */
void game_render_world(Game* g, Renderer* r, Camera* cam) {
    (void)cam;
    renderer_draw_world_static(r, g->world);
    fx_begin(r);
    game_render_nature(g, r);
    game_render_life(g, r);
    game_render_players(g, r);
    game_render_objectives(g, r);
    game_render_projectiles(g, r);
    game_render_pickups(g, r);
    game_render_particles(g, r);
    fx_render(r);
    game_render_builds(g, r);
    game_render_zone(g, r);
    if (g->local_player >= 0 && g->local_player < g->num_players) {
        renderer_begin_viewmodel(r);
        game_render_viewmodel(g, r, g->time);
    }
}

/* scores ---------------------------------------------------------- */
void game_sort_scores(Game* g, ScoreEntry* out, int* n) {
    int len = 0;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active || p->zombie) continue;
        ScoreEntry* e = &out[len++];
        e->frags = p->frags; e->deaths = p->deaths;
        e->assists = p->assists; e->points = p->score_points; e->rank = p->rank;
        str_copy(e->name, p->name, sizeof(e->name));
        e->color = p->color;
        e->bot = p->bot;
        e->id = p->id;
    }
    for (int i = 0; i < len; i++) for (int j = i + 1; j < len; j++) {
        if (out[j].frags > out[i].frags) { ScoreEntry t = out[i]; out[i] = out[j]; out[j] = t; }
    }
    *n = len;
}

const char* game_time_string(float t, char* buf, int cap) {
    if (t < 0) t = 0;
    int secs = (int)t;
    int m = secs / 60, s = secs % 60;
    snprintf(buf, cap, "%d:%02d", m, s);
    return buf;
}
