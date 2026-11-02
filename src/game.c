#include "game.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
    "Assault", "Onslaught", "Conquest", "RPG Deathmatch", "Battle Royale"
};
static const char* MODE_SHORT[GM_COUNT] = { "DM", "TDM", "CTF", "VCTF", "ASSAULT", "ONSLAUGHT", "CONQUEST", "RPG", "BR" };

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
    return -1;
}
bool game_mode_team_based(int mode) {
    return mode == GM_TDM || mode == GM_CTF || mode == GM_VCTF || mode == GM_ASSAULT ||
           mode == GM_ONSLAUGHT || mode == GM_CONQUEST || mode == GM_RPG;
}

static float player_max_health(Player* p) {
    int lvl = p->level > 0 ? p->level : 1;
    return 100.0f + (float)(lvl - 1) * 20.0f;
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
    for (int i = 0; i < g->num_players; i++) if (g->players[i].active && g->players[i].alive) n++;
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
    p->protection = 2.0f;
    p->bot_weapon_pref = WPN_AR;
    p->bot_aim_error = 0.02f;
    p->bot_strafe = 1;
    p->level = 1;
    p->xp = 0;
    p->xp_next = 100;
    p->dmg_mult = 1.0f;
    p->vehicle = -1;
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

void game_spawn_vehicle(Game* g, int team) {
    if (!g || g->vehicle_count >= MAX_VEHICLES) return;
    Vehicle* v = &g->vehicles[g->vehicle_count++];
    memset(v, 0, sizeof(*v));
    v->active = true;
    v->team = team;
    v->type = (g->vehicle_count % 2) ? 0 : 1;
    Vec3 b = g->team_base[team < 0 ? 0 : (team & 1)];
    v->home = v3(b.x + (team == 0 ? 3.0f : -3.0f), b.y + 0.55f, b.z + (g->vehicle_count % 3 - 1) * 3.0f);
    v->pos = v->home;
    v->yaw = team == 0 ? 0 : PI;
    v->hp = 250.0f;
    v->respawn = 0;
    v->driver = -1;
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
}

void game_shutdown(Game* g) { memset(g, 0, sizeof(*g)); }

static int pick_spawn(Game* g, Player* p) {
    int best = -1; float best_score = -1e9f;
    for (int i = 0; i < g->world->count; i++) {
        Actor* a = &g->world->actors[i];
        if (a->type != ACT_PLAYER_START) continue;
        float score = frandr(0, 6);
        for (int j = 0; j < g->num_players; j++) {
            Player* o = &g->players[j];
            if (o == p || !o->active || !o->alive) continue;
            float d = v3len(v3sub(o->pos, a->pos));
            score += d * 2.2f;
            if (d < 8.0f) score -= 80.0f;
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
    int si = pick_spawn(g, p);
    if (si >= 0) {
        Actor* a = &g->world->actors[si];
        p->pos = v3add(a->pos, v3(0, 0.15f, 0));
        p->yaw = a->yaw * DEG2RAD;
    } else {
        p->pos = v3(0, 0.5f, 20);
        p->yaw = 0;
    }
    p->vel = v3(0, 0, 0);
    p->pitch = 0;
    p->health = player_max_health(p);
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
    p->fire_timer = 0; p->alt_timer = 0;
    p->jump_count = 0;
    p->crouch = false;
    p->grounded = false;
    p->bob = 0; p->land_kick = 0; p->recoil = 0;
    p->render_pos = p->pos;
    p->render_yaw = p->yaw;
    p->render_pitch = p->pitch;
    p->bot_path_len = 0; p->bot_path_idx = 0; p->bot_target = -1; p->bot_repath = 0;
    p->use_latch = false;
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
        game_spawn_vehicle(g, 0);
        game_spawn_vehicle(g, 0);
        game_spawn_vehicle(g, 1);
        game_spawn_vehicle(g, 1);
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
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active) continue;
        p->frags = 0; p->deaths = 0; p->spree = 0;
        p->alive = false; p->respawn_timer = 0;
        p->in_vehicle = false; p->vehicle = -1;
        game_respawn(g, p);
    }
    g->br_alive = game_alive_count(g);
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
    }
    audio_play_at(SFX_EXPLODE, 1.0f, pos.x, pos.y, pos.z, g->players[g->local_player].pos.x, g->players[g->local_player].pos.y, g->players[g->local_player].pos.z,
                  sinf(g->players[g->local_player].yaw), -cosf(g->players[g->local_player].yaw));
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
    (void)point;
    (void)weapon;
    if (!victim->active || !victim->alive) return;
    if (victim->protection > 0 && attacker != victim->id) return;
    if (attacker == victim->id) dmg *= 0.6f;
    if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
        Player* atk = &g->players[attacker];
        if (game_mode_team_based(g->game_mode) && atk->team >= 0 && atk->team == victim->team) return;
        dmg *= (atk->dmg_mult > 0.01f ? atk->dmg_mult : 1.0f);
    }

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
    if (g->local_player == victim->id) { g->damage_flash = 1.0f; g->damage_dir = dir; g->shake = fminf(g->shake + dmg * 0.02f, 1.4f); }
    if (g->local_player == attacker) g->hitmarker = 0.28f;

    if (victim->health <= 0) {
        victim->health = 0;
        victim->alive = false;
        victim->deaths++;
        victim->spree = 0;
        victim->death_time = 0;
        victim->respawn_timer = (g->game_mode == GM_BATTLE_ROYALE) ? 1e9f : 2.4f;
        victim->vel = v3(0, 0, 0);
        if (victim->in_vehicle) {
            int vi = victim->vehicle;
            if (vi >= 0 && vi < g->vehicle_count) g->vehicles[vi].driver = -1;
            victim->in_vehicle = false;
            victim->vehicle = -1;
        }
        victim->killer = attacker;
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
        if (attacker >= 0 && attacker < g->num_players && attacker != victim->id) {
            Player* a = &g->players[attacker];
            a->frags++;
            a->spree++;
            if (g->game_mode == GM_RPG) {
                a->xp += 100 + victim->level * 25;
                bool leveled = false;
                while (a->xp >= a->xp_next && a->level < 60) {
                    a->xp -= a->xp_next;
                    a->level++;
                    a->xp_next = 100 + a->level * 60;
                    a->dmg_mult = 1.0f + 0.08f * (float)(a->level - 1);
                    a->health = player_max_health(a);
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
            a->health = fminf(player_max_health(a), a->health + 12.0f);
            a->armor = fminf(100.0f, a->armor + 8.0f);
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
            game_add_killfeed(g, "%s  >>  %s", a->name, victim->name);
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
    int hp = -1; Vec3 hpt;
    if (!combo && trace_players(g, eye, end, p->id, &hp, &hpt)) {
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

void game_fire(Game* g, Player* p, bool alt) {
    if (!p->alive || p->protection > 1.7f) return;
    int wi = p->weapon;
    if (wi < 0 || wi >= WPN_COUNT) return;
    WeaponDef* wd = &weapon_defs[wi];
    if (!p->has[wi]) return;
    if (p->fire_timer > 0) return;
    if (p->ammo[wi] <= 0) {
        if (p->id == g->local_player) audio_play(SFX_UI_CLICK, 0.35f, 0);
        return;
    }
    float delay = alt ? wd->alt_delay : wd->fire_delay;
    p->fire_timer = delay;
    p->ammo[wi] -= 1;
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
    p->ammo[wi] = weapon_defs[wi].ammo_max;
    p->weapon = wi;
    if (p->id == g->local_player) {
        game_pickup_msg(g, "Picked up %s", weapon_defs[wi].name);
        audio_play(SFX_PICKUP_WEAPON, 0.7f, 0);
    }
}

static void apply_pickup(Game* g, Player* p, Actor* a) {
    int sub = a->subtype;
    switch (sub) {
    case PICK_HEALTH_SMALL: p->health = fminf(player_max_health(p), p->health + 10.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+10 Health"); audio_play(SFX_PICKUP_HEALTH, 0.6f, 0); } break;
    case PICK_HEALTH_BIG: p->health = fminf(player_max_health(p), p->health + 25.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+25 Health"); audio_play(SFX_PICKUP_HEALTH, 0.8f, 0); } break;
    case PICK_ARMOR_SMALL: p->armor = fminf(100.0f, p->armor + 25.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+25 Shield"); audio_play(SFX_PICKUP_ARMOR, 0.6f, 0); } break;
    case PICK_ARMOR_BIG: p->armor = fminf(100.0f, p->armor + 50.0f); if (p->id == g->local_player) { game_pickup_msg(g, "+50 Shield"); audio_play(SFX_PICKUP_ARMOR, 0.8f, 0); } break;
    case PICK_AMMO: {
        int wi = p->weapon;
        p->ammo[wi] = (int)fminf((float)weapon_defs[wi].ammo_max, p->ammo[wi] + 20.0f);
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
                } else { pr->pos = h.point; boom = true; }
            }
            else pr->pos = v3add(prev, step);
        } else pr->pos = v3add(prev, step);

        int hp = -1; Vec3 hpt;
        if (!boom && trace_players(g, prev, pr->pos, pr->owner, &hp, &hpt)) { pr->pos = hpt; boom = true; }
        if (boom) {
            if (pr->splash > 0) game_explosion(g, pr->pos, pr->radius, pr->damage, pr->owner, pr->color);
            else {
                if (hp >= 0) game_damage(g, &g->players[hp], pr->damage, pr->owner, v3norm(pr->vel), pr->pos, pr->type);
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
        p->respawn_timer -= dt;
        p->death_time += dt;
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
    if (p->dodge_cooldown > 0) p->dodge_cooldown -= dt;

    float yaw = p->yaw;
    Vec3 fwd = v3(sinf(yaw), 0, -cosf(yaw));
    Vec3 right = v3(cosf(yaw), 0, sinf(yaw));
    Vec3 wish = v3add(v3mul(fwd, p->input.move_z), v3mul(right, p->input.move_x));
    float wl = v3len(wish);
    if (wl > 1.0f) wish = v3mul(wish, 1.0f / wl);
    wl = fminf(wl, 1.0f);

    bool want_jump = p->input.jump;
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
    if (p->input.dodge && p->grounded && p->dodge_cooldown <= 0 && wl > 0.1f) {
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
    p->crouch = p->input.crouch;

    bool was_grounded = p->grounded;
    float accel = p->grounded ? 62.0f : 14.0f;
    float max_speed = p->grounded ? (p->crouch ? 3.0f : 9.6f) : 10.5f;
    if (p->in_vehicle) { accel *= 2.4f; max_speed *= 2.7f; }
    if (p->dodge_timer > 0) { accel = 0; max_speed = 18.0f; }
    Vec3 hv = v3(p->vel.x, 0, p->vel.z);
    if (wl > 0.05f) {
        float cur = v3dot(hv, wish);
        float add = clampf(max_speed - cur, 0, accel * dt * max_speed);
        p->vel.x += wish.x * add;
        p->vel.z += wish.z * add;
    } else if (p->grounded) {
        float f = fmaxf(0.0f, 1.0f - 9.5f * dt);
        p->vel.x *= f;
        p->vel.z *= f;
        if (fabsf(p->vel.x) < 0.02f) p->vel.x = 0;
        if (fabsf(p->vel.z) < 0.02f) p->vel.z = 0;
    }
    p->vel.y -= (g->gravity > 1.0f ? g->gravity : 24.0f) * dt;
    if (p->vel.y < -55.0f) p->vel.y = -55.0f;

    bool grounded = false, hit_wall = false;
    int ga = -1;
    Vec3 pre_move = p->pos;
    world_move_box(g->world, &p->pos, &p->vel, dt, 0.40f, player_height(p), &grounded, &hit_wall, &ga);
    if (getenv("A2K4_COMBATLOG") && (fabsf(p->pos.x) > 40.0f || fabsf(p->pos.z) > 40.0f) && fabsf(pre_move.x) <= 40.0f && fabsf(pre_move.z) <= 40.0f) {
        fprintf(stderr, "[bounds] %s from %.1f,%.1f,%.1f to %.1f,%.1f,%.1f vel %.1f,%.1f,%.1f pad=%d\n",
                p->name, pre_move.x, pre_move.y, pre_move.z, p->pos.x, p->pos.y, p->pos.z,
                p->vel.x, p->vel.y, p->vel.z, p->grounded);
    }
    p->grounded = grounded;
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
    if (p->input.fire && !p->fire_latch) game_fire(g, p, false);
    if (p->input.alt_fire && !p->alt_latch) game_fire(g, p, true);
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
        if (!world_raycast(g->world, r, 3.4f, true, &h)) return false;
        if (h.normal.y < 0.5f) return false;
    }
    return true;
}

/* Wall-free + walkable check used by link building and path smoothing. */
static bool nav_segment_ok(Game* g, Vec3 a, Vec3 b) {
    Vec3 d = v3sub(b, a);
    float dist = v3len(d);
    if (dist < 0.05f) return true;
    Vec3 dir = v3mul(d, 1.0f / dist);
    Ray r; r.origin = v3add(a, v3(0, 0.1f, 0)); r.dir = dir;
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
    float step = sqrtf(area / 1100.0f);
    if (step < 3.0f) step = 3.0f;
    if (step > 8.0f) step = 8.0f;
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
    if (!have_move) { move_target = p->pos; have_move = true; }

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
            if (!safe) { fwd = 0; strafe = 0; }
        }
    }
    p->input.move_x = strafe;
    p->input.move_z = fwd;

    /* jump over obstacles / randomness */
    if (!edge_ahead && frand() < 0.03f + (1.0f - skill) * 0.02f) p->input.jump = true;
    else p->input.jump = false;
    if (!edge_ahead && frand() < 0.02f) p->input.dodge = true;

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
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active) {
            v->respawn -= dt;
            if (v->respawn <= 0) {
                v->active = true;
                v->pos = v->home;
                v->hp = 250.0f;
                v->driver = -1;
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
            }
        }
    }
}

static void update_vehicle_use(Game* g, Player* p) {
    if (!p->input.use) { p->use_latch = false; return; }
    if (p->use_latch) return;
    p->use_latch = true;
    if (p->in_vehicle) {
        int vi = p->vehicle;
        if (vi >= 0 && vi < g->vehicle_count) g->vehicles[vi].driver = -1;
        p->in_vehicle = false;
        p->vehicle = -1;
        if (p->id == g->local_player) game_pickup_msg(g, "Vehicle left");
        return;
    }
    int best = -1;
    float bd = 12.0f;
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active || v->driver >= 0) continue;
        float dx = v->pos.x - p->pos.x, dz = v->pos.z - p->pos.z;
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        Vehicle* v = &g->vehicles[best];
        v->driver = p->id;
        p->in_vehicle = true;
        p->vehicle = best;
        if (p->id == g->local_player) {
            game_pickup_msg(g, "%s ready - E to exit", v->type == 1 ? "Manta" : (v->type == 2 ? "Goliath" : "Scorpion"));
            audio_play(SFX_PICKUP_WEAPON, 0.5f, 0);
        }
    }
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

void game_update(Game* g, float dt) {
    if (dt > 0.05f) dt = 0.05f;
    g->time += dt;
    g->elapsed += dt;
    if (g->paused) return;

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
        update_vehicle_use(g, p);
        if (p->bot) {
            if (g->start_countdown > 0) { p->input.fire = false; p->input.move_x = 0; p->input.move_z = 0; }
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
    int full_budget = 28;
    for (int i = 0; i < g->num_players; i++) {
        Player* p = &g->players[i];
        if (!p->active) continue;
        float ddx = p->pos.x - eye.x, ddz = p->pos.z - eye.z;
        float d2 = ddx * ddx + ddz * ddz;
        if (g->num_players > 40) {
            if (d2 > 170.0f * 170.0f) continue;
            if (p->alive && i != li && (d2 > 55.0f * 55.0f || full_budget <= 0)) {
                draw_box(r, v3(p->pos.x, p->pos.y + 0.9f, p->pos.z), v3(0.72f, 1.55f, 0.5f), p->color, 11, p->yaw, 0.04f);
                continue;
            }
        }
        if (!p->alive) {
            /* corpse-ish flattened blocks fade */
            Color c = col_mul(p->color, 0.35f);
            draw_box(r, v3(p->pos.x, p->pos.y + 0.15f, p->pos.z), v3(1.1f, 0.3f, 0.7f), c, 15, p->yaw, 0);
            continue;
        }
        if (i != li) full_budget--;
        Color team = p->color;
        Color dark = col3(0.18f, 0.20f, 0.24f);
        float yaw = p->render_yaw;
        Vec3 f = v3(sinf(yaw), 0, -cosf(yaw));
        Vec3 rt = v3(cosf(yaw), 0, sinf(yaw));
        float speed = v3len(v3(p->vel.x, 0, p->vel.z));
        float swing = sinf(p->bob * 2.2f) * fminf(speed * 0.045f, 0.42f);
        Vec3 base = p->render_pos;
        float crouch = p->crouch ? -0.32f : 0.0f;

        /* legs */
        for (int s = -1; s <= 1; s += 2) {
            Vec3 hip = v3add(base, v3add(v3mul(rt, s * 0.17f), v3(0, 0.78f + crouch, 0)));
            Vec3 knee = v3add(hip, v3add(v3mul(f, swing * s), v3(0, -0.4f, 0)));
            draw_box(r, lerp3(hip, knee, 0.5f), v3(0.22f, 0.44f, 0.24f), dark, 0, yaw, 0);
            Vec3 foot = v3add(knee, v3add(v3mul(f, -swing * s * 0.5f), v3(0, -0.38f, 0)));
            draw_box(r, foot, v3(0.24f, 0.42f, 0.26f), col_mul(dark, 0.8f), 0, yaw, 0);
            draw_box(r, v3add(foot, v3(0, -0.2f, 0)), v3(0.26f, 0.14f, 0.36f), col3(0.12f, 0.13f, 0.15f), 15, yaw, 0);
        }
        /* torso */
        Vec3 torso = v3add(base, v3(0, 1.17f + crouch, 0));
        draw_box(r, torso, v3(0.66f, 0.58f, 0.38f), team, 11, yaw, 0.05f);
        draw_box(r, v3add(torso, v3(0, 0.02f, 0)), v3(0.42f, 0.42f, 0.44f), dark, 0, yaw, 0);
        /* shoulders */
        for (int s = -1; s <= 1; s += 2) {
            Vec3 sh = v3add(base, v3add(v3mul(rt, s * 0.44f), v3(0, 1.34f + crouch, 0)));
            draw_box(r, sh, v3(0.26f, 0.24f, 0.34f), team, 11, yaw, 0.08f);
            Vec3 hand = v3add(sh, v3add(v3mul(f, 0.34f), v3mul(rt, -s * 0.06f)));
            hand.y -= 0.22f;
            draw_box(r, v3lerp(sh, hand, 0.55f), v3(0.17f, 0.5f, 0.19f), dark, 0, yaw, 0);
        }
        /* head + helmet */
        Vec3 head = v3add(base, v3(0, 1.66f + crouch, 0));
        renderer_draw_model(r, &r->sphere, mat4_from_trs(head, v3(0, yaw, 0), v3(0.34f, 0.36f, 0.34f)), col3(0.72f, 0.65f, 0.58f), 0, 0, 1, false);
        draw_box(r, v3add(head, v3(0, 0.13f, 0)), v3(0.38f, 0.16f, 0.38f), team, 11, yaw, 0.08f);
        /* visor */
        Vec3 visor = v3add(head, v3mul(f, 0.14f));
        visor.y += 0.02f;
        draw_box(r, visor, v3(0.28f, 0.09f, 0.08f), col(0.15f, 0.85f, 1.0f, 1.0f), 5, yaw, 1.4f);
        /* weapon in hands */
        if (p->weapon >= 0 && p->has[p->weapon]) {
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
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        if (!v->active) continue;
        Color c = team_base_color(v->team < 0 ? 0 : v->team);
        Vec3 base = v3(v->pos.x, v->pos.y + 0.62f, v->pos.z);
        Vec3 f = v3(sinf(v->yaw), 0, -cosf(v->yaw));
        Vec3 rt = v3(cosf(v->yaw), 0, sinf(v->yaw));
        draw_box(r, base, v3(2.3f, 0.55f, 1.35f), c, 11, v->yaw, 0.15f);
        draw_box(r, v3add(base, v3mul(f, 0.55f)), v3(1.0f, 0.5f, 1.1f), col_mul(c, 0.7f), 0, v->yaw, 0.1f);
        draw_box(r, v3add(base, v3(0, 0.42f, 0)), v3(0.9f, 0.16f, 0.8f), col3(0.2f, 0.6f, 1.0f), 5, v->yaw, 1.2f);
        for (int s = -1; s <= 1; s += 2) for (int k = -1; k <= 1; k += 2) {
            Vec3 w = v3add(base, v3add(v3mul(rt, s * 1.02f), v3mul(f, k * 0.85f)));
            w.y -= 0.42f;
            draw_box(r, w, v3(0.42f, 0.42f, 0.42f), col3(0.09f, 0.09f, 0.10f), 0, v->yaw, 0);
        }
        if (v->driver >= 0) {
            draw_box(r, v3add(base, v3(0, 0.85f, 0)), v3(0.55f, 0.55f, 0.55f), col3(0.9f, 0.7f, 0.4f), 0, v->yaw, 0);
        }
    }
}

/* full scene render ------------------------------------------------ */
void game_render_world(Game* g, Renderer* r, Camera* cam) {
    (void)cam;
    renderer_draw_world_static(r, g->world);
    fx_begin(r);
    game_render_players(g, r);
    game_render_objectives(g, r);
    game_render_projectiles(g, r);
    game_render_pickups(g, r);
    game_render_particles(g, r);
    fx_render(r);
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
        if (!p->active) continue;
        ScoreEntry* e = &out[len++];
        e->frags = p->frags; e->deaths = p->deaths;
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
