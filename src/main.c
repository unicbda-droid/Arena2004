/* Arena2004 - OpenGL arena FPS + editor + online play
 * --------------------------------------------------- */
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include "platform.h"
#include "dirent_compat.h"

#include "math3d.h"
#include "gl_util.h"
#include "world.h"
#include "render.h"
#include "ui.h"
#include "audio.h"
#include "game.h"
#include "hud.h"
#include "editor.h"
#include "net.h"
#include "console.h"
#include "steam_bridge.h"

enum { MODE_MENU = 0, MODE_GAME = 1, MODE_EDITOR = 2 };
enum { PAGE_MAIN = 0, PAGE_HOST, PAGE_JOIN, PAGE_SETTINGS };
#define APP_MAX_MAPS 48

typedef struct {
    SDL_Window* win;
    SDL_GLContext gl;
    Renderer r;
    Ui ui;
    World* world;
    Game* game;
    Net* net;
    Editor* ed;
    int w, h;
    int mode;
    int page;
    bool running;
    bool hidden;
    bool fullscreen;
    float time;
    float fps;
    float fps_accum;
    int fps_frames;

    /* input */
    float mx, my, dmx, dmy;
    int wheel;
    bool mb[3], mb_press[3], mb_release[3];
    bool key_edge[SDL_NUM_SCANCODES];
    bool ui_escape;
    char text[64];
    int text_len;
    float look_yaw, look_pitch;
    bool was_alive;
    bool paused;
    bool show_scores;
    bool mouse_captured;
    /* keyboard virtual cursor + console */
    Console console;
    bool kbd_cursor;
    float kcx, kcy;
    bool kbd_click_down;
    bool kbd_click_allowed;
    bool show_fps;
    bool god;
    float sensitivity;
    float fov;
    float gravity;
    float fog_density;
    bool vsync;
    int opencode_pid;

    /* maps */
    char map_names[APP_MAX_MAPS][64];
    int map_count;
    int map_index;

    /* menu / setup */
    int bots;
    int difficulty;
    float frag_limit;
    float volume;
    int game_mode;
    int mega_total;
    char host_room[40];
    char host_port[12];
    char host_password[32];
    char host_lobby[64];
    char host_lobby_port[12];
    bool host_via_lobby;
    bool hosting;
    char join_host[64];
    char join_port[12];
    char join_lobby[64];
    char join_lobby_port[12];
    char join_code[16];
    char join_password[32];
    bool join_via_lobby;
    char status[192];
    float status_t;
    float connect_time;

    /* menu camera */
    Camera menu_cam;

    /* automated */
    const char* capture_path;
    int capture_frame;
    bool capture_done;
    int benchmark_frames;
    bool selftest;
    bool no_swap_sleep;
    bool log_enabled;
    char log_path[192];
    bool netgraph;
} App;

/* ------------------------------------------------------------------ */
static void app_status(App* a, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof(a->status), fmt, ap);
    va_end(ap);
    a->status_t = 6.0f;
}

/* Persistent event / protocol log (Gothic/Quake style). */
static void app_log(App* a, const char* fmt, ...) {
    if (!a->log_enabled || !a->log_path[0]) return;
    FILE* f = fopen(a->log_path, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  ", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void app_log_event(App* a, const char* kind, const char* text) {
    app_log(a, "[%s] %s", kind ? kind : "event", text ? text : "");
}

static void app_scan_maps(App* a) {
    a->map_count = 0;
    DIR* d = opendir("maps");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL && a->map_count < APP_MAX_MAPS) {
            const char* n = e->d_name;
            int len = (int)strlen(n);
            if (len <= 4 || strcmp(n + len - 4, ".map") != 0) continue;
            int k = len - 4;
            if (k > 63) k = 63;
            memcpy(a->map_names[a->map_count], n, (size_t)k);
            a->map_names[a->map_count][k] = 0;
            a->map_count++;
        }
        closedir(d);
    }
    if (a->map_count == 0) {
        str_copy(a->map_names[0], a->world ? a->world->name : "DM-Deck17", 64);
        a->map_count = 1;
    }
    a->map_index = 0;
    if (a->world) {
        for (int i = 0; i < a->map_count; i++) {
            if (!strcmp(a->map_names[i], a->world->name)) { a->map_index = i; break; }
        }
    }
}

static bool app_load_map(App* a, int index) {
    if (!a->world || index < 0 || index >= a->map_count) return false;
    char path[128];
    snprintf(path, sizeof(path), "maps/%s.map", a->map_names[index]);
    if (!world_load(a->world, path)) {
        app_status(a, "Map konnte nicht geladen werden: %s", path);
        return false;
    }
    a->map_index = index;
    str_copy(a->world->name, a->map_names[index], sizeof(a->world->name));
    app_status(a, "Map geladen: %s (%d Actors)", a->world->name, a->world->count);
    return true;
}

static void app_cycle_map(App* a, int dir) {
    if (a->map_count <= 1) return;
    int ni = (a->map_index + dir + a->map_count) % a->map_count;
    app_load_map(a, ni);
}

static void split_host_port(const char* s, char* host, int hcap, int* port) {
    if (!s || !s[0]) { str_copy(host, "127.0.0.1", hcap); return; }
    const char* colon = strrchr(s, ':');
    if (colon && colon != s) {
        int n = (int)(colon - s);
        if (n >= hcap) n = hcap - 1;
        memcpy(host, s, n);
        host[n] = 0;
        *port = atoi(colon + 1);
        if (*port <= 0) *port = 7777;
    } else {
        str_copy(host, s, hcap);
    }
}

static bool key_down(App* a, SDL_Scancode sc) {
    const Uint8* k = SDL_GetKeyboardState(NULL);
    (void)a;
    return k[sc] != 0;
}

/* ------------------------------------------------------------------ */
static void clear_scene(Renderer* r) {
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] pre-clear_scene 0x%x fbo=%u %dx%d\n", e, r->hdr.fbo, r->hdr.w, r->hdr.h); }
    glBindFramebuffer(GL_FRAMEBUFFER, r->hdr.fbo);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, r->hdr.w, r->hdr.h);
    glClearColor(0.006f, 0.008f, 0.012f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static bool save_screenshot(int w, int h, const char* path) {
    unsigned char* px = malloc((size_t)w * h * 4);
    if (!px) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    unsigned char* flip = malloc((size_t)w * h * 4);
    if (!flip) { free(px); return false; }
    for (int y = 0; y < h; y++)
        memcpy(flip + (size_t)(h - 1 - y) * w * 4, px + (size_t)y * w * 4, (size_t)w * 4);
    bool ok = save_png(path, w, h, flip);
    free(flip);
    free(px);
    return ok;
}

/* ------------------------------------------------------------------ */
static void app_set_fullscreen(App* a, bool on) {
    a->fullscreen = on;
    SDL_SetWindowFullscreen(a->win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void capture_mouse(App* a, bool on) {
    if (a->mouse_captured == on) return;
    a->mouse_captured = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

static void app_release_net(App* a) {
    if (!a->net) return;
    net_close(a->net);
    a->hosting = false;
}

static void app_start_game(App* a, bool demo, int bots) {
    if (!a->world || !a->game) return;
    capture_mouse(a, false);
    int total = demo ? bots : (1 + bots);
    if (game_mode_team_based(a->game_mode) && (total & 1) && total < MAX_PLAYERS) {
        if (demo) bots++; else bots++;
        total++;
    }
    game_init(a->game, a->world, demo, bots);
    a->game->frag_limit = (int)a->frag_limit;
    a->game->difficulty = a->difficulty;
    game_set_mode(a->game, a->game_mode);
    if (a->mega_total > MAX_PLAYERS) game_configure_zones(a->game, a->mega_total);
    game_reset(a->game);
    a->game->frag_limit = (int)a->frag_limit;
    game_set_mode(a->game, a->game_mode);
    a->game->fov = a->fov;
    a->game->gravity = a->gravity;
    a->game->fps = a->fps;
    a->mode = MODE_GAME;
    a->paused = false;
    a->show_scores = false;
    a->was_alive = false;
    a->look_yaw = a->game->players[0].yaw;
    a->look_pitch = 0;
    capture_mouse(a, true);
    audio_music(MUS_GAME);
    game_announce(a->game, demo ? "DEMO MATCH" : "DEATHMATCH", col3(1.0f, 0.72f, 0.2f), 2.0f);
}

static void app_start_host(App* a) {
    int port = atoi(a->host_port);
    if (port <= 0) port = 7777;
    int lport = atoi(a->host_lobby_port);
    if (lport <= 0) lport = 7777;
    app_release_net(a);
    int net_bots = a->bots;
    if (net_bots > PKT_MAX_PLAYERS - 1) net_bots = PKT_MAX_PLAYERS - 1;
    if (!net_host(a->net, port, a->host_via_lobby ? a->host_lobby : NULL,
                  lport, a->host_room, net_bots, a->host_password)) {
        app_status(a, "%s", a->net->status);
        return;
    }
    a->hosting = true;
    a->status_t = 8.0f;
    snprintf(a->status, sizeof(a->status), "%s", net_status(a->net));
    game_init(a->game, a->world, false, net_bots);
    a->game->frag_limit = (int)a->frag_limit;
    game_set_mode(a->game, a->game_mode);
    if (a->mega_total > MAX_PLAYERS) game_configure_zones(a->game, a->mega_total);
    game_reset(a->game);
    a->game->frag_limit = (int)a->frag_limit;
    game_set_mode(a->game, a->game_mode);
    a->game->fov = a->fov;
    a->game->gravity = a->gravity;
    a->game->networked = true;
    a->game->server_mode = true;
    a->mode = MODE_GAME;
    a->paused = false;
    a->was_alive = false;
    a->look_yaw = a->game->players[0].yaw;
    a->look_pitch = 0;
    capture_mouse(a, true);
    audio_music(MUS_GAME);
    if (a->net->room_code[0]) game_announce(a->game, "SERVER ONLINE", col3(0.4f, 1.0f, 0.5f), 3.0f);
}

static void app_start_join(App* a) {
    int port = atoi(a->join_port);
    if (port <= 0) port = 7777;
    int lport = atoi(a->join_lobby_port);
    if (lport <= 0) lport = 7777;
    app_release_net(a);
    if (!net_join(a->net, a->join_host, port, a->join_via_lobby ? a->join_lobby : NULL,
                  lport, a->join_via_lobby ? a->join_code : NULL, a->join_password)) {
        app_status(a, "%s", a->net->status);
        return;
    }
    game_init(a->game, a->world, false, 0);
    a->game->frag_limit = (int)a->frag_limit;
    game_set_mode(a->game, a->game_mode);
    game_reset(a->game);
    a->game->fov = a->fov;
    a->game->gravity = a->gravity;
    a->game->networked = true;
    a->game->server_mode = false;
    a->game->players[0].protection = 0;
    a->mode = MODE_GAME;
    a->paused = false;
    a->show_scores = false;
    a->was_alive = false;
    a->look_yaw = a->game->players[0].yaw;
    a->look_pitch = 0;
    a->connect_time = 0;
    capture_mouse(a, true);
    audio_music(MUS_GAME);
}

static void app_enter_editor(App* a) {
    app_release_net(a);
    if (!a->ed) {
        a->ed = calloc(1, sizeof(Editor));
        if (!a->ed) { app_status(a, "Nicht genug Speicher fuer den Editor"); return; }
    }
    if (!editor_init(a->ed, a->world, a->w, a->h)) {
        app_status(a, "Editor-Initialisierung fehlgeschlagen");
        free(a->ed);
        a->ed = NULL;
        return;
    }
    a->ed->fps = a->fps;
    a->mode = MODE_EDITOR;
    a->paused = false;
    capture_mouse(a, false);
    audio_music(MUS_EDITOR);
}

static void app_exit_editor(App* a) {
    if (!a->ed) { a->mode = MODE_MENU; return; }
    editor_shutdown(a->ed);
    free(a->ed);
    a->ed = NULL;
    a->mode = MODE_MENU;
    a->page = PAGE_MAIN;
    capture_mouse(a, false);
    audio_music(MUS_MENU);
    app_status(a, "Editor geschlossen - Map im Spiel nutzbar");
}

/* ------------------------------------------------------------------ */
/* console command layer                                               */
static const char* CONSOLE_HELP[] = {
    "help | commands              Diese Hilfe anzeigen",
    "clear                        Konsole leeren",
    "echo <text>                  Text ausgeben",
    "menu [main|host|join|settings]  Menue oeffnen",
    "play [bots] | demo [bots]    Deathmatch / Demo starten",
    "host [port] [room]           LAN-Server starten",
    "startserver [port]           Server starten (CS-Style)",
    "join <ip> [port]             Server per IP betreten",
    "connect <ip[:port]> [pw]     CS-Style connect / Steam-Protokoll",
    "password [pw]                Server-Passwort (Passkontrolle)",
    "lobbyjoin <code> [host] [port]  Ueber NAT-Lobby beitreten",
    "disconnect                   Verbindung trennen",
    "maps | map <name>            Maps auflisten / laden",
    "nextmap | prevmap            Naechste / vorherige Map",
    "editor                       Level-Editor oeffnen",
    "save [name] | load <name>    Map speichern / laden",
    "newmap                       Neue leere Map erstellen",
    "spawn <type> [n]             Actor platzieren (insert ist Alias)",
    "staticmesh | trigger <cmd>   Static Mesh / Event-Trigger (Passkontrolle)",
    "insert orc|player            Bot ins laufende Match einfuegen",
    "addbot [n] | removebot       Bots hinzufuegen / entfernen",
    "mode <dm|tdm|ctf|vctf|assault|onslaught|conquest|rpg|br>  Spielmodus",
    "scale <5v5|32v32|64v64|128v128|256|1000|10k|30k>  Matchgroesse",
    "team [slot] <red|blue|none>  Teamzuordnung aendern",
    "mega <n> | zones [n]         PlanetSide-Zonen (bis 30000 Spieler)",
    "zone <id> | channels | channel <id>  Zone/Channel wechseln",
    "objectives | score | vehicle [team]  Objectives, Punkte, Fahrzeug",
    "status | players            Server-/Spielerstatus wie CS",
    "protocol | netgraph         Netzwerkprotokoll / Netzanzeige",
    "stats | fps [on/off]         Match-Infos / FPS-Anzeige",
    "log [on|off|datei]           Ereignis-/Protokoll-Log schreiben",
    "event <cmd> | events         Event ausfuehren / Trigger auflisten",
    "setactor <feld> <wert>       Actor bearbeiten (name, size, mat, ...)",
    "god | give <weapon|all>      Cheats",
    "kill | teleport <x> <y> <z>  Spieleraktionen",
    "say <text>                   Ansage im Spiel",
    "set <var> <value> | get [var]  Einstellungen aendern/lesen",
    "bind <key> <command>         Taste an Konsolenbefehl binden",
    "exec <file>                  Konsolen-Skript ausfuehren",
    "screenshot [file]            Screenshot speichern",
    "opencode [args]              OpenCode im Terminal starten",
    "fullscreen <on|off> | res <w> <h> | vsync <on|off>",
    "quit                         Spiel beenden",
    NULL
};

static const char* CONSOLE_COMMANDS[] = {
    "help","commands","clear","echo","menu","play","demo","host","join","lobbyjoin",
    "disconnect","maps","map","changelevel","nextmap","prevmap","editor","save","load","newmap",
    "spawn","insert","addbot","addplayer","removebot","bot_add","bot_kick","bot_stop","stats","status","players",
    "protocol","netgraph","fps","god","give","trigger","events","event","setactor",
    "kill","teleport","say","set","get","bind","exec","script","screenshot","opencode",
    "fullscreen","res","resolution","vsync","quit","restart","pause","connect","startserver",
    "password","sv_password","joinpassword","log","mode","gametype","gamemode","scale",
    "playerscale","team","mega","megazone","zones","zonelist","zone","channels","channellist",
    "channel","ch","objectives","obj","score","scores","vehicle","veh", NULL
};

typedef struct { const char* name; int type; int sub; } SpawnDef;
static const SpawnDef SPAWN_DEFS[] = {
    {"brush", ACT_BRUSH, SHAPE_BOX}, {"box", ACT_BRUSH, SHAPE_BOX},
    {"ramp", ACT_BRUSH, SHAPE_RAMP}, {"cylinder", ACT_BRUSH, SHAPE_CYLINDER},
    {"decor", ACT_DECOR, 0}, {"barrel", ACT_DECOR, 0},
    {"staticmesh", ACT_STATICMESH, 0}, {"mesh", ACT_STATICMESH, 0}, {"static", ACT_STATICMESH, 0},
    {"trigger", ACT_TRIGGER, 0}, {"event", ACT_TRIGGER, 0},
    {"light", ACT_LIGHT, 0}, {"playerstart", ACT_PLAYER_START, 0},
    {"spawnpoint", ACT_PLAYER_START, 0}, {"jumppad", ACT_JUMPPAD, 0},
    {"lava", ACT_LAVA, 0}, {"health", ACT_PICKUP, PICK_HEALTH_BIG},
    {"healthsmall", ACT_PICKUP, PICK_HEALTH_SMALL}, {"healthbig", ACT_PICKUP, PICK_HEALTH_BIG},
    {"shield", ACT_PICKUP, PICK_ARMOR_BIG}, {"shieldsmall", ACT_PICKUP, PICK_ARMOR_SMALL},
    {"armor", ACT_PICKUP, PICK_ARMOR_BIG}, {"ammo", ACT_PICKUP, PICK_AMMO},
    {"ar", ACT_PICKUP, PICK_WEAPON_AR}, {"rifle", ACT_PICKUP, PICK_WEAPON_AR},
    {"shock", ACT_PICKUP, PICK_WEAPON_SHOCK}, {"rocket", ACT_PICKUP, PICK_WEAPON_ROCKET},
    {"flak", ACT_PICKUP, PICK_WEAPON_FLAK}, {"lightning", ACT_PICKUP, PICK_WEAPON_LIGHTNING},
};
static const int SPAWN_DEF_COUNT = (int)(sizeof(SPAWN_DEFS) / sizeof(SPAWN_DEFS[0]));

static void con_ok(App* a, const char* fmt, ...) {
    char b[CON_INPUT_MAX];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    console_printf(&a->console, "ok  %s", b);
    app_status(a, "%s", b);
}
static void con_err(App* a, const char* fmt, ...) {
    char b[CON_INPUT_MAX];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    console_printf(&a->console, "error  %s", b);
}

static int con_split(char* line, char** argv, int max) {
    int n = 0;
    char* p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[n++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = 0;
        } else {
            argv[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
        }
    }
    return n;
}

static int con_int(const char* s, int fallback) {
    if (!s || !s[0]) return fallback;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return fallback;
    return (int)v;
}
static float con_float(const char* s, float fallback) {
    if (!s || !s[0]) return fallback;
    char* end = NULL;
    float v = strtof(s, &end);
    if (end == s) return fallback;
    return v;
}
static bool con_bool(const char* s, bool fallback) {
    if (!s || !s[0]) return fallback;
    if (!strcasecmp(s, "on") || !strcasecmp(s, "true") || !strcmp(s, "1") || !strcasecmp(s, "yes")) return true;
    if (!strcasecmp(s, "off") || !strcasecmp(s, "false") || !strcmp(s, "0") || !strcasecmp(s, "no")) return false;
    return fallback;
}

static bool app_spawn_actor_type(App* a, const char* name, int count) {
    if (!a->world || count <= 0) return false;
    const SpawnDef* def = NULL;
    for (int i = 0; i < SPAWN_DEF_COUNT; i++) {
        if (!strcasecmp(name, SPAWN_DEFS[i].name)) { def = &SPAWN_DEFS[i]; break; }
    }
    if (!def) return false;
    Vec3 base = v3(0, 1, 0);
    Vec3 fwd = v3(0, 0, -1);
    if (a->mode == MODE_GAME && a->game && a->game->local_player >= 0) {
        Player* p = &a->game->players[a->game->local_player];
        base = p->pos;
        fwd = v3(sinf(p->yaw), 0, -cosf(p->yaw));
    } else if (a->mode == MODE_EDITOR && a->ed) {
        if (a->ed->cursor_valid) base = a->ed->cursor_world;
        else base = v3add(a->ed->cam.pos, v3mul(a->ed->cam.fwd, 8.0f));
        fwd = a->ed->cam.fwd;
    } else {
        for (int i = 0; i < a->world->count; i++) {
            if (a->world->actors[i].type == ACT_PLAYER_START) {
                base = a->world->actors[i].pos;
                fwd = v3(sinf(a->world->actors[i].yaw * DEG2RAD), 0, -cosf(a->world->actors[i].yaw * DEG2RAD));
                break;
            }
        }
    }
    int made = 0;
    for (int n = 0; n < count; n++) {
        Actor act;
        world_actor_defaults(&act, def->type, def->sub);
        Vec3 p = v3add(base, v3mul(fwd, 3.5f));
        if (n > 0) p.x += (float)(n % 4) * 1.6f - 2.4f, p.z += (float)(n / 4) * 1.6f;
        if (def->type == ACT_LIGHT) p.y += 3.0f;
        else if (def->type == ACT_PLAYER_START) p.y += 0.08f;
        else if (def->type == ACT_PICKUP) p.y += 0.9f;
        else if (def->type == ACT_JUMPPAD) p.y += 0.12f;
        else p.y += act.size.y * 0.5f;
        if (def->type == ACT_BRUSH) act.shape = def->sub;
        act.pos = p;
        int idx = world_add(a->world, act);
        if (idx >= 0) {
            made++;
            if (a->game && a->mode == MODE_GAME && def->type == ACT_PICKUP) {
                a->game->pickups[idx].actor = idx;
                a->game->pickups[idx].active = true;
                a->game->pickups[idx].respawn = 0;
            }
        }
    }
    if (a->game && a->mode == MODE_GAME) game_build_nav(a->game);
    if (a->ed) a->ed->dirty = true;
    con_ok(a, "%d x %s platziert (%d Actors)", made, name, a->world->count);
    return made > 0;
}

static bool app_add_bot(App* a) {
    if (!a->game || a->mode != MODE_GAME) { con_err(a, "addbot: kein laufendes Match"); return false; }
    Game* g = a->game;
    if (g->networked || g->server_mode) { con_err(a, "addbot: im Netzspiel nicht erlaubt"); return false; }
    if (g->num_players >= MAX_PLAYERS) { con_err(a, "addbot: Lobby voll (%d/%d)", g->num_players, MAX_PLAYERS); return false; }
    int i = g->num_players;
    game_set_player_count(g, i + 1);
    Player* p = &g->players[i];
    p->bot = true;
    game_respawn(g, p);
    game_build_nav(g);
    con_ok(a, "Bot %s hinzugefuegt (%d Spieler)", p->name, g->num_players);
    return true;
}

static bool app_remove_bot(App* a) {
    if (!a->game || a->mode != MODE_GAME) { con_err(a, "removebot: kein laufendes Match"); return false; }
    Game* g = a->game;
    if (g->num_players <= 1) { con_err(a, "removebot: kein Bot vorhanden"); return false; }
    int i = g->num_players - 1;
    if (i == 0 || !g->players[i].bot) { con_err(a, "removebot: letzter Slot ist kein Bot"); return false; }
    g->players[i].active = false;
    g->num_players--;
    if (g->local_player >= g->num_players) g->local_player = 0;
    game_build_nav(g);
    con_ok(a, "Bot entfernt (%d Spieler)", g->num_players);
    return true;
}

static Vec3 app_player_pos(App* a);
static int app_target_actor(App* a) {
    if (a->ed && a->ed->selected >= 0 && a->ed->selected < a->world->count) return a->ed->selected;
    if (!a->world || a->world->count <= 0) return -1;
    Vec3 p = app_player_pos(a);
    int best = -1; float bestd = 16.0f;
    for (int i = 0; i < a->world->count; i++) {
        Actor* act = &a->world->actors[i];
        if (act->type == ACT_LIGHT) continue;
        float d = v3len(v3sub(act->pos, p));
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static Vec3 app_player_pos(App* a) {
    if (a->game && a->mode == MODE_GAME && a->game->local_player >= 0 && a->game->local_player < a->game->num_players)
        return a->game->players[a->game->local_player].pos;
    if (a->ed) return a->ed->cursor_valid ? a->ed->cursor_world : a->ed->cam.pos;
    for (int i = 0; a->world && i < a->world->count; i++)
        if (a->world->actors[i].type == ACT_PLAYER_START) return a->world->actors[i].pos;
    return v3(0, 1, 0);
}

static void app_new_map(App* a) {
    if (!a->world) return;
    world_init(a->world);
    str_copy(a->world->name, "Untitled", sizeof(a->world->name));
    Actor f;
    world_actor_defaults(&f, ACT_BRUSH, 0);
    f.pos = v3(0, -0.5f, 0); f.size = v3(80, 1, 80); f.material = 4; f.color = col3(0.30f, 0.31f, 0.33f);
    world_add(a->world, f);
    for (int i = 0; i < 4; i++) {
        Actor l; world_actor_defaults(&l, ACT_LIGHT, 0);
        l.pos = v3((i & 1 ? 12.0f : -12.0f), 10, (i & 2 ? 12.0f : -12.0f));
        l.power = 26; l.extra = 1.6f; l.color = col3(0.6f, 0.75f, 1.0f);
        world_add(a->world, l);
    }
    for (int i = 0; i < 4; i++) {
        Actor s; world_actor_defaults(&s, ACT_PLAYER_START, 0);
        s.pos = v3((i & 1 ? 14.0f : -14.0f), 0.08f, (i & 2 ? 14.0f : -14.0f));
        world_add(a->world, s);
    }
    if (a->ed) {
        a->ed->selected = -1;
        a->ed->undo_count = a->ed->undo_pos = 0;
        a->ed->dirty = true;
        str_copy(a->ed->map_name, "Untitled", sizeof(a->ed->map_name));
    }
    if (a->mode == MODE_GAME && a->game) game_reset(a->game);
    con_ok(a, "Neue Map erstellt (%d Actors)", a->world->count);
}

static bool app_save_map(App* a, const char* name) {
    if (!a->world) return false;
    char nm[64];
    if (name && name[0]) str_copy(nm, name, sizeof(nm));
    else if (a->ed && a->ed->map_name[0]) str_copy(nm, a->ed->map_name, sizeof(nm));
    else str_copy(nm, a->world->name, sizeof(nm));
    char* dot = strrchr(nm, '.');
    if (dot && !strcasecmp(dot, ".map")) *dot = 0;
    if (!nm[0]) return false;
    str_copy(a->world->name, nm, sizeof(a->world->name));
    char path[192];
    if (strchr(nm, '/')) snprintf(path, sizeof(path), "%s.map", nm);
    else snprintf(path, sizeof(path), "maps/%s.map", nm);
    plat_ensure_dir("maps");
    if (!world_save(a->world, path)) { con_err(a, "save: %s fehlgeschlagen", path); return false; }
    if (a->ed) { str_copy(a->ed->map_name, nm, sizeof(a->ed->map_name)); a->ed->dirty = false; }
    app_scan_maps(a);
    con_ok(a, "gespeichert: %s (%d Actors)", path, a->world->count);
    return true;
}

static int app_find_map(App* a, const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < a->map_count; i++) if (!strcasecmp(a->map_names[i], name)) return i;
    size_t n = strlen(name);
    for (int i = 0; i < a->map_count; i++) if (!strncasecmp(a->map_names[i], name, n)) return i;
    return -1;
}

static void app_bind_command(App* a, const char* key, const char* cmd) {
    con_ok(a, "bind %s = %s (Hinweis: Konsolenbefehle sind auch per exec-Skript steuerbar)", key ? key : "?", cmd ? cmd : "?");
}

static void app_console_execute_depth(App* a, const char* line, int depth);

static void app_console_exec(App* a, const char* file, int depth) {
    if (!file || !file[0]) { con_err(a, "exec: Dateiname fehlt"); return; }
    FILE* f = fopen(file, "r");
    if (!f) { con_err(a, "exec: %s nicht gefunden", file); return; }
    char line[CON_INPUT_MAX];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        char* e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!p[0] || p[0] == '#' || (p[0] == '/' && p[1] == '/')) continue;
        console_printf(&a->console, "exec> %s", p);
        app_console_execute_depth(a, p, depth + 1);
        n++;
        if (n > 5000) { con_err(a, "exec: Skript-Limit erreicht"); break; }
    }
    fclose(f);
    con_ok(a, "exec %s (%d Befehle)", file, n);
}

static bool app_spawn_opencode(App* a, const char* args) {
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) str_copy(cwd, "/home/admin/Arena2004", sizeof(cwd));
    char inner[768];
    const char* env = getenv("A2K4_OPENCODE_CMD");
    if (env && env[0]) {
        snprintf(inner, sizeof(inner), "%s %s", env, args ? args : "");
    } else {
        snprintf(inner, sizeof(inner), "cd '%s' && opencode %s", cwd, args ? args : "");
    }
#ifdef _WIN32
    char win_cmd[1024];
    snprintf(win_cmd, sizeof(win_cmd),
             "start \"OpenCode\" cmd /k \"cd /d %s && %s\"",
             cwd, env && env[0] ? env : "opencode");
    if (args && args[0] && !(env && env[0])) {
        snprintf(win_cmd, sizeof(win_cmd),
                 "start \"OpenCode\" cmd /k \"cd /d %s && opencode %s\"", cwd, args);
    }
    int rc = system(win_cmd);
    if (rc != 0) { con_err(a, "opencode: Terminalstart fehlgeschlagen"); return false; }
    a->opencode_pid = -1;
#else
    pid_t pid = fork();
    if (pid < 0) { con_err(a, "opencode: fork fehlgeschlagen"); return false; }
    if (pid == 0) {
        setsid();
        execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "bash", "-lc", inner, (char*)NULL);
        execlp("gnome-terminal", "gnome-terminal", "--", "bash", "-lc", inner, (char*)NULL);
        execlp("konsole", "konsole", "-e", "bash", "-lc", inner, (char*)NULL);
        execlp("xfce4-terminal", "xfce4-terminal", "-e", "bash", "-lc", inner, (char*)NULL);
        execlp("xterm", "xterm", "-e", "bash", "-lc", inner, (char*)NULL);
        execlp("osascript", "osascript", "-e",
               "tell application \"Terminal\" to do script \"bash -lc 'opencode'\"", (char*)NULL);
        fprintf(stderr, "[opencode] no terminal emulator found\n");
        _exit(127);
    }
    a->opencode_pid = (int)pid;
#endif
    if (a->mode == MODE_GAME) { a->paused = true; capture_mouse(a, false); }
    con_ok(a, "OpenCode Terminal gestartet in %s", cwd);
    return true;
}

static void app_console_help(App* a, const char* topic) {
    if (topic && topic[0]) {
        for (int i = 0; CONSOLE_HELP[i]; i++) {
            if (!strncasecmp(CONSOLE_HELP[i], topic, strlen(topic))) {
                console_print(&a->console, CONSOLE_HELP[i]);
                return;
            }
        }
        con_err(a, "help: kein Eintrag fuer '%s'", topic);
        return;
    }
    console_print(&a->console, "Befehle (alles ist auch per GUI/Tastatur erreichbar):");
    for (int i = 0; CONSOLE_HELP[i]; i++) console_print(&a->console, CONSOLE_HELP[i]);
    console_print(&a->console, "Beispiele: play 5 | map DM-Rankin | spawn light 4 | insert orc | set bloom 0.8 | opencode");
}

static void app_console_set(App* a, const char* var, const char* val, bool has_val) {
    if (!var) { con_err(a, "set: Variable fehlt"); return; }
    if (!has_val) {
        /* set var -> show current value */
        has_val = false;
    }
    if (!strcasecmp(var, "fps") || !strcasecmp(var, "cl_showfps") || !strcasecmp(var, "showfps")) {
        if (has_val) { a->show_fps = con_bool(val, true); hud_set_fps_visible(a->show_fps); }
        con_ok(a, "fps = %s", a->show_fps ? "on" : "off");
    } else if (!strcasecmp(var, "fraglimit") || !strcasecmp(var, "sv_fraglimit") || !strcasecmp(var, "mp_fraglimit")) {
        if (has_val) a->frag_limit = clampf(con_float(val, a->frag_limit), 1, 1000);
        if (a->game) a->game->frag_limit = (int)a->frag_limit;
        con_ok(a, "fraglimit = %.0f", a->frag_limit);
    } else if (!strcasecmp(var, "bots") || !strcasecmp(var, "sv_bots") || !strcasecmp(var, "bot_count")) {
        if (has_val) a->bots = con_int(val, a->bots);
        if (a->bots < 1) a->bots = 1;
        if (a->bots > 7) a->bots = 7;
        con_ok(a, "bots = %d", a->bots);
    } else if (!strcasecmp(var, "skill") || !strcasecmp(var, "difficulty")) {
        if (has_val) a->difficulty = con_int(val, a->difficulty);
        if (a->difficulty < 0) a->difficulty = 0;
        if (a->difficulty > 6) a->difficulty = 6;
        if (a->game) a->game->difficulty = a->difficulty;
        con_ok(a, "skill = %d", a->difficulty);
    } else if (!strcasecmp(var, "volume") || !strcasecmp(var, "vol")) {
        if (has_val) { a->volume = clampf(con_float(val, a->volume), 0, 1); audio_set_master(a->volume); audio_set_music(a->volume * 0.5f); }
        con_ok(a, "volume = %.2f", a->volume);
    } else if (!strcasecmp(var, "sens") || !strcasecmp(var, "sensitivity") || !strcasecmp(var, "m_pitch")) {
        if (has_val) a->sensitivity = clampf(con_float(val, a->sensitivity), 0.05f, 10.0f);
        con_ok(a, "sens = %.2f", a->sensitivity);
    } else if (!strcasecmp(var, "fov")) {
        if (has_val) a->fov = clampf(con_float(val, a->fov), 40, 140);
        if (a->game) a->game->fov = a->fov;
        a->menu_cam.fov = a->fov * 0.8f;
        con_ok(a, "fov = %.0f", a->fov);
    } else if (!strcasecmp(var, "gravity") || !strcasecmp(var, "sv_gravity")) {
        if (has_val) {
            a->gravity = clampf(con_float(val, a->gravity), 1.0f, 80.0f);
            if (a->game) a->game->gravity = a->gravity;
        }
        con_ok(a, "sv_gravity = %.1f", a->gravity);
    } else if (!strcasecmp(var, "crosshair") || !strcasecmp(var, "cl_crosshair")) {
        static bool cross = true;
        if (has_val) { cross = con_bool(val, cross); hud_set_crosshair_visible(cross); }
        con_ok(a, "crosshair = %s", cross ? "on" : "off");
    } else if (!strcasecmp(var, "password") || !strcasecmp(var, "sv_password") || !strcasecmp(var, "serverpassword")) {
        if (has_val) {
            if (!strcasecmp(val, "none") || !strcasecmp(val, "off") || !strcmp(val, "0")) a->host_password[0] = 0;
            else str_copy(a->host_password, val, sizeof(a->host_password));
            if (a->net) str_copy(a->net->password, a->host_password, sizeof(a->net->password));
        }
        con_ok(a, "server password = %s", a->host_password[0] ? "gesetzt (Passkontrolle aktiv)" : "offen");
    } else if (!strcasecmp(var, "joinpassword") || !strcasecmp(var, "cl_password")) {
        if (has_val) str_copy(a->join_password, val, sizeof(a->join_password));
        con_ok(a, "join password = %s", a->join_password[0] ? "gesetzt" : "leer");
    } else if (!strcasecmp(var, "bloom")) {
        if (has_val) a->r.bloom_strength = clampf(con_float(val, a->r.bloom_strength), 0, 2.5f);
        con_ok(a, "bloom = %.2f", a->r.bloom_strength);
    } else if (!strcasecmp(var, "exposure")) {
        if (has_val) a->r.exposure = clampf(con_float(val, a->r.exposure), 0.2f, 3.0f);
        con_ok(a, "exposure = %.2f", a->r.exposure);
    } else if (!strcasecmp(var, "fog")) {
        if (has_val && a->world) a->world->fog_density = clampf(con_float(val, a->world->fog_density), 0, 0.2f);
        con_ok(a, "fog = %.4f", a->world ? a->world->fog_density : 0.0f);
    } else if (!strcasecmp(var, "fullscreen") || !strcasecmp(var, "full") || !strcasecmp(var, "r_fullscreen")) {
        if (has_val) app_set_fullscreen(a, con_bool(val, a->fullscreen));
        con_ok(a, "fullscreen = %s", a->fullscreen ? "on" : "off");
    } else if (!strcasecmp(var, "vsync") || !strcasecmp(var, "r_vsync")) {
        if (has_val) { a->vsync = con_bool(val, a->vsync); SDL_GL_SetSwapInterval(a->vsync ? 1 : 0); }
        con_ok(a, "vsync = %s", a->vsync ? "on" : "off");
    } else if (!strcasecmp(var, "god")) {
        if (has_val) a->god = con_bool(val, a->god);
        con_ok(a, "god = %s", a->god ? "on" : "off");
    } else if (!strcasecmp(var, "res") || !strcasecmp(var, "resolution")) {
        if (has_val) {
            int w = a->w, h = a->h;
            if (val && sscanf(val, "%dx%d", &w, &h) != 2) { w = a->w; h = a->h; }
            if (w > 320 && h > 200) {
                SDL_SetWindowSize(a->win, w, h);
                a->w = w; a->h = h;
                renderer_resize(&a->r, w, h);
                a->ui.w = w; a->ui.h = h;
            }
        }
        con_ok(a, "resolution = %dx%d", a->w, a->h);
    } else {
        con_err(a, "set: unbekannte Variable '%s' (fps, fraglimit, bots, skill, volume, sens, fov, gravity, bloom, exposure, fog, fullscreen, vsync, god, crosshair, password, res)", var);
    }
}

/* Scale presets: 5v5, 32v32, 64v64, 128v128, 256, 1000, 10k..30k.
 * Matches above MAX_PLAYERS are represented as PlanetSide-style zones. */
static void app_apply_scale(App* a, int total, const char* label) {
    if (total < 1) total = 1;
    if (total > 30000) total = 30000;
    if (game_mode_team_based(a->game_mode) && (total & 1) && total < MAX_PLAYERS) total++;
    int local = total > MAX_PLAYERS ? MAX_PLAYERS : total;
    a->bots = local > 1 ? local - 1 : 0;
    if (a->game && a->mode == MODE_GAME) {
        Game* g = a->game;
        game_set_player_count(g, local);
        g->num_players = local;
        game_set_mode(g, a->game_mode);
        game_configure_zones(g, total);
        game_reset(g);
        g->frag_limit = (int)a->frag_limit;
        game_set_mode(g, a->game_mode);
        a->was_alive = false;
        con_ok(a, "Scale %s: %d Spieler gesamt, %d lokal simuliert, %d Zonen, %d Channels",
               label, total, local, g->zone_count, g->chan_count);
    } else {
        con_ok(a, "Scale %s vorgemerkt: %d Spieler (naechstes Match)", label, total);
    }
}

static int parse_scale_total(const char* s) {
    if (!s || !s[0]) return 0;
    int x = 0, y = 0;
    if (sscanf(s, "%dv%d", &x, &y) == 2) {
        int t = (x + y) * 1;
        return t > 0 ? t : 0;
    }
    if (!strcasecmp(s, "duel")) return 2;
    if (!strcasecmp(s, "cs") || !strcasecmp(s, "5on5")) return 10;
    if (!strcasecmp(s, "small")) return 10;
    if (!strcasecmp(s, "medium")) return 32;
    if (!strcasecmp(s, "large")) return 64;
    if (!strcasecmp(s, "huge")) return 128;
    if (!strcasecmp(s, "mega")) return 1000;
    if (!strcasecmp(s, "royale")) return 1000;
    if (!strcasecmp(s, "planet")) return 10000;
    if (!strcasecmp(s, "world")) return 30000;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end && *end == 'k') v *= 1000;
    return (int)v;
}

static void app_print_objectives(App* a) {
    if (!a->game || a->mode != MODE_GAME) { con_ok(a, "objectives: kein laufendes Match"); return; }
    Game* g = a->game;
    console_printf(&a->console, "MODE %s (%s)  limit=%.0f  team %.0f:%.0f",
                   game_mode_name(g->game_mode), game_mode_short(g->game_mode), g->score_limit,
                   (float)g->team_score[0], (float)g->team_score[1]);
    for (int t = 0; t < 2; t++) {
        Flag* f = &g->flags[t];
        if (!f->active) continue;
        console_printf(&a->console, "  FLAG %s  %s  pos %.1f/%.1f/%.1f",
                       t == 0 ? "RED " : "BLUE", f->carried ? "carried" : (f->dropped ? "dropped" : "home"),
                       f->pos.x, f->pos.y, f->pos.z);
    }
    for (int i = 0; i < g->node_count; i++) {
        ControlNode* n = &g->nodes[i];
        if (!n->active) continue;
        console_printf(&a->console, "  NODE %-12s team=%s capture=%.0f%% pos %.1f/%.1f/%.1f",
                       n->name, n->team < 0 ? "neutral" : (n->team == 0 ? "red" : "blue"),
                       n->progress * 100.0f, n->pos.x, n->pos.y, n->pos.z);
    }
    for (int i = 0; i < g->vehicle_count; i++) {
        Vehicle* v = &g->vehicles[i];
        console_printf(&a->console, "  VEHICLE %d %s team=%s driver=%d hp=%.0f",
                       i, v->type == 1 ? "Manta" : (v->type == 2 ? "Goliath" : "Scorpion"),
                       v->team == 0 ? "red" : "blue", v->driver, v->hp);
    }
}

static void app_console_execute_depth(App* a, const char* line, int depth) {
    if (depth > 8) { con_err(a, "exec: Rekursion zu tief"); return; }
    char buf[CON_INPUT_MAX];
    str_copy(buf, line, sizeof(buf));
    char* args[24];
    int n = con_split(buf, args, 24);
    if (n <= 0) return;
    app_log_event(a, "cmd", line);
    const char* cmd = args[0];
    const char* a1 = n > 1 ? args[1] : NULL;
    const char* a2 = n > 2 ? args[2] : NULL;
    const char* a3 = n > 3 ? args[3] : NULL;

    if (!strcasecmp(cmd, "help") || !strcasecmp(cmd, "?")) app_console_help(a, a1);
    else if (!strcasecmp(cmd, "commands")) app_console_help(a, NULL);
    else if (!strcasecmp(cmd, "clear") || !strcasecmp(cmd, "cls")) console_clear(&a->console);
    else if (!strcasecmp(cmd, "echo")) {
        char out[CON_INPUT_MAX]; out[0] = 0;
        for (int i = 1; i < n; i++) { if (i > 1) strncat(out, " ", sizeof(out) - strlen(out) - 1); strncat(out, args[i], sizeof(out) - strlen(out) - 1); }
        console_print(&a->console, out);
    }
    else if (!strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit")) a->running = false;
    else if (!strcasecmp(cmd, "menu")) {
        app_release_net(a);
        capture_mouse(a, false);
        a->mode = MODE_MENU;
        a->paused = false;
        if (!a1 || !strcasecmp(a1, "main")) a->page = PAGE_MAIN;
        else if (!strcasecmp(a1, "host")) a->page = PAGE_HOST;
        else if (!strcasecmp(a1, "join")) a->page = PAGE_JOIN;
        else if (!strcasecmp(a1, "settings") || !strcasecmp(a1, "options")) a->page = PAGE_SETTINGS;
        else { con_err(a, "menu: main|host|join|settings"); return; }
        audio_music(MUS_MENU);
        con_ok(a, "Menue: %s", a1 ? a1 : "main");
    }
    else if (!strcasecmp(cmd, "play") || !strcasecmp(cmd, "dm")) {
        if (a1) a->bots = con_int(a1, a->bots);
        app_start_game(a, false, a->bots);
        con_ok(a, "Deathmatch gestartet (%d Bots)", a->bots);
    }
    else if (!strcasecmp(cmd, "demo")) {
        if (a1) a->bots = con_int(a1, a->bots);
        app_start_game(a, true, a->bots + 1);
        con_ok(a, "Demo gestartet");
    }
    else if (!strcasecmp(cmd, "host")) {
        if (a1) snprintf(a->host_port, sizeof(a->host_port), "%d", con_int(a1, atoi(a->host_port)));
        if (a2) str_copy(a->host_room, a2, sizeof(a->host_room));
        app_start_host(a);
        con_ok(a, "Host gestartet auf Port %s", a->host_port);
    }
    else if (!strcasecmp(cmd, "join")) {
        if (!a1) { con_err(a, "join <ip> [port]"); return; }
        str_copy(a->join_host, a1, sizeof(a->join_host));
        a->join_via_lobby = false;
        if (a2) snprintf(a->join_port, sizeof(a->join_port), "%d", con_int(a2, 7777));
        app_start_join(a);
        con_ok(a, "Verbinde mit %s:%s", a->join_host, a->join_port);
    }
    else if (!strcasecmp(cmd, "lobbyjoin")) {
        if (!a1) { con_err(a, "lobbyjoin <code> [lobby-host] [lobby-port]"); return; }
        str_copy(a->join_code, a1, sizeof(a->join_code));
        a->join_via_lobby = true;
        if (a2) str_copy(a->join_lobby, a2, sizeof(a->join_lobby));
        if (a3) snprintf(a->join_lobby_port, sizeof(a->join_lobby_port), "%d", con_int(a3, 7777));
        app_start_join(a);
        con_ok(a, "Lobby-Beitritt mit Code %s", a1);
    }
    else if (!strcasecmp(cmd, "disconnect")) {
        app_release_net(a);
        a->mode = MODE_MENU; a->page = PAGE_MAIN; a->paused = false;
        capture_mouse(a, false);
        audio_music(MUS_MENU);
        con_ok(a, "Verbindung getrennt");
    }
    else if (!strcasecmp(cmd, "maps")) {
        console_printf(&a->console, "Maps (%d):", a->map_count);
        for (int i = 0; i < a->map_count; i++) console_printf(&a->console, "  %s%s", a->map_names[i], i == a->map_index ? "  <-- aktuell" : "");
    }
    else if (!strcasecmp(cmd, "map") || !strcasecmp(cmd, "changelevel")) {
        if (!a1) { con_err(a, "map <name>"); return; }
        if (!strcasecmp(a1, "next")) { app_cycle_map(a, 1); con_ok(a, "Map: %s", a->world->name); return; }
        if (!strcasecmp(a1, "prev")) { app_cycle_map(a, -1); con_ok(a, "Map: %s", a->world->name); return; }
        int idx = app_find_map(a, a1);
        if (idx < 0) { con_err(a, "map: '%s' nicht gefunden", a1); return; }
        if (!app_load_map(a, idx)) return;
        if (a->mode == MODE_GAME && a->game) { game_reset(a->game); a->was_alive = false; }
        if (a->ed) { str_copy(a->ed->map_name, a->world->name, sizeof(a->ed->map_name)); a->ed->selected = -1; a->ed->dirty = false; }
        con_ok(a, "Map geladen: %s (%d Actors)", a->world->name, a->world->count);
    }
    else if (!strcasecmp(cmd, "nextmap")) { app_cycle_map(a, 1); con_ok(a, "Map: %s", a->world->name); }
    else if (!strcasecmp(cmd, "prevmap")) { app_cycle_map(a, -1); con_ok(a, "Map: %s", a->world->name); }
    else if (!strcasecmp(cmd, "editor") || !strcasecmp(cmd, "edit")) {
        if (a->mode == MODE_EDITOR) { con_ok(a, "Editor ist bereits offen"); return; }
        app_enter_editor(a);
        if (a->mode == MODE_EDITOR) con_ok(a, "Editor geoeffnet");
        else con_err(a, "Editor konnte nicht geoeffnet werden");
    }
    else if (!strcasecmp(cmd, "save")) { app_save_map(a, a1); }
    else if (!strcasecmp(cmd, "load")) {
        if (!a1) { con_err(a, "load <name>"); return; }
        int idx = app_find_map(a, a1);
        if (idx < 0) { con_err(a, "load: '%s' nicht gefunden", a1); return; }
        if (!app_load_map(a, idx)) return;
        if (a->mode == MODE_GAME && a->game) { game_reset(a->game); a->was_alive = false; }
        if (a->ed) { str_copy(a->ed->map_name, a->world->name, sizeof(a->ed->map_name)); a->ed->selected = -1; a->ed->dirty = false; }
        con_ok(a, "Map geladen: %s (%d Actors)", a->world->name, a->world->count);
    }
    else if (!strcasecmp(cmd, "newmap")) app_new_map(a);
    else if (!strcasecmp(cmd, "spawn") || !strcasecmp(cmd, "insert")) {
        if (!a1) { con_err(a, "spawn <type> [anzahl]  (brush, light, playerstart, jumppad, lava, health, ar, shock, rocket, flak, lightning, ...)"); return; }
        if (!strcasecmp(a1, "orc") || !strcasecmp(a1, "player") || !strcasecmp(a1, "bot")) {
            int count = a2 ? con_int(a2, 1) : 1;
            for (int i = 0; i < count; i++) app_add_bot(a);
            if (count > 0) game_announce(a->game, "INSERT ORC", col3(0.9f, 0.4f, 0.2f), 2.0f);
            return;
        }
        int count = a2 ? con_int(a2, 1) : 1;
        if (count < 1) count = 1;
        if (count > 64) count = 64;
        if (!app_spawn_actor_type(a, a1, count)) con_err(a, "spawn: unbekannter Typ '%s'", a1);
    }
    else if (!strcasecmp(cmd, "addbot") || !strcasecmp(cmd, "addplayer") || !strcasecmp(cmd, "bot_add")) {
        int count = a1 ? con_int(a1, 1) : 1;
        for (int i = 0; i < count; i++) if (!app_add_bot(a)) break;
    }
    else if (!strcasecmp(cmd, "removebot") || !strcasecmp(cmd, "bot_kick")) app_remove_bot(a);
    else if (!strcasecmp(cmd, "bot_stop")) {
        if (!a->game || a->mode != MODE_GAME) { con_err(a, "bot_stop: kein laufendes Match"); return; }
        int removed = 0;
        while (a->game->num_players > 1 && app_remove_bot(a)) removed++;
        con_ok(a, "bot_stop: %d Bots entfernt", removed);
    }
    else if (!strcasecmp(cmd, "stats") || !strcasecmp(cmd, "status") || !strcasecmp(cmd, "players") || !strcasecmp(cmd, "listplayers")) {
        if (!a->game || a->mode != MODE_GAME) { con_ok(a, "kein laufendes Match"); return; }
        Game* g = a->game;
        console_printf(&a->console, "Match: %s  players=%d  nav=%d  fraglimit=%d", g->world->name, g->num_players, g->nav.count, g->frag_limit);
        for (int i = 0; i < g->num_players; i++) {
            Player* p = &g->players[i];
            if (!p->active) continue;
            console_printf(&a->console, "  %d %-16s frags=%d deaths=%d bot=%d alive=%d hp=%.0f pos=%.1f/%.1f/%.1f",
                           i, p->name, p->frags, p->deaths, p->bot ? 1 : 0, p->alive ? 1 : 0, p->health,
                           p->pos.x, p->pos.y, p->pos.z);
        }
    }
    else if (!strcasecmp(cmd, "protocol") || !strcasecmp(cmd, "version")) {
        console_printf(&a->console, "ARENA2004 protocol v%d  //  scheme arena2004://connect/<host:port>", NET_VERSION);
        console_printf(&a->console, "build %s %s  GL 3.3 core  UDP  lobby-relay=%s", __DATE__, __TIME__,
                       a->net && a->net->use_lobby ? "on" : "off");
        console_printf(&a->console, "commands: connect, startserver, password, status, netgraph, log, event, trigger");
        if (a->net) console_printf(&a->console, "%s", net_status(a->net));
        console_printf(&a->console, "%s", steam_bridge_status());
    }
    else if (!strcasecmp(cmd, "netgraph")) {
        if (a1) a->netgraph = con_bool(a1, !a->netgraph); else a->netgraph = !a->netgraph;
        hud_set_netgraph_visible(a->netgraph);
        con_ok(a, "netgraph = %s", a->netgraph ? "on" : "off");
    }
    else if (!strcasecmp(cmd, "connect")) {
        if (!a1) { con_err(a, "connect <ip[:port]> [passwort]"); return; }
        char host[64]; int port = atoi(a->join_port);
        split_host_port(a1, host, sizeof(host), &port);
        str_copy(a->join_host, host, sizeof(a->join_host));
        snprintf(a->join_port, sizeof(a->join_port), "%d", port > 0 ? port : 7777);
        a->join_via_lobby = false;
        if (a2) str_copy(a->join_password, a2, sizeof(a->join_password));
        app_start_join(a);
        app_log_event(a, "connect", a->join_host);
        con_ok(a, "connect %s:%s%s", a->join_host, a->join_port, a->join_password[0] ? " (Passwort)" : "");
    }
    else if (!strcasecmp(cmd, "startserver")) {
        if (a1) snprintf(a->host_port, sizeof(a->host_port), "%d", con_int(a1, atoi(a->host_port)));
        if (a2) str_copy(a->host_room, a2, sizeof(a->host_room));
        app_start_host(a);
        app_log_event(a, "startserver", a->host_port);
        con_ok(a, "startserver Port %s%s", a->host_port, a->host_password[0] ? " (Passkontrolle aktiv)" : "");
    }
    else if (!strcasecmp(cmd, "password") || !strcasecmp(cmd, "sv_password")) {
        if (a1) {
            char rest[CON_INPUT_MAX]; rest[0] = 0;
            for (int i = 1; i < n; i++) { if (i > 1) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
            if (!strcasecmp(rest, "none") || !strcasecmp(rest, "off") || !strcmp(rest, "0")) a->host_password[0] = 0;
            else str_copy(a->host_password, rest, sizeof(a->host_password));
            if (a->net) str_copy(a->net->password, a->host_password, sizeof(a->net->password));
        }
        con_ok(a, "server password = %s", a->host_password[0] ? "gesetzt (Passkontrolle aktiv)" : "offen");
        app_log_event(a, "password", a->host_password[0] ? "set" : "cleared");
    }
    else if (!strcasecmp(cmd, "joinpassword")) {
        if (a1) {
            char rest[CON_INPUT_MAX]; rest[0] = 0;
            for (int i = 1; i < n; i++) { if (i > 1) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
            str_copy(a->join_password, rest, sizeof(a->join_password));
        }
        con_ok(a, "join password = %s", a->join_password[0] ? "gesetzt" : "leer");
    }
    else if (!strcasecmp(cmd, "log")) {
        if (a1) {
            if (!strcasecmp(a1, "on")) a->log_enabled = true;
            else if (!strcasecmp(a1, "off")) a->log_enabled = false;
            else str_copy(a->log_path, a1, sizeof(a->log_path));
        }
        con_ok(a, "log = %s (%s)", a->log_enabled ? "on" : "off", a->log_path);
    }
    else if (!strcasecmp(cmd, "event")) {
        char rest[CON_INPUT_MAX]; rest[0] = 0;
        for (int i = 1; i < n; i++) { if (i > 1) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
        if (!rest[0]) { con_err(a, "event <konsolenbefehl>"); return; }
        app_log_event(a, "event", rest);
        console_printf(&a->console, "event> %s", rest);
        app_console_execute_depth(a, rest, depth + 1);
    }
    else if (!strcasecmp(cmd, "events")) {
        int count = 0;
        for (int i = 0; i < a->world->count; i++) {
            Actor* act = &a->world->actors[i];
            if (act->type != ACT_TRIGGER) continue;
            console_printf(&a->console, "  trigger #%d  pos=%.1f/%.1f/%.1f  %s  (%s)", i,
                           act->pos.x, act->pos.y, act->pos.z, act->name,
                           act->extra > 0 ? "repeat" : "once");
            count++;
        }
        con_ok(a, "%d Trigger/Events", count);
    }
    else if (!strcasecmp(cmd, "trigger")) {
        int idx = app_target_actor(a);
        if (idx < 0 || a->world->actors[idx].type != ACT_TRIGGER) {
            con_err(a, "trigger: keinen Trigger ausgewaehlt (Editor: Actor waehlen)");
            return;
        }
        if (n < 2) { con_ok(a, "trigger: %s", a->world->actors[idx].name); return; }
        char rest[CON_INPUT_MAX]; rest[0] = 0;
        for (int i = 1; i < n; i++) { if (i > 1) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
        str_copy(a->world->actors[idx].name, rest, MAX_NAME);
        if (a->ed) a->ed->dirty = true;
        con_ok(a, "Trigger #%d Event = %s", idx, rest);
    }
    else if (!strcasecmp(cmd, "setactor")) {
        int idx = app_target_actor(a);
        if (idx < 0) { con_err(a, "setactor: kein Actor ausgewaehlt"); return; }
        Actor* act = &a->world->actors[idx];
        if (!a1) { con_ok(a, "setactor: #%d %s '%s'", idx, actor_type_name(act->type), act->name); return; }
        if (!a2) { con_err(a, "setactor <name|pos|size|yaw|mat|color|extra|respawn|solid|emissive> ..."); return; }
        if (!strcasecmp(a1, "name")) {
            char rest[CON_INPUT_MAX]; rest[0] = 0;
            for (int i = 2; i < n; i++) { if (i > 2) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
            str_copy(act->name, rest, MAX_NAME);
        } else if (!strcasecmp(a1, "pos") && n >= 5) {
            act->pos = v3(con_float(args[2], act->pos.x), con_float(args[3], act->pos.y), con_float(args[4], act->pos.z));
        } else if (!strcasecmp(a1, "size") && n >= 5) {
            act->size = v3(con_float(args[2], act->size.x), con_float(args[3], act->size.y), con_float(args[4], act->size.z));
        } else if (!strcasecmp(a1, "yaw")) act->yaw = con_float(a2, act->yaw);
        else if (!strcasecmp(a1, "mat") || !strcasecmp(a1, "material")) act->material = con_int(a2, act->material);
        else if (!strcasecmp(a1, "extra")) act->extra = con_float(a2, act->extra);
        else if (!strcasecmp(a1, "respawn")) act->respawn = con_float(a2, act->respawn);
        else if (!strcasecmp(a1, "solid")) act->flags = con_bool(a2, true) ? (act->flags | FLAG_SOLID) : (act->flags & ~FLAG_SOLID);
        else if (!strcasecmp(a1, "emissive")) act->flags = con_bool(a2, true) ? (act->flags | FLAG_EMISSIVE) : (act->flags & ~FLAG_EMISSIVE);
        else if (!strcasecmp(a1, "color") && n >= 5) act->color = col3(con_float(args[2], 1), con_float(args[3], 1), con_float(args[4], 1));
        else { con_err(a, "setactor: unbekanntes Feld '%s'", a1); return; }
        if (a->ed) { a->ed->dirty = true; }
        if (a->game && a->mode == MODE_GAME) game_build_nav(a->game);
        con_ok(a, "Actor #%d (%s) aktualisiert", idx, actor_type_name(act->type));
    }
    else if (!strcasecmp(cmd, "fps")) {
        if (a1) { a->show_fps = con_bool(a1, a->show_fps); hud_set_fps_visible(a->show_fps); }
        con_ok(a, "fps = %.1f (Anzeige %s)", a->fps, a->show_fps ? "on" : "off");
    }
    else if (!strcasecmp(cmd, "god")) { a->god = !a->god; con_ok(a, "god = %s", a->god ? "on" : "off"); }
    else if (!strcasecmp(cmd, "give")) {
        if (!a->game || a->mode != MODE_GAME) { con_err(a, "give: kein laufendes Match"); return; }
        Player* p = &a->game->players[a->game->local_player];
        if (!a1 || !strcasecmp(a1, "all")) {
            for (int i = 0; i < WPN_COUNT; i++) { p->has[i] = true; p->ammo[i] = weapon_defs[i].ammo_max; }
            p->health = 100; p->armor = 100;
            con_ok(a, "alle Waffen, Health und Shield");
        } else if (!strcasecmp(a1, "health")) { p->health = 199; con_ok(a, "Health 199"); }
        else if (!strcasecmp(a1, "armor") || !strcasecmp(a1, "shield")) { p->armor = 150; con_ok(a, "Shield 150"); }
        else {
            int w = -1;
            if (!strcasecmp(a1,"ar") || !strcasecmp(a1,"rifle")) w = WPN_AR;
            else if (!strcasecmp(a1,"shock")) w = WPN_SHOCK;
            else if (!strcasecmp(a1,"rocket")) w = WPN_ROCKET;
            else if (!strcasecmp(a1,"flak")) w = WPN_FLAK;
            else if (!strcasecmp(a1,"lightning")) w = WPN_LIGHTNING;
            if (w < 0) { con_err(a, "give: health|armor|ar|shock|rocket|flak|lightning|all"); return; }
            p->has[w] = true; p->ammo[w] = weapon_defs[w].ammo_max; p->weapon = w;
            con_ok(a, "Waffe: %s", weapon_defs[w].name);
        }
    }
    else if (!strcasecmp(cmd, "kill")) {
        if (!a->game || a->mode != MODE_GAME) { con_err(a, "kill: kein laufendes Match"); return; }
        Player* p = &a->game->players[a->game->local_player];
        p->health = 0; p->alive = true;
        con_ok(a, "Spieler getoetet");
    }
    else if (!strcasecmp(cmd, "teleport")) {
        if (!a->game || a->mode != MODE_GAME) { con_err(a, "teleport: kein laufendes Match"); return; }
        Player* p = &a->game->players[a->game->local_player];
        Vec3 pos = app_player_pos(a);
        if (a1) pos.x = con_float(a1, pos.x);
        if (a2) pos.y = con_float(a2, pos.y);
        if (a3) pos.z = con_float(a3, pos.z);
        p->pos = pos; p->vel = v3(0,0,0); p->grounded = false;
        con_ok(a, "teleport %.1f/%.1f/%.1f", pos.x, pos.y, pos.z);
    }
    else if (!strcasecmp(cmd, "say")) {
        char out[CON_INPUT_MAX]; out[0] = 0;
        for (int i = 1; i < n; i++) { if (i > 1) strncat(out, " ", sizeof(out) - strlen(out) - 1); strncat(out, args[i], sizeof(out) - strlen(out) - 1); }
        if (!a->game || a->mode != MODE_GAME) { console_print(&a->console, out); return; }
        game_announce(a->game, out, col3(1.0f, 0.75f, 0.25f), 3.0f);
        con_ok(a, "Ansage: %s", out);
    }
    else if (!strcasecmp(cmd, "mode") || !strcasecmp(cmd, "gametype") || !strcasecmp(cmd, "gamemode")) {
        if (!a1) {
            console_printf(&a->console, "Modi (%d):", GM_COUNT);
            for (int i = 0; i < GM_COUNT; i++)
                console_printf(&a->console, "  %-11s %s%s", game_mode_short(i), game_mode_name(i),
                               i == a->game_mode ? "  <-- aktuell" : "");
            return;
        }
        int m = game_mode_from_name(a1);
        if (m < 0) { con_err(a, "mode: unbekannt '%s' (dm, tdm, ctf, vctf, assault, onslaught, conquest, rpg, br)", a1); return; }
        a->game_mode = m;
        if (a->game && a->mode == MODE_GAME) {
            game_set_mode(a->game, m);
            game_reset(a->game);
            a->game->frag_limit = (int)a->frag_limit;
            game_set_mode(a->game, m);
            a->was_alive = false;
        }
        con_ok(a, "mode = %s (%s)", game_mode_name(m), game_mode_short(m));
    }
    else if (!strcasecmp(cmd, "scale") || !strcasecmp(cmd, "playerscale")) {
        if (!a1) { con_ok(a, "scale: 5v5 | 32v32 | 64v64 | 128v128 | 256 | 1000 | 10000 | 30000"); return; }
        int total = parse_scale_total(a1);
        if (total <= 0) { con_err(a, "scale: Zahl oder Preset erwartet"); return; }
        app_apply_scale(a, total, a1);
    }
    else if (!strcasecmp(cmd, "team")) {
        if (!a->game || a->mode != MODE_GAME) { con_err(a, "team: kein laufendes Match"); return; }
        int idx = a->game->local_player;
        const char* tv = a1;
        if (a1 && a2) { idx = con_int(a1, idx); tv = a2; }
        if (idx < 0 || idx >= a->game->num_players) { con_err(a, "team: Slot ungueltig"); return; }
        int team = -1;
        if (tv && (!strcasecmp(tv, "red") || !strcmp(tv, "0"))) team = 0;
        else if (tv && (!strcasecmp(tv, "blue") || !strcmp(tv, "1"))) team = 1;
        else if (tv && (!strcasecmp(tv, "none") || !strcasecmp(tv, "spectator") || !strcmp(tv, "-1"))) team = -1;
        else { con_err(a, "team [slot] red|blue|none"); return; }
        game_set_team(a->game, idx, team);
        con_ok(a, "team slot %d = %s", idx, team < 0 ? "none" : (team == 0 ? "red" : "blue"));
    }
    else if (!strcasecmp(cmd, "mega") || !strcasecmp(cmd, "megazone")) {
        if (!a1) { con_err(a, "mega <10..30000>  (PlanetSide-Zonen)"); return; }
        int total = parse_scale_total(a1);
        if (total < 1) total = 10000;
        if (a->game) {
            game_configure_zones(a->game, total);
            con_ok(a, "Mega-Server: %d Spieler in %d Zonen / %d Channels (lokal %d simuliert)",
                   total, a->game->zone_count, a->game->chan_count, MAX_PLAYERS);
        } else con_ok(a, "Mega-Server vorgemerkt: %d Spieler", total);
        a->mega_total = total;
    }
    else if (!strcasecmp(cmd, "zones") || !strcasecmp(cmd, "zonelist")) {
        if (a1) {
            if (!a->game) { con_err(a, "zones: kein Match"); return; }
            game_configure_zones(a->game, parse_scale_total(a1));
        }
        if (!a->game) { con_ok(a, "zones: kein Match"); return; }
        Game* g = a->game;
        console_printf(&a->console, "Zonen: %d  Gesamt: %d  lokal: %s", g->zone_count, g->zone_total,
                       g->zone_label[g->zone_id]);
        for (int z = 0; z < g->zone_count; z++)
            console_printf(&a->console, "  [%d] %-10s %5d Spieler%s", z, g->zone_label[z], g->zone_pop[z],
                           z == g->zone_id ? "  <-- aktiv" : "");
    }
    else if (!strcasecmp(cmd, "zone")) {
        if (!a->game) { con_err(a, "zone: kein Match"); return; }
        if (a1) game_select_zone(a->game, con_int(a1, 0));
        con_ok(a, "zone = %s (%d Spieler, %d Channels)",
               a->game->zone_label[a->game->zone_id], a->game->zone_pop[a->game->zone_id], a->game->chan_count);
    }
    else if (!strcasecmp(cmd, "channels") || !strcasecmp(cmd, "channellist")) {
        if (!a->game) { con_ok(a, "channels: kein Match"); return; }
        Game* g = a->game;
        console_printf(&a->console, "Zone %s: %d Channels, Cap %d", g->zone_label[g->zone_id], g->chan_count, g->chan_cap);
        for (int c = 0; c < g->chan_count; c++)
            console_printf(&a->console, "  %s  %d Spieler%s", g->chan_label[c], g->chan_pop[c],
                           c == g->chan_id ? "  <-- aktiv" : "");
    }
    else if (!strcasecmp(cmd, "channel") || !strcasecmp(cmd, "ch")) {
        if (!a->game) { con_err(a, "channel: kein Match"); return; }
        if (a1) game_select_channel(a->game, con_int(a1, 0));
        con_ok(a, "channel = %s (%d/%d)", a->game->chan_label[a->game->chan_id],
               a->game->chan_id + 1, a->game->chan_count);
    }
    else if (!strcasecmp(cmd, "objectives") || !strcasecmp(cmd, "obj")) app_print_objectives(a);
    else if (!strcasecmp(cmd, "score") || !strcasecmp(cmd, "scores")) {
        if (!a->game) { con_ok(a, "score: kein Match"); return; }
        Game* g = a->game;
        console_printf(&a->console, "%s: RED %d  BLUE %d  limit %.0f  kills %d/%d",
                       game_mode_short(g->game_mode), g->team_score[0], g->team_score[1], g->score_limit,
                       g->team_kills[0], g->team_kills[1]);
    }
    else if (!strcasecmp(cmd, "vehicle") || !strcasecmp(cmd, "veh")) {
        if (!a->game) { con_err(a, "vehicle: kein Match"); return; }
        Game* g = a->game;
        int team = a1 ? con_int(a1, 0) : (g->players[g->local_player].team >= 0 ? g->players[g->local_player].team : 0);
        if (team < 0 || team > 1) team = 0;
        int before = g->vehicle_count;
        game_spawn_vehicle(g, team);
        if (g->vehicle_count > before) con_ok(a, "Fahrzeug %d fuer %s gespawnt (E = ein/aus)", g->vehicle_count - 1, team == 0 ? "RED" : "BLUE");
        else con_err(a, "vehicle: Limit erreicht");
    }
    else if (!strcasecmp(cmd, "set")) { app_console_set(a, a1, a2, n > 2); }
    else if (!strcasecmp(cmd, "sensitivity")) { app_console_set(a, "sens", a1, n > 1); }
    else if (!strcasecmp(cmd, "cl_showfps")) { app_console_set(a, "fps", a1, n > 1); }
    else if (!strcasecmp(cmd, "sv_gravity")) { app_console_set(a, "sv_gravity", a1, n > 1); }
    else if (!strcasecmp(cmd, "r_fullscreen")) { app_console_set(a, "fullscreen", a1, n > 1); }
    else if (!strcasecmp(cmd, "r_vsync")) { app_console_set(a, "vsync", a1, n > 1); }
    else if (!strcasecmp(cmd, "name")) {
        if (a1 && a->game && a->game->local_player >= 0 && a->game->local_player < a->game->num_players)
            str_copy(a->game->players[a->game->local_player].name, a1, sizeof(a->game->players[0].name));
        con_ok(a, "name = %s", a1 ? a1 : "Player");
    }
    else if (!strcasecmp(cmd, "get")) {
        if (!a1) { app_console_set(a, "fps", NULL, false); app_console_set(a, "fraglimit", NULL, false); app_console_set(a, "bots", NULL, false); app_console_set(a, "skill", NULL, false); app_console_set(a, "volume", NULL, false); app_console_set(a, "sens", NULL, false); app_console_set(a, "fov", NULL, false); app_console_set(a, "bloom", NULL, false); app_console_set(a, "exposure", NULL, false); app_console_set(a, "fog", NULL, false); app_console_set(a, "fullscreen", NULL, false); app_console_set(a, "vsync", NULL, false); app_console_set(a, "god", NULL, false); }
        else app_console_set(a, a1, NULL, false);
    }
    else if (!strcasecmp(cmd, "bind")) app_bind_command(a, a1, a2);
    else if (!strcasecmp(cmd, "exec") || !strcasecmp(cmd, "script")) app_console_exec(a, a1, depth);
    else if (!strcasecmp(cmd, "screenshot")) {
        const char* path = a1 ? a1 : "shots/console.png";
        bool ok = save_screenshot(a->w, a->h, path);
        if (ok) con_ok(a, "Screenshot: %s", path); else con_err(a, "Screenshot fehlgeschlagen: %s", path);
    }
    else if (!strcasecmp(cmd, "opencode") || !strcasecmp(cmd, "code")) {
        char rest[CON_INPUT_MAX]; rest[0] = 0;
        for (int i = 1; i < n; i++) { if (i > 1) strncat(rest, " ", sizeof(rest) - strlen(rest) - 1); strncat(rest, args[i], sizeof(rest) - strlen(rest) - 1); }
        app_spawn_opencode(a, rest);
    }
    else if (!strcasecmp(cmd, "fullscreen")) { if (a1) app_set_fullscreen(a, con_bool(a1, true)); else app_set_fullscreen(a, !a->fullscreen); con_ok(a, "fullscreen = %s", a->fullscreen ? "on" : "off"); }
    else if (!strcasecmp(cmd, "vsync")) { if (a1) a->vsync = con_bool(a1, !a->vsync); else a->vsync = !a->vsync; SDL_GL_SetSwapInterval(a->vsync ? 1 : 0); con_ok(a, "vsync = %s", a->vsync ? "on" : "off"); }
    else if (!strcasecmp(cmd, "res") || !strcasecmp(cmd, "resolution")) {
        if (a1 && a2) {
            int w = con_int(a1, a->w), h = con_int(a2, a->h);
            if (w > 320 && h > 200) { SDL_SetWindowSize(a->win, w, h); a->w = w; a->h = h; renderer_resize(&a->r, w, h); a->ui.w = w; a->ui.h = h; }
        }
        con_ok(a, "resolution = %dx%d", a->w, a->h);
    }
    else if (!strcasecmp(cmd, "restart")) {
        if (a->game && a->mode == MODE_GAME) { game_reset(a->game); a->was_alive = false; con_ok(a, "Match neu gestartet"); }
        else con_err(a, "restart: kein laufendes Match");
    }
    else if (!strcasecmp(cmd, "pause")) {
        if (a->mode == MODE_GAME) { a->paused = !a->paused; capture_mouse(a, !a->paused); con_ok(a, "pause = %s", a->paused ? "on" : "off"); }
        else con_err(a, "pause: nur im Spiel");
    }
    else con_err(a, "unbekannter Befehl '%s' - tippe 'help'", cmd);
}

static void app_console_execute(App* a, const char* line) {
    app_console_execute_depth(a, line, 0);
}

static bool app_console_key_input(App* a) {
    Console* c = &a->console;
    bool toggle = a->key_edge[SDL_SCANCODE_GRAVE] || a->key_edge[SDL_SCANCODE_F8];
    if (!c->open) {
        if (toggle) {
            console_set_open(c, true);
            capture_mouse(a, false);
            a->text_len = 0; a->text[0] = 0;
            return true;
        }
        return false;
    }
    if (toggle || a->key_edge[SDL_SCANCODE_ESCAPE]) {
        console_set_open(c, false);
        if (a->mode == MODE_GAME && !a->paused) capture_mouse(a, true);
        a->text_len = 0; a->text[0] = 0;
        return true;
    }
    if (a->key_edge[SDL_SCANCODE_LCTRL] && a->key_edge[SDL_SCANCODE_L]) console_clear(c);
    if (a->key_edge[SDL_SCANCODE_PAGEUP]) console_scroll(c, 4);
    if (a->key_edge[SDL_SCANCODE_PAGEDOWN]) console_scroll(c, -4);
    if (a->key_edge[SDL_SCANCODE_UP]) console_history_move(c, -1);
    if (a->key_edge[SDL_SCANCODE_DOWN]) console_history_move(c, 1);
    if (a->key_edge[SDL_SCANCODE_BACKSPACE]) console_backspace(c);
    if (a->text_len > 0) console_add_text(c, a->text, a->text_len);
    if (a->key_edge[SDL_SCANCODE_TAB]) {
        c->hint[0] = 0;
        const char* pfx = c->input;
        int matches = 0;
        const char* only = NULL;
        for (int i = 0; CONSOLE_COMMANDS[i]; i++) {
            if (!strncasecmp(CONSOLE_COMMANDS[i], pfx, strlen(pfx))) {
                matches++;
                only = CONSOLE_COMMANDS[i];
            }
        }
        if (matches == 1 && only) {
            str_copy(c->input, only, CON_INPUT_MAX);
            c->input_len = (int)strlen(c->input);
            snprintf(c->hint, sizeof(c->hint), "vervollstaendigt: %s", only);
        } else if (matches > 1) {
            char b[CON_INPUT_MAX]; b[0] = 0;
            for (int i = 0; CONSOLE_COMMANDS[i]; i++) {
                if (!strncasecmp(CONSOLE_COMMANDS[i], pfx, strlen(pfx))) {
                    if (strlen(b) + strlen(CONSOLE_COMMANDS[i]) + 2 < sizeof(b)) { strcat(b, CONSOLE_COMMANDS[i]); strcat(b, "  "); }
                }
            }
            console_printf(c, "%s", b);
        }
    }
    if (a->key_edge[SDL_SCANCODE_RETURN] || a->key_edge[SDL_SCANCODE_KP_ENTER]) {
        char cmd[CON_INPUT_MAX];
        if (console_submit(c, cmd, sizeof(cmd))) app_console_execute(a, cmd);
    }
    a->text_len = 0; a->text[0] = 0;
    return true;
}

static void app_update_kbd_cursor(App* a, float dt) {
    if (a->console.open) return;
    bool allow = a->mode == MODE_MENU || (a->mode == MODE_GAME && a->paused) ||
                 (a->mode == MODE_EDITOR && a->ed && !a->ed->pie_active);
    if (!allow) {
        if (a->mode == MODE_GAME && !a->paused) a->kbd_cursor = false;
        return;
    }
    float dx = (float)(key_down(a, SDL_SCANCODE_RIGHT) ? 1 : 0) - (float)(key_down(a, SDL_SCANCODE_LEFT) ? 1 : 0);
    float dy = (float)(key_down(a, SDL_SCANCODE_DOWN) ? 1 : 0) - (float)(key_down(a, SDL_SCANCODE_UP) ? 1 : 0);
    if (dx != 0 || dy != 0) {
        a->kbd_cursor = true;
        float spd = (key_down(a, SDL_SCANCODE_LSHIFT) || key_down(a, SDL_SCANCODE_RSHIFT)) ? 1500.0f : 720.0f;
        float l = sqrtf(dx * dx + dy * dy);
        a->kcx = clampf(a->kcx + dx / l * spd * dt, 2.0f, (float)a->w - 3.0f);
        a->kcy = clampf(a->kcy + dy / l * spd * dt, 2.0f, (float)a->h - 3.0f);
        a->mx = a->kcx; a->my = a->kcy;
    }
    if (a->kbd_cursor) { a->mx = a->kcx; a->my = a->kcy; }
}

static void app_apply_kbd_click(App* a) {
    if (a->console.open || !a->kbd_cursor || !a->kbd_click_allowed) return;
    if (a->ui.focus != 0) return;
    if (!(a->key_edge[SDL_SCANCODE_RETURN] || a->key_edge[SDL_SCANCODE_KP_ENTER])) return;
    a->mx = a->kcx;
    a->my = a->kcy;
    a->mb[0] = true;
    a->mb_press[0] = true;
    a->mb_release[0] = true;
    a->kbd_click_down = true;
}


/* ------------------------------------------------------------------ */
static void build_ui_input(App* a, UiInput* in) {
    memset(in, 0, sizeof(*in));
    if (a->console.open) {
        in->mx = a->kbd_cursor ? a->kcx : a->mx;
        in->my = a->kbd_cursor ? a->kcy : a->my;
        return;
    }
    if (a->kbd_cursor) {
        in->mx = a->kcx;
        in->my = a->kcy;
        a->mx = a->kcx;
        a->my = a->kcy;
    } else {
        int mx = 0, my = 0;
        Uint32 m = SDL_GetMouseState(&mx, &my);
        (void)m;
        a->mx = (float)mx;
        a->my = (float)my;
        in->mx = a->mx;
        in->my = a->my;
    }
    in->down = a->mb[0];
    in->pressed = a->mb_press[0];
    in->released = a->mb_release[0];
    in->wheel = a->wheel;
    const Uint8* k = SDL_GetKeyboardState(NULL);
    in->shift = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    in->ctrl = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    in->alt = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
    in->enter = a->key_edge[SDL_SCANCODE_RETURN] || a->key_edge[SDL_SCANCODE_KP_ENTER];
    in->backspace = a->key_edge[SDL_SCANCODE_BACKSPACE];
    in->escape = a->key_edge[SDL_SCANCODE_ESCAPE];
    in->tab = a->key_edge[SDL_SCANCODE_TAB];
    in->del = a->key_edge[SDL_SCANCODE_DELETE];
    in->up = a->key_edge[SDL_SCANCODE_UP];
    in->down_nav = a->key_edge[SDL_SCANCODE_DOWN];
    in->left = a->key_edge[SDL_SCANCODE_LEFT];
    in->right = a->key_edge[SDL_SCANCODE_RIGHT];
    in->home = a->key_edge[SDL_SCANCODE_HOME];
    in->end = a->key_edge[SDL_SCANCODE_END];
    in->page_up = a->key_edge[SDL_SCANCODE_PAGEUP];
    in->page_down = a->key_edge[SDL_SCANCODE_PAGEDOWN];
    in->minus = a->key_edge[SDL_SCANCODE_MINUS] || a->key_edge[SDL_SCANCODE_KP_MINUS];
    in->plus = a->key_edge[SDL_SCANCODE_EQUALS] || a->key_edge[SDL_SCANCODE_KP_PLUS];
    memcpy(in->text, a->text, sizeof(in->text));
    in->text_len = a->text_len;
}

static void update_menu_camera(App* a, float dt) {
    a->time += dt;
    float t = a->time * 0.055f;
    Vec3 target = v3(0, 3.0f, 0);
    a->menu_cam.pos = v3(cosf(t) * 44.0f, 16.0f + sinf(t * 1.7f) * 4.0f, sinf(t) * 44.0f);
    Vec3 d = v3sub(target, a->menu_cam.pos);
    float len = v3len(d);
    if (len < 0.01f) d = v3(0, 0, -1);
    d = v3mul(d, 1.0f / fmaxf(len, 0.01f));
    a->menu_cam.yaw = atan2f(d.x, -d.z);
    a->menu_cam.pitch = asinf(clampf(d.y, -1.0f, 1.0f));
    a->menu_cam.fov = 72.0f;
    camera_update(&a->menu_cam);
}

/* ------------------------------------------------------------------ */
static void game_update_input(App* a, Game* g, float dt) {
    int li = g->local_player;
    if (li < 0 || li >= MAX_PLAYERS) li = 0;
    Player* p = &g->players[li];
    if (!p->active) return;
    if (p->alive && !a->was_alive) {
        a->look_yaw = p->yaw;
        a->look_pitch = p->pitch;
    }
    a->was_alive = p->alive;
    float sens = 0.0021f * (a->sensitivity > 0.01f ? a->sensitivity : 1.0f);
    bool input_locked = a->console.open;
    if (!a->paused && !input_locked) {
        a->look_yaw += a->dmx * sens;
        a->look_pitch = clampf(a->look_pitch - a->dmy * sens, -1.45f, 1.45f);
        /* full keyboard aiming: arrow keys turn / look */
        float klook = 2.1f * (key_down(a, SDL_SCANCODE_LSHIFT) ? 0.45f : 1.0f);
        if (key_down(a, SDL_SCANCODE_LEFT)) a->look_yaw -= klook * dt;
        if (key_down(a, SDL_SCANCODE_RIGHT)) a->look_yaw += klook * dt;
        if (key_down(a, SDL_SCANCODE_UP)) a->look_pitch = clampf(a->look_pitch + klook * dt, -1.45f, 1.45f);
        if (key_down(a, SDL_SCANCODE_DOWN)) a->look_pitch = clampf(a->look_pitch - klook * dt, -1.45f, 1.45f);
    }
    PlayerInput in;
    memset(&in, 0, sizeof(in));
    in.move_z = (key_down(a, SDL_SCANCODE_W) ? 1.0f : 0.0f) - (key_down(a, SDL_SCANCODE_S) ? 1.0f : 0.0f);
    in.move_x = (key_down(a, SDL_SCANCODE_D) ? 1.0f : 0.0f) - (key_down(a, SDL_SCANCODE_A) ? 1.0f : 0.0f);
    in.jump = key_down(a, SDL_SCANCODE_SPACE);
    in.crouch = key_down(a, SDL_SCANCODE_LCTRL) || key_down(a, SDL_SCANCODE_RCTRL);
    in.fire = (a->mb[0] || key_down(a, SDL_SCANCODE_F) || key_down(a, SDL_SCANCODE_RETURN)) && !a->paused && !input_locked;
    in.alt_fire = (a->mb[2] || key_down(a, SDL_SCANCODE_G)) && !a->paused && !input_locked;
    in.dodge = key_down(a, SDL_SCANCODE_LSHIFT) || key_down(a, SDL_SCANCODE_RSHIFT);
    in.use = key_down(a, SDL_SCANCODE_E);
    in.yaw = a->look_yaw;
    in.pitch = a->look_pitch;
    in.switch_weapon = -1;
    if (a->paused || input_locked) {
        in.fire = in.alt_fire = in.jump = in.dodge = in.crouch = in.use = false;
        in.move_x = in.move_z = 0;
        in.yaw = p->yaw;
        in.pitch = p->pitch;
    }
    /* weapon selection */
    if (!input_locked) for (int i = 0; i < WPN_COUNT; i++) {
        if (a->key_edge[SDL_SCANCODE_1 + i] && p->has[i]) p->weapon = i;
    }
    if (!input_locked && a->wheel != 0) {
        int dir = a->wheel > 0 ? 1 : -1;
        int w = p->weapon;
        for (int k = 0; k < WPN_COUNT; k++) {
            w = (w + dir + WPN_COUNT) % WPN_COUNT;
            if (p->has[w]) { p->weapon = w; break; }
        }
    }
    a->game->local_player = li;
    p->input = in;
}

static void game_frame(App* a, float dt) {
    Game* g = a->game;
    if (!g) return;
    bool is_client = a->net && a->net->role == NET_ROLE_CLIENT;
    bool is_host = a->net && a->net->role == NET_ROLE_HOST;
    if (a->net) net_poll(a->net, g, dt);
    if (is_client) {
        if (a->net->local_slot >= 0 && a->net->local_slot < MAX_PLAYERS)
            g->local_player = a->net->local_slot;
        if (!a->net->connected) {
            a->connect_time += dt;
            g->paused = true;
            g->fps = a->fps;
            g->ping = a->net->ping;
            return;
        }
        g->paused = false;
    }
    game_update_input(a, g, dt);
    if (!a->paused) game_update(g, dt);
    g->fps = a->fps;
    g->ping = a->net ? a->net->ping : 0;
    if (is_client) {
        int ci = g->local_player;
        if (ci < 0 || ci >= MAX_PLAYERS) ci = 0;
        PlayerInput in = g->players[ci].input;
        net_client_send_input(a->net, g, in, dt);
    }
    if (is_host) net_host_broadcast(a->net, g, dt);
    if (!a->console.open) {
        if (g->match_over && a->key_edge[SDL_SCANCODE_F5]) {
            game_reset(g);
            a->was_alive = false;
        }
        if (a->key_edge[SDL_SCANCODE_F5] && !g->match_over) {
            game_reset(g);
            a->was_alive = false;
            app_status(a, "Match neu gestartet");
        }
        if (a->key_edge[SDL_SCANCODE_ESCAPE]) {
            a->paused = !a->paused;
            capture_mouse(a, !a->paused);
        }
        if (a->key_edge[SDL_SCANCODE_F10]) {
            app_release_net(a);
            a->mode = MODE_MENU;
            a->page = PAGE_MAIN;
            capture_mouse(a, false);
            audio_music(MUS_MENU);
            return;
        }
    }
    a->show_scores = !a->console.open && key_down(a, SDL_SCANCODE_TAB);
}

/* ------------------------------------------------------------------ */
static void editor_build_input(App* a, EdInput* in) {
    memset(in, 0, sizeof(*in));
    in->mx = a->mx;
    in->my = a->my;
    in->dx = a->dmx;
    in->dy = a->dmy;
    in->wheel = a->wheel;
    in->left = a->mb[0]; in->left_pressed = a->mb_press[0]; in->left_released = a->mb_release[0];
    in->right = a->mb[2]; in->right_pressed = a->mb_press[2];
    in->kf = key_down(a, SDL_SCANCODE_W);
    in->kb = key_down(a, SDL_SCANCODE_S);
    in->kl = key_down(a, SDL_SCANCODE_A);
    in->kr = key_down(a, SDL_SCANCODE_D);
    in->ku = key_down(a, SDL_SCANCODE_E);
    in->kd = key_down(a, SDL_SCANCODE_Q);
    in->fast = key_down(a, SDL_SCANCODE_LSHIFT) || key_down(a, SDL_SCANCODE_RSHIFT);
    in->shift = in->fast;
    in->ctrl = key_down(a, SDL_SCANCODE_LCTRL) || key_down(a, SDL_SCANCODE_RCTRL);
    in->alt = key_down(a, SDL_SCANCODE_LALT) || key_down(a, SDL_SCANCODE_RALT);
    in->del = a->key_edge[SDL_SCANCODE_DELETE];
    in->undo = a->key_edge[SDL_SCANCODE_Z] && in->ctrl;
    in->redo = a->key_edge[SDL_SCANCODE_Y] && in->ctrl;
    in->escape = a->key_edge[SDL_SCANCODE_ESCAPE];
    in->duplicate = a->key_edge[SDL_SCANCODE_D] && in->ctrl;
    in->play = a->key_edge[SDL_SCANCODE_F5];
    in->enter = a->key_edge[SDL_SCANCODE_RETURN];
    in->backspace = a->key_edge[SDL_SCANCODE_BACKSPACE];
    in->space = key_down(a, SDL_SCANCODE_SPACE);
    in->text_len = a->text_len;
    memcpy(in->text, a->text, sizeof(in->text));
}

static void editor_frame(App* a, float dt) {
    if (!a->ed) { a->mode = MODE_MENU; return; }
    EdInput in;
    editor_build_input(a, &in);
    editor_update(a->ed, dt, &in, a->w, a->h);
    a->ed->fps = a->fps;
    /* mouse wheel is consumed by the editor camera */
    if (a->key_edge[SDL_SCANCODE_F10]) {
        app_exit_editor(a);
    }
}

/* ------------------------------------------------------------------ */
static void draw_menu(App* a, Ui* ui) {
    int w = a->w, h = a->h;
    float accent = 0.98f;
    /* left panel */
    ui_rect_grad(ui, 0, 0, (float)w, (float)h, col(0.01f, 0.014f, 0.022f, 0.10f), col(0.01f, 0.014f, 0.022f, 0.72f));
    ui_rect(ui, 0, 0, 560, (float)h, col(0.018f, 0.024f, 0.035f, 0.90f));
    ui_rect(ui, 558, 0, 2, (float)h, col(accent, 0.62f, 0.16f, 0.9f));

    /* logo */
    ui_rect(ui, 58, 58, 42, 42, col(accent, 0.62f, 0.16f, 1.0f));
    ui_tri(ui, v2(68, 68), v2(68, 90), v2(90, 79), col(0.03f, 0.04f, 0.05f, 1));
    ui_text(ui, 116, 52, "ARENA", 46, col(0.95f, 0.97f, 1.0f, 1));
    ui_text(ui, 116, 94, "2004", 26, col(accent, 0.62f, 0.16f, 1));
    ui_text(ui, 60, 120, "OPENGL ARENA DEATHMATCH", 11, col(0.55f, 0.62f, 0.72f, 1));

    if (a->page == PAGE_MAIN) {
        float y = 184;
        if (ui_button(ui, "menu.play", "DEATHMATCH  (vs BOTS)", 60, y, 430, 44, 1)) {
            a->mode = MODE_GAME;
            app_start_game(a, false, a->bots);
        }
        y += 54;
        if (ui_button(ui, "menu.demo", "DEMO MATCH  (BOTS ONLY)", 60, y, 430, 38, 0)) {
            a->mode = MODE_GAME;
            app_start_game(a, true, a->bots + 1);
        }
        y += 48;
        if (ui_button(ui, "menu.host", "HOST ONLINE GAME", 60, y, 210, 40, 0)) {
            a->page = PAGE_HOST;
            a->status_t = 0;
        }
        if (ui_button(ui, "menu.join", "JOIN GAME", 280, y, 210, 40, 0)) {
            a->page = PAGE_JOIN;
            a->status_t = 0;
        }
        y += 52;
        if (ui_button(ui, "menu.editor", "LEVEL EDITOR  (UE3 STYLE)", 60, y, 430, 40, 0)) {
            app_enter_editor(a);
        }
        y += 50;
        if (ui_button(ui, "menu.settings", "SETTINGS", 60, y, 210, 36, 0)) a->page = PAGE_SETTINGS;
        if (ui_button(ui, "menu.quit", "QUIT", 280, y, 210, 36, 0)) a->running = false;

        /* status */
        if (a->status_t > 0 || a->status[0]) {
            ui_text(ui, 60, (float)h - 150, a->status, 11, col(accent, 0.75f, 0.40f, 1));
        }
        ui_text(ui, 60, (float)h - 118, "TAB SCORES   ESC PAUSE   F10 MENU", 10, col(0.45f, 0.50f, 0.58f, 1));
        char mf[160];
        snprintf(mf, sizeof(mf), "MAP  %s   //   PROCEDURAL ASSETS", a->world ? a->world->name : "-");
        ui_text(ui, 60, (float)h - 100, mf, 10, col(0.45f, 0.50f, 0.58f, 1));
        ui_text(ui, 60, (float)h - 76, "OpenGL 3.3 Core  |  SDL2  |  UDP LAN / NAT Relay", 10, col(0.34f, 0.39f, 0.46f, 1));

        /* right info card */
        float cx = (float)w - 380;
        ui_rect(ui, cx, 60, 320, 286, col(0.02f, 0.028f, 0.04f, 0.78f));
        ui_rect(ui, cx, 60, 320, 2, col(accent, 0.62f, 0.16f, 0.9f));
        ui_text(ui, cx + 16, 74, "MATCH SETUP", 10, col(0.98f, 0.62f, 0.16f, 1));
        char b[96];
        snprintf(b, sizeof(b), "BOTS        %d", a->bots);
        ui_text(ui, cx + 16, 96, b, 12, col(0.85f, 0.88f, 0.92f, 1));
        snprintf(b, sizeof(b), "FRAG LIMIT  %d", (int)a->frag_limit);
        ui_text(ui, cx + 16, 114, b, 12, col(0.85f, 0.88f, 0.92f, 1));
        const char* skills[] = { "NOVICE", "AVERAGE", "EXPERIENCED", "SKILLED", "ADEPT", "MASTERFUL", "INHUMAN" };
        snprintf(b, sizeof(b), "BOT SKILL   %s", skills[a->difficulty < 0 ? 0 : (a->difficulty > 6 ? 6 : a->difficulty)]);
        ui_text(ui, cx + 16, 132, b, 12, col(0.85f, 0.88f, 0.92f, 1));
        snprintf(b, sizeof(b), "AUDIO       %s", audio_active() ? "ON" : "SILENT");
        ui_text(ui, cx + 16, 150, b, 12, col(0.85f, 0.88f, 0.92f, 1));

        ui_text(ui, cx + 16, 176, "MODE", 9, col(0.98f, 0.62f, 0.16f, 1));
        snprintf(b, sizeof(b), "%s  %s", game_mode_short(a->game_mode), game_mode_name(a->game_mode));
        ui_text(ui, cx + 16, 191, b, 12, col(0.92f, 0.95f, 1.0f, 1));
        if (ui_button(ui, "mode.prev", "<", cx + 232, 184, 30, 24, 0)) {
            a->game_mode = (a->game_mode + GM_COUNT - 1) % GM_COUNT;
        }
        if (ui_button(ui, "mode.next", ">", cx + 266, 184, 30, 24, 0)) {
            a->game_mode = (a->game_mode + 1) % GM_COUNT;
        }

        ui_text(ui, cx + 16, 214, "SCALE", 9, col(0.98f, 0.62f, 0.16f, 1));
        if (a->mega_total > 0)
            snprintf(b, sizeof(b), "MEGA %d  (%d zones)", a->mega_total, a->game ? a->game->zone_count : 0);
        else
            snprintf(b, sizeof(b), "%d players%s", a->bots + 1, game_mode_team_based(a->game_mode) ? " (teams)" : "");
        ui_text(ui, cx + 16, 229, b, 12, col(0.92f, 0.95f, 1.0f, 1));
        if (ui_button(ui, "scale.prev", "<", cx + 232, 222, 30, 24, 0)) {
            static const int scales[] = { 10, 64, 128, 256, 1000, 10000, 30000 };
            int cur = 0;
            for (int i = 0; i < 7; i++) if (scales[i] <= a->bots + 1) cur = i;
            cur = (cur + 6) % 7;
            if (scales[cur] > MAX_PLAYERS) a->mega_total = scales[cur];
            else { a->mega_total = 0; a->bots = scales[cur] - 1; }
        }
        if (ui_button(ui, "scale.next", ">", cx + 266, 222, 30, 24, 0)) {
            static const int scales[] = { 10, 64, 128, 256, 1000, 10000, 30000 };
            int cur = 0;
            for (int i = 0; i < 7; i++) if (scales[i] <= a->bots + 1) cur = i;
            cur = (cur + 1) % 7;
            if (scales[cur] > MAX_PLAYERS) a->mega_total = scales[cur];
            else { a->mega_total = 0; a->bots = scales[cur] - 1; }
        }

        ui_text(ui, cx + 16, 252, "MAP", 9, col(0.98f, 0.62f, 0.16f, 1));
        ui_text(ui, cx + 16, 267, a->world->name, 14, col(0.90f, 0.93f, 0.97f, 1));
        if (ui_button(ui, "map.prev", "<", cx + 16, 292, 28, 24, 0)) app_cycle_map(a, -1);
        if (ui_button(ui, "map.next", ">", cx + 48, 292, 28, 24, 0)) app_cycle_map(a, 1);
        snprintf(b, sizeof(b), "%d / %d", a->map_index + 1, a->map_count);
        ui_text(ui, cx + 86, 298, b, 9, col(0.45f, 0.50f, 0.58f, 1));
    } else if (a->page == PAGE_HOST) {
        ui_text(ui, 60, 162, "HOST ONLINE GAME", 20, col(0.95f, 0.97f, 1, 1));
        ui_text(ui, 60, 188, "NAT-freundlich: Raumcode teilen, Spieler treten ueber Lobby bei.", 10, col(0.55f, 0.62f, 0.72f, 1));
        float y = 222;
        ui_text(ui, 60, y, "ROOM NAME", 9, col(0.55f, 0.62f, 0.72f, 1));
        ui_textbox(ui, "host.room", 60, y + 14, 300, 28, a->host_room, sizeof(a->host_room), "Arena Deathmatch");
        y += 54;
        ui_text(ui, 60, y, "UDP PORT", 9, col(0.55f, 0.62f, 0.72f, 1));
        ui_textbox(ui, "host.port", 60, y + 14, 120, 28, a->host_port, sizeof(a->host_port), "7777");
        y += 54;
        ui_text(ui, 60, y, "SERVER PASSWORT / PASSKONTROLLE (optional)", 9, col(0.55f, 0.62f, 0.72f, 1));
        ui_textbox(ui, "host.password", 60, y + 14, 240, 28, a->host_password, sizeof(a->host_password), "offen");
        y += 54;
        ui_toggle(ui, "host.lobby", "PUBLIC VIA LOBBY / NAT RELAY", 60, y, 320, 26, &a->host_via_lobby);
        y += 36;
        if (a->host_via_lobby) {
            ui_text(ui, 60, y, "LOBBY SERVER (IP)", 9, col(0.55f, 0.62f, 0.72f, 1));
            ui_textbox(ui, "host.lobbyip", 60, y + 14, 220, 28, a->host_lobby, sizeof(a->host_lobby), "127.0.0.1");
            ui_textbox(ui, "host.lobbyport", 290, y + 14, 70, 28, a->host_lobby_port, sizeof(a->host_lobby_port), "7777");
            y += 54;
        }
        if (ui_button(ui, "host.start", "START SERVER", 60, y, 200, 42, 1)) {
            app_start_host(a);
        }
        if (ui_button(ui, "host.back", "ZURUECK", 270, y, 120, 42, 0)) {
            app_release_net(a);
            a->page = PAGE_MAIN;
        }
        y += 58;
        if (a->hosting && a->net && a->net->room_code[0]) {
            ui_text(ui, 60, y, "RAUMCODE", 9, col(0.55f, 0.62f, 0.72f, 1));
            ui_text(ui, 60, y + 14, a->net->room_code, 48, col(0.98f, 0.62f, 0.16f, 1));
            char b[128]; snprintf(b, sizeof(b), "Lobby: %s:%s", a->host_lobby, a->host_lobby_port);
            ui_text(ui, 60, y + 70, b, 10, col(0.55f, 0.62f, 0.72f, 1));
        }
        if (a->status[0]) ui_text(ui, 60, (float)h - 80, a->status, 10, col(0.65f, 0.7f, 0.78f, 1));
    } else if (a->page == PAGE_JOIN) {
        ui_text(ui, 60, 162, "JOIN GAME", 20, col(0.95f, 0.97f, 1, 1));
        ui_text(ui, 60, 188, "Direkt per IP (LAN) oder ueber Lobby-Raumcode (Internet/NAT).", 10, col(0.55f, 0.62f, 0.72f, 1));
        float y = 222;
        ui_toggle(ui, "join.lobby", "USE LOBBY CODE (NAT RELAY)", 60, y, 320, 26, &a->join_via_lobby);
        y += 38;
        if (a->join_via_lobby) {
            ui_text(ui, 60, y, "ROOM CODE", 9, col(0.55f, 0.62f, 0.72f, 1));
            ui_textbox(ui, "join.code", 60, y + 14, 140, 30, a->join_code, sizeof(a->join_code), "AB12");
            y += 58;
            ui_text(ui, 60, y, "LOBBY SERVER (IP)", 9, col(0.55f, 0.62f, 0.72f, 1));
            ui_textbox(ui, "join.lobbyip", 60, y + 14, 220, 28, a->join_lobby, sizeof(a->join_lobby), "127.0.0.1");
            ui_textbox(ui, "join.lobbyport", 290, y + 14, 70, 28, a->join_lobby_port, sizeof(a->join_lobby_port), "7777");
            y += 56;
        } else {
            ui_text(ui, 60, y, "HOST IP / PORT", 9, col(0.55f, 0.62f, 0.72f, 1));
            ui_textbox(ui, "join.ip", 60, y + 14, 240, 28, a->join_host, sizeof(a->join_host), "192.168.1.20");
            ui_textbox(ui, "join.port", 310, y + 14, 70, 28, a->join_port, sizeof(a->join_port), "7777");
            y += 56;
        }
        ui_text(ui, 60, y, "SERVER PASSWORT (falls geschuetzt)", 9, col(0.55f, 0.62f, 0.72f, 1));
        ui_textbox(ui, "join.password", 60, y + 14, 240, 28, a->join_password, sizeof(a->join_password), "offen");
        y += 58;
        if (ui_button(ui, "join.go", "CONNECT", 60, y, 200, 42, 1)) {
            app_start_join(a);
        }
        if (ui_button(ui, "join.back", "ZURUECK", 270, y, 120, 42, 0)) {
            a->page = PAGE_MAIN;
        }
        if (a->status[0]) ui_text(ui, 60, (float)h - 80, a->status, 10, col(0.65f, 0.7f, 0.78f, 1));
    } else {
        ui_text(ui, 60, 162, "SETTINGS", 20, col(0.95f, 0.97f, 1, 1));
        float y = 210;
        float bots = (float)a->bots;
        if (ui_slider(ui, "set.bots", "BOTS", 60, y, 330, 22, &bots, 1, 7)) a->bots = (int)(bots + 0.5f);
        y += 46;
        float diff = (float)a->difficulty;
        if (ui_slider(ui, "set.diff", "BOT SKILL", 60, y, 330, 22, &diff, 0, 6)) a->difficulty = (int)(diff + 0.5f);
        y += 46;
        if (ui_slider(ui, "set.frags", "FRAG LIMIT", 60, y, 330, 22, &a->frag_limit, 5, 50)) {}
        y += 46;
        if (ui_slider(ui, "set.vol", "MASTER VOLUME", 60, y, 330, 22, &a->volume, 0, 1)) audio_set_master(a->volume);
        y += 56;
        if (ui_toggle(ui, "set.full", "FULLSCREEN (F11)", 60, y, 260, 26, &a->fullscreen))
            app_set_fullscreen(a, a->fullscreen);
        if (ui_button(ui, "set.back", "ZURUECK", 60, y + 48, 200, 38, 0)) a->page = PAGE_MAIN;
        ui_text(ui, 60, (float)h - 80, "Aenderungen gelten fuer das naechste Match.", 10, col(0.55f, 0.62f, 0.72f, 1));
    }
}

/* ------------------------------------------------------------------ */
static void draw_pause(App* a, Ui* ui) {
    int w = a->w, h = a->h;
    ui_rect(ui, 0, 0, (float)w, (float)h, col(0, 0, 0, 0.62f));
    float pw = 420, ph = 330, px = w * 0.5f - pw * 0.5f, py = h * 0.5f - ph * 0.5f;
    ui_rect(ui, px, py, pw, ph, col(0.025f, 0.034f, 0.048f, 0.97f));
    ui_rect_outline(ui, px, py, pw, ph, col(0.2f, 0.26f, 0.33f, 1), 1);
    ui_rect(ui, px, py, pw, 3, col(0.98f, 0.62f, 0.16f, 1));
    ui_text(ui, px + 24, py + 20, "PAUSED", 28, col(0.97f, 0.98f, 1, 1));
    char b[128];
    snprintf(b, sizeof(b), "%s  //  %d FRAGS", a->game->world->name, a->game->players[a->game->local_player].frags);
    ui_text(ui, px + 24, py + 56, b, 10, col(0.55f, 0.62f, 0.72f, 1));
    if (ui_button(ui, "pause.resume", "RESUME", px + 24, py + 88, pw - 48, 38, 1)) {
        a->paused = false;
        capture_mouse(a, true);
    }
    if (ui_button(ui, "pause.restart", "RESTART MATCH", px + 24, py + 134, pw - 48, 34, 0)) {
        game_reset(a->game);
        a->paused = false;
        a->was_alive = false;
        capture_mouse(a, true);
    }
    if (ui_button(ui, "pause.editor", "OPEN EDITOR", px + 24, py + 176, pw - 48, 34, 0)) {
        app_release_net(a);
        a->mode = MODE_MENU;
        a->paused = false;
        capture_mouse(a, false);
        app_enter_editor(a);
    }
    if (ui_button(ui, "pause.quit", "QUIT TO MENU", px + 24, py + 218, pw - 48, 34, 2)) {
        app_release_net(a);
        a->mode = MODE_MENU;
        a->page = PAGE_MAIN;
        a->paused = false;
        capture_mouse(a, false);
        audio_music(MUS_MENU);
    }
    ui_text(ui, px + 24, py + ph - 42, "ESC = RESUME   //   F10 = MENU", 9, col(0.5f, 0.56f, 0.64f, 1));
}

/* ------------------------------------------------------------------ */
static void render_menu(App* a) {
    clear_scene(&a->r);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] clear_scene 0x%x\n", e); }
    renderer_begin(&a->r, a->menu_cam, a->world, 0, 0, a->w, a->h, a->time);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] renderer_begin 0x%x\n", e); }
    renderer_draw_world_static(&a->r, a->world);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] world_static 0x%x\n", e); }
    /* a few glowing pickups for life */
    fx_begin(&a->r);
    for (int i = 0; i < a->world->count; i++) {
        Actor* ac = &a->world->actors[i];
        if (ac->type == ACT_LIGHT) {
            float p = 0.6f + 0.4f * sinf(a->time * 1.6f + i);
            fx_sprite(&a->r, ac->pos, 1.2f + p * 0.5f, col(ac->color.r, ac->color.g, ac->color.b, 0.22f), true);
        }
    }
    fx_render(&a->r);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] fx 0x%x\n", e); }
    renderer_end(&a->r);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] renderer_end 0x%x\n", e); }
}

static void render_game(App* a) {
    Camera cam;
    game_get_camera(a->game, &cam);
    clear_scene(&a->r);
    renderer_begin(&a->r, cam, a->game->world, 0, 0, a->w, a->h, a->game->time);
    game_collect_lights(a->game, &a->r);
    game_render_world(a->game, &a->r, &cam);
    renderer_end(&a->r);
}

static void render_editor(App* a) {
    Editor* ed = a->ed;
    if (!ed) return;
    clear_scene(&a->r);
    Camera cam;
    editor_get_camera(ed, &cam);
    World* w = ed->pie_active ? &ed->pie_world : ed->world;
    renderer_begin(&a->r, cam, w, ed->vx, ed->vy, ed->vw, ed->vh, a->time);
    if (ed->pie_active) {
        game_collect_lights(&ed->pie_game, &a->r);
        game_render_world(&ed->pie_game, &a->r, &cam);
    } else {
        renderer_draw_world_static(&a->r, ed->world);
        editor_draw_3d(ed, &a->r);
    }
    renderer_end(&a->r);
}

/* ------------------------------------------------------------------ */
static void app_frame(App* a, float dt) {
    if (a->status_t > 0) a->status_t -= dt;

    /* Console has priority over every other input path. */
    bool con_input = app_console_key_input(a);
    if (!con_input) {
        app_update_kbd_cursor(a, dt);
        app_apply_kbd_click(a);
    }

    if (a->mode == MODE_MENU) {
        update_menu_camera(a, dt);
    } else if (a->mode == MODE_GAME) {
        game_frame(a, dt);
        if (a->game && a->game->event_count > 0) {
            /* Trigger / event actors run real console commands. */
            int n = a->game->event_count;
            a->game->event_count = 0;
            for (int i = 0; i < n; i++) {
                app_log_event(a, "trigger", a->game->events[i]);
                console_printf(&a->console, "event> %s", a->game->events[i]);
                app_console_execute(a, a->game->events[i]);
            }
        }
    } else if (a->mode == MODE_EDITOR) {
        if (!con_input) editor_frame(a, dt);
    }

    /* fps */
    a->fps_accum += dt;
    a->fps_frames++;
    if (a->fps_accum > 0.35f) {
        a->fps = (float)a->fps_frames / a->fps_accum;
        a->fps_accum = 0;
        a->fps_frames = 0;
    }
}

static void draw_virtual_cursor(App* a, Ui* ui) {
    if (!a->kbd_cursor) return;
    float x = a->kcx, y = a->kcy;
    Color sh = col(0, 0, 0, 0.55f);
    Color fg = col(1.0f, 0.92f, 0.72f, 1.0f);
    Color ac = col(0.98f, 0.62f, 0.16f, 1.0f);
    ui_tri(ui, v2(x + 2, y + 3), v2(x + 2, y + 23), v2(x + 17, y + 17), sh);
    ui_tri(ui, v2(x, y), v2(x, y + 20), v2(x + 14, y + 14), fg);
    ui_line(ui, v2(x, y), v2(x, y + 20), ac, 1);
    ui_line(ui, v2(x, y + 20), v2(x + 14, y + 14), ac, 1);
    ui_line(ui, v2(x + 14, y + 14), v2(x, y), ac, 1);
    /* small pulse ring so the virtual cursor is clearly distinguishable */
    ui_rect_outline(ui, x - 4, y - 4, 24, 28, col(0.98f, 0.62f, 0.16f, 0.35f), 1);
}

static void app_render(App* a) {
    if (a->mode == MODE_MENU) render_menu(a);
    else if (a->mode == MODE_GAME) render_game(a);
    else render_editor(a);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] after scene 0x%x mode=%d\n", e, a->mode); }

    UiInput uin;
    build_ui_input(a, &uin);
    ui_frame_begin(&a->ui, a->w, a->h, uin);
    float warp_x, warp_y;
    if (ui_nav_take_warp(&a->ui, &warp_x, &warp_y)) {
        a->kcx = warp_x;
        a->kcy = warp_y;
        a->kbd_cursor = true;
    }

    if (a->mode == MODE_MENU) {
        draw_menu(a, &a->ui);
    } else if (a->mode == MODE_GAME) {
        hud_draw(&a->ui, a->game, a->w, a->h);
        if (a->show_scores) hud_scoreboard(&a->ui, a->game, a->w, a->h);
        if (a->net) {
            const char* st = net_status(a->net);
            ui_text(&a->ui, 22, (float)a->h - 38, st, 9, col(0.45f, 0.62f, 0.78f, 0.9f));
        }
        if (a->net && a->net->role == NET_ROLE_CLIENT && !a->net->connected) {
            ui_rect(&a->ui, 0, 0, (float)a->w, (float)a->h, col(0.0f, 0.0f, 0.0f, 0.55f));
            ui_text_center(&a->ui, a->w * 0.5f, a->h * 0.4f, "CONNECTING ...", 30, col(1, 0.8f, 0.3f, 1));
            ui_text_center(&a->ui, a->w * 0.5f, a->h * 0.4f + 44, a->net->status, 12, col(0.8f, 0.85f, 0.9f, 1));
            ui_text_center(&a->ui, a->w * 0.5f, a->h * 0.4f + 70, "ESC = ABORT", 10, col(0.6f, 0.65f, 0.72f, 1));
        }
        if (a->paused) draw_pause(a, &a->ui);
    } else if (a->mode == MODE_EDITOR) {
        editor_draw_ui(a->ed, &a->ui, a->w, a->h);
    }

    draw_virtual_cursor(a, &a->ui);
    console_draw(&a->console, &a->ui, a->w, a->h);

    ui_frame_end(&a->ui);
    glUseProgram(0);
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] after ui 0x%x mode=%d\n", e, a->mode); }
}

/* ------------------------------------------------------------------ */
static void handle_events(App* a) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            a->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                a->w = e.window.data1;
                a->h = e.window.data2;
                renderer_resize(&a->r, a->w, a->h);
                a->ui.w = a->w; a->ui.h = a->h;
            }
            break;
        case SDL_MOUSEMOTION:
            if (e.motion.xrel || e.motion.yrel) a->kbd_cursor = false;
            if (a->mouse_captured) {
                a->dmx += (float)e.motion.xrel;
                a->dmy += (float)e.motion.yrel;
            } else {
                /* absolute delta for editor */
                a->dmx += (float)e.motion.xrel;
                a->dmy += (float)e.motion.yrel;
            }
            a->mx = (float)e.motion.x;
            a->my = (float)e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button >= 1 && e.button.button <= 3) {
                int b = e.button.button - 1;
                a->mb[b] = true;
                a->mb_press[b] = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button >= 1 && e.button.button <= 3) {
                int b = e.button.button - 1;
                a->mb[b] = false;
                a->mb_release[b] = true;
            }
            break;
        case SDL_MOUSEWHEEL:
            a->wheel += e.wheel.y;
            break;
        case SDL_TEXTINPUT:
            {
                int n = (int)strlen(e.text.text);
                if (a->text_len + n < (int)sizeof(a->text) - 1) {
                    memcpy(a->text + a->text_len, e.text.text, (size_t)n);
                    a->text_len += n;
                    a->text[a->text_len] = 0;
                }
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
                app_set_fullscreen(a, !a->fullscreen);
                break;
            }
            if (!e.key.repeat) a->key_edge[e.key.keysym.scancode] = true;
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) a->ui_escape = true;
            break;
        default:
            break;
        }
    }
}

static void reset_frame_edges(App* a) {
    a->dmx = a->dmy = 0;
    a->wheel = 0;
    if (a->kbd_click_down) {
        a->mb[0] = false;
        a->kbd_click_down = false;
    }
    for (int i = 0; i < 3; i++) a->mb_press[i] = a->mb_release[i] = false;
    memset(a->key_edge, 0, sizeof(a->key_edge));
    a->text_len = 0;
    a->text[0] = 0;
    a->ui_escape = false;
}

/* ------------------------------------------------------------------ */
static bool app_init(App* a, int w, int h, bool hidden, bool fullscreen) {
    memset(a, 0, sizeof(*a));
    a->w = w; a->h = h;
    a->bots = 4;
    a->difficulty = 3;
    a->frag_limit = 20;
    a->game_mode = GM_DM;
    a->mega_total = 0;
    a->volume = 0.8f;
    a->show_fps = true;
    a->sensitivity = 1.0f;
    a->fov = 90.0f;
    a->gravity = 24.0f;
    a->kbd_click_allowed = true;
    a->vsync = true;
    a->running = true;
    a->hidden = hidden;
    a->fullscreen = fullscreen;
    str_copy(a->host_room, "Arena Deathmatch", sizeof(a->host_room));
    str_copy(a->host_port, "7777", sizeof(a->host_port));
    a->log_enabled = true;
    str_copy(a->log_path, "arena2004.log", sizeof(a->log_path));
    str_copy(a->host_lobby, "127.0.0.1", sizeof(a->host_lobby));
    str_copy(a->host_lobby_port, "7777", sizeof(a->host_lobby_port));
    str_copy(a->join_host, "127.0.0.1", sizeof(a->join_host));
    str_copy(a->join_port, "7777", sizeof(a->join_port));
    str_copy(a->join_lobby, "127.0.0.1", sizeof(a->join_lobby));
    str_copy(a->join_lobby_port, "7777", sizeof(a->join_lobby_port));
    str_copy(a->join_code, "", sizeof(a->join_code));
    a->join_via_lobby = false;
    a->host_via_lobby = false;
    console_init(&a->console);
    a->kcx = w * 0.5f;
    a->kcy = h * 0.5f;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[sdl] init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (hidden) flags |= SDL_WINDOW_HIDDEN;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    a->win = SDL_CreateWindow("Arena2004 - OpenGL Arena FPS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w, h, flags);
    if (!a->win) { fprintf(stderr, "[sdl] window failed: %s\n", SDL_GetError()); return false; }
    a->gl = SDL_GL_CreateContext(a->win);
    if (!a->gl) { fprintf(stderr, "[gl] context failed: %s\n", SDL_GetError()); return false; }
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) { fprintf(stderr, "[glew] %s\n", glewGetErrorString(err)); return false; }
    while (glGetError() != GL_NO_ERROR) {}
    if (getenv("A2K4_GLDEBUG")) fprintf(stderr, "[gl] after glew clear\n");
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_TRUE);

    if (!renderer_init(&a->r, w, h)) return false;
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] renderer_init 0x%x\n", e); }
    if (!ui_init(&a->ui, "assets/fonts/DejaVuSans.ttf", w, h)) return false;
    if (getenv("A2K4_GLDEBUG")) { GLenum e = glGetError(); if (e) fprintf(stderr, "[gl] ui_init 0x%x\n", e); }
    audio_init();
    audio_set_music(a->volume * 0.5f);
    audio_music(MUS_MENU);

    plat_ensure_dir("shots");
    plat_ensure_dir("maps");
    a->world = calloc(1, sizeof(World));
    if (!a->world) return false;
    world_default_deck17(a->world);
    {
        const char* def = "maps/DM-Deck17.map";
        FILE* f = fopen(def, "rb");
        if (!f) world_save(a->world, def);
        else fclose(f);
    }
    app_scan_maps(a);
    a->game = calloc(1, sizeof(Game));
    if (!a->game) return false;
    a->net = calloc(1, sizeof(Net));
    if (!a->net) return false;
    a->net->sock = PLAT_INVALID_SOCKET;
    a->menu_cam.pos = v3(0, 16, 44);
    a->menu_cam.yaw = 0;
    a->menu_cam.pitch = -0.25f;
    a->menu_cam.fov = 72;
    camera_update(&a->menu_cam);
    a->mode = MODE_MENU;
    a->page = PAGE_MAIN;
    steam_bridge_init();
    app_status(a, "Willkommen in Arena 2004 - bereit fuer Deathmatch");
    return true;
}

static void app_shutdown(App* a) {
    steam_bridge_shutdown();
    if (a->ed) { editor_shutdown(a->ed); free(a->ed); a->ed = NULL; }
    if (a->net) { net_close(a->net); free(a->net); a->net = NULL; }
    if (a->game) { game_shutdown(a->game); free(a->game); a->game = NULL; }
    free(a->world); a->world = NULL;
    audio_shutdown();
    ui_destroy(&a->ui);
    renderer_destroy(&a->r);
    if (a->gl) SDL_GL_DeleteContext(a->gl);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
static void run_selftest(App* a) {
    printf("[selftest] start\n");
    fflush(stdout);
    /* menu frames */
    for (int i = 0; i < 40; i++) {
        app_frame(a, 1.0f / 60.0f);
        app_render(a);
        SDL_GL_SwapWindow(a->win);
        reset_frame_edges(a);
    }
    bool menu_png = save_screenshot(a->w, a->h, "shots/selftest_menu.png");
    printf("[selftest] menu render: %s glerr=0x%x\n", menu_png ? "ok" : "FAILED", glGetError());

    /* demo match */
    app_start_game(a, true, 5);
    int f = 0;
    for (; f < 260; f++) {
        app_frame(a, 1.0f / 60.0f);
        app_render(a);
        SDL_GL_SwapWindow(a->win);
        reset_frame_edges(a);
    }
    int alive = 0, active = 0;
    for (int i = 0; i < a->game->num_players; i++) {
        if (a->game->players[i].active) active++;
        if (a->game->players[i].alive) alive++;
    }
    int nav = a->game->nav.count;
    bool game_png = save_screenshot(a->w, a->h, "shots/selftest_game.png");
    printf("[selftest] game glerr=0x%x\n", glGetError());
    printf("[selftest] game render: %s  players=%d active=%d alive=%d navnodes=%d\n",
           game_png ? "ok" : "FAILED", a->game->num_players, active, alive, nav);

    /* game mode / scale smoke tests */
    bool modes_ok = true;
    const int test_modes[] = { GM_DM, GM_TDM, GM_CTF, GM_VCTF, GM_ASSAULT, GM_ONSLAUGHT, GM_CONQUEST, GM_RPG, GM_BATTLE_ROYALE };
    for (int mi = 0; mi < (int)(sizeof(test_modes) / sizeof(test_modes[0])); mi++) {
        game_set_mode(a->game, test_modes[mi]);
        game_reset(a->game);
        for (int i = 0; i < 30; i++) {
            app_frame(a, 1.0f / 60.0f);
            app_render(a);
            SDL_GL_SwapWindow(a->win);
            reset_frame_edges(a);
        }
        Game* g = a->game;
        bool ok = true;
        if (test_modes[mi] == GM_CTF || test_modes[mi] == GM_VCTF)
            ok = g->flags[0].active && g->flags[1].active;
        if (test_modes[mi] == GM_ASSAULT) ok = g->node_count == 2;
        if (test_modes[mi] == GM_ONSLAUGHT) ok = g->node_count == 3;
        if (test_modes[mi] == GM_CONQUEST) ok = g->node_count == 5;
        if (test_modes[mi] == GM_VCTF) ok = ok && g->vehicle_count >= 4;
        if (test_modes[mi] == GM_BATTLE_ROYALE) ok = g->zone_radius > 0 && g->br_total > 0;
        if (test_modes[mi] == GM_RPG) ok = g->players[0].level >= 1 && g->players[0].xp_next > 0;
        printf("[selftest] mode %-9s %s  nodes=%d vehicles=%d flags=%d zone=%.0f\n",
               game_mode_short(test_modes[mi]), ok ? "ok" : "FAILED",
               g->node_count, g->vehicle_count, (g->flags[0].active ? 1 : 0) + (g->flags[1].active ? 1 : 0), g->zone_radius);
        modes_ok = modes_ok && ok;
    }
    /* big-match scale + PlanetSide zones / Metin2 channels */
    game_set_mode(a->game, GM_CONQUEST);
    game_set_player_count(a->game, 256);
    game_reset(a->game);
    for (int i = 0; i < 20; i++) {
        app_frame(a, 1.0f / 60.0f);
        app_render(a);
        SDL_GL_SwapWindow(a->win);
        reset_frame_edges(a);
    }
    int big_players = a->game->num_players;
    game_configure_zones(a->game, 30000);
    bool zones_ok = a->game->zone_count >= 12 && a->game->chan_count >= 1 &&
                    a->game->zone_total == 30000;
    game_select_zone(a->game, 3);
    game_select_channel(a->game, 0);
    zones_ok = zones_ok && a->game->zone_id == 3 && a->game->chan_count >= 1;
    printf("[selftest] scale 256: players=%d  zones=%s total=%d zc=%d cc=%d zone=%s\n",
           big_players, zones_ok ? "ok" : "FAILED", a->game->zone_total,
           a->game->zone_count, a->game->chan_count, a->game->zone_label[a->game->zone_id]);
    game_set_player_count(a->game, 5);
    game_set_mode(a->game, GM_DM);
    game_reset(a->game);

    /* editor */
    app_release_net(a);
    app_enter_editor(a);
    bool editor_ok = a->ed != NULL;
    if (a->ed) {
        for (int i = 0; i < 30; i++) {
            app_frame(a, 1.0f / 60.0f);
            app_render(a);
            SDL_GL_SwapWindow(a->win);
            reset_frame_edges(a);
        }
        editor_ok = true;
    }
    bool editor_png = save_screenshot(a->w, a->h, "shots/selftest_editor.png");
    printf("[selftest] editor glerr=0x%x\n", glGetError());
    /* Play-in-editor smoke test */
    bool pie_ok = false;
    if (a->ed) {
        editor_play(a->ed);
        for (int i = 0; i < 45; i++) {
            app_frame(a, 1.0f / 60.0f);
            app_render(a);
            SDL_GL_SwapWindow(a->win);
            reset_frame_edges(a);
        }
        pie_ok = a->ed->pie_active && a->ed->pie_game.num_players > 0;
        save_screenshot(a->w, a->h, "shots/selftest_pie.png");
        editor_stop(a->ed);
    }
    printf("[selftest] editor: %s  png=%s  actors=%d  pie=%s\n", editor_ok ? "ok" : "FAILED",
           editor_png ? "ok" : "FAILED", a->world->count, pie_ok ? "ok" : "FAILED");

    /* navigation + map roundtrip */
    World tmp;
    memset(&tmp, 0, sizeof(tmp));
    bool save_ok = world_save(a->world, "maps/selftest.map");
    bool load_ok = save_ok && world_load(&tmp, "maps/selftest.map");
    printf("[selftest] map save/load: %s (%d actors)\n", load_ok ? "ok" : "FAILED", load_ok ? tmp.count : 0);
    remove("maps/selftest.map");

    /* networking sanity: relay server may not be running; test socket creation only */
    Net nt;
    memset(&nt, 0, sizeof(nt));
    bool net_ok = net_host(&nt, 7799, NULL, 0, "Test", 1, NULL);
    if (net_ok) net_close(&nt);
    printf("[selftest] network socket: %s\n", net_ok ? "ok" : "FAILED");
    printf("[selftest] audio: %s\n", audio_active() ? "ok" : "silent");
    GLenum glerr = glGetError();
    printf("[selftest] gl errors: 0x%x\n", glerr);
    printf("[selftest] %s\n", (menu_png && game_png && editor_png && editor_ok && load_ok && net_ok && pie_ok &&
                               modes_ok && zones_ok && glerr == GL_NO_ERROR) ? "PASS" : "FAIL");
    a->running = false;
}

static void run_benchmark(App* a, int frames) {
    app_start_game(a, true, 6);
    Uint32 t0 = SDL_GetTicks();
    for (int i = 0; i < frames; i++) {
        app_frame(a, 1.0f / 60.0f);
        app_render(a);
        SDL_GL_SwapWindow(a->win);
        reset_frame_edges(a);
    }
    Uint32 dt = SDL_GetTicks() - t0;
    printf("[benchmark] %d frames in %u ms -> %.1f fps (sim), %.0f fps smoothed\n",
           frames, dt, frames * 1000.0 / fmaxf((float)dt, 1.0f), a->fps);
    a->running = false;
}

/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    int width = 1600, height = 900;
    bool hidden = false, fullscreen = false;
    bool start_demo = false, start_editor = false;
    const char* map_path = NULL;
    const char* capture = NULL;
    int capture_frame = 120;
    int benchmark = 0;
    bool selftest = false;
    bool cli_host = false, cli_join = false;
    bool cli_netlog = false, cli_stats = false;
    float cli_seconds = 0.0f;
    char cli_join_host[64] = "", cli_lobby[64] = "", cli_code[16] = "", cli_room[40] = "";
    char cli_mode[24] = "", cli_scale[16] = "", cli_password[32] = "", cli_join_password[32] = "";
    bool cli_lobby_set = false;
    int cli_port = 7777, cli_lobby_port = 7777;

    for (int i = 1; i < argc; i++) {
        const char* s = argv[i];
        if (!strcmp(s, "--width") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(s, "--height") && i + 1 < argc) height = atoi(argv[++i]);
        else if (!strcmp(s, "--hidden")) hidden = true;
        else if (!strcmp(s, "--fullscreen")) fullscreen = true;
        else if (!strcmp(s, "--demo")) start_demo = true;
        else if (!strcmp(s, "--editor")) start_editor = true;
        else if (!strcmp(s, "--map") && i + 1 < argc) map_path = argv[++i];
        else if (!strcmp(s, "--capture") && i + 1 < argc) capture = argv[++i];
        else if (!strcmp(s, "--frames") && i + 1 < argc) capture_frame = atoi(argv[++i]);
        else if (!strcmp(s, "--benchmark") && i + 1 < argc) benchmark = atoi(argv[++i]);
        else if (!strcmp(s, "--selftest")) { selftest = true; hidden = true; }
        else if (!strcmp(s, "--netlog")) cli_netlog = true;
        else if (!strcmp(s, "--stats")) cli_stats = true;
        else if (!strcmp(s, "--seconds") && i + 1 < argc) cli_seconds = (float)atof(argv[++i]);
        else if (!strcmp(s, "--host")) cli_host = true;
        else if (!strcmp(s, "--join") && i + 1 < argc) { cli_join = true; str_copy(cli_join_host, argv[++i], sizeof(cli_join_host)); }
        else if (!strcmp(s, "--port") && i + 1 < argc) cli_port = atoi(argv[++i]);
        else if (!strcmp(s, "--lobby") && i + 1 < argc) {
            cli_lobby_set = true;
            const char* lv = argv[++i];
            if (strchr(lv, ':')) split_host_port(lv, cli_lobby, sizeof(cli_lobby), &cli_lobby_port);
            else str_copy(cli_lobby, lv, sizeof(cli_lobby));
        }
        else if (!strcmp(s, "--code") && i + 1 < argc) str_copy(cli_code, argv[++i], sizeof(cli_code));
        else if (!strcmp(s, "--room") && i + 1 < argc) str_copy(cli_room, argv[++i], sizeof(cli_room));
        else if (!strcmp(s, "--mode") && i + 1 < argc) str_copy(cli_mode, argv[++i], sizeof(cli_mode));
        else if (!strcmp(s, "--scale") && i + 1 < argc) str_copy(cli_scale, argv[++i], sizeof(cli_scale));
        else if (!strcmp(s, "--password") && i + 1 < argc) str_copy(cli_password, argv[++i], sizeof(cli_password));
        else if (!strcmp(s, "--joinpassword") && i + 1 < argc) str_copy(cli_join_password, argv[++i], sizeof(cli_join_password));
        else if (!strcmp(s, "--bots") && i + 1 < argc) { /* applied after app init */ }
        else if (!strcmp(s, "--help") || !strcmp(s, "-h")) {
            printf("Arena2004 - OpenGL arena FPS (Unreal Tournament 2004 style)\n\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("  --demo                 start a bot demo match\n");
            printf("  --editor               start the level editor\n");
            printf("  --map FILE             load a .map file\n");
            printf("  --host                 host an online game\n");
            printf("  --join IP              join a game by IP\n");
            printf("  --port N               UDP port (default 7777)\n");
            printf("  --lobby HOST[:PORT]    use lobby / NAT relay\n");
            printf("  --code CODE            lobby room code\n");
            printf("  --room NAME            room name when hosting\n");
            printf("  --mode NAME            dm|tdm|ctf|vctf|assault|onslaught|conquest|rpg|br\n");
            printf("  --scale N|5v5|32v32    match size (up to 30000 players/zones)\n");
            printf("  --bots N               bot count (large values act as scale)\n");
            printf("  --password PW          host password / access control\n");
            printf("  --joinpassword PW      client password\n");
            printf("  --capture FILE         save a screenshot after --frames\n");
            printf("  --frames N             frame count for capture/benchmark\n");
            printf("  --benchmark N          benchmark N frames, then exit\n");
            printf("  --selftest             automated sanity test (hidden window)\n");
            printf("  --seconds N            quit automatically after N seconds\n");
            printf("  --netlog               print network status once per second\n");
            printf("  --stats                print match statistics on exit\n");
            printf("  --width N --height N   window size\n");
            printf("  --hidden               hidden window\n");
            printf("  --fullscreen           fullscreen\n");
            return 0;
        }
    }
    int bots_override = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bots") && i + 1 < argc) bots_override = atoi(argv[i + 1]);
    }

    App* a = calloc(1, sizeof(App));
    if (!a) return 1;
    if (!app_init(a, width, height, hidden, fullscreen)) {
        app_shutdown(a);
        free(a);
        return 1;
    }
    if (bots_override >= 1) a->bots = bots_override;
    if (a->bots > MAX_PLAYERS - 1) a->bots = MAX_PLAYERS - 1;
    if (cli_mode[0]) {
        int m = game_mode_from_name(cli_mode);
        if (m >= 0) a->game_mode = m;
    }
    if (cli_scale[0]) {
        int total = parse_scale_total(cli_scale);
        if (total > 0) {
            if (total > MAX_PLAYERS) {
                a->mega_total = total;
                a->bots = MAX_PLAYERS - 1;
            } else {
                a->bots = total > 1 ? total - 1 : 0;
            }
        }
    }
    if (cli_password[0]) str_copy(a->host_password, cli_password, sizeof(a->host_password));
    if (cli_join_password[0]) str_copy(a->join_password, cli_join_password, sizeof(a->join_password));

    if (map_path) {
        if (!world_load(a->world, map_path)) app_status(a, "Map konnte nicht geladen werden: %s", map_path);
        else app_status(a, "Map geladen: %s (%d Actors)", map_path, a->world->count);
        app_scan_maps(a);
    }

    if (selftest) {
        run_selftest(a);
        app_shutdown(a);
        free(a);
        return 0;
    }
    if (benchmark > 0) {
        run_benchmark(a, benchmark);
        app_shutdown(a);
        free(a);
        return 0;
    }
    if (cli_join) {
        str_copy(a->join_host, cli_join_host, sizeof(a->join_host));
        snprintf(a->join_port, sizeof(a->join_port), "%d", cli_port);
        a->join_via_lobby = cli_lobby_set && cli_code[0];
        if (cli_lobby_set) {
            str_copy(a->join_lobby, cli_lobby, sizeof(a->join_lobby));
            snprintf(a->join_lobby_port, sizeof(a->join_lobby_port), "%d", cli_lobby_port);
        }
        if (cli_code[0]) {
            str_copy(a->join_code, cli_code, sizeof(a->join_code));
            a->join_via_lobby = true;
        }
        app_start_join(a);
    } else if (cli_host) {
        snprintf(a->host_port, sizeof(a->host_port), "%d", cli_port);
        a->host_via_lobby = cli_lobby_set;
        if (cli_lobby_set) {
            str_copy(a->host_lobby, cli_lobby, sizeof(a->host_lobby));
            snprintf(a->host_lobby_port, sizeof(a->host_lobby_port), "%d", cli_lobby_port);
        }
        if (cli_room[0]) str_copy(a->host_room, cli_room, sizeof(a->host_room));
        app_start_host(a);
    } else if (start_demo) {
        app_start_game(a, true, a->bots + 1);
    } else if (start_editor) {
        app_enter_editor(a);
    }

    if (capture) {
        a->capture_path = capture;
        a->capture_frame = capture_frame;
    }

    Uint32 last = SDL_GetTicks();
    int frame = 0;
    float elapsed = 0.0f;
    float next_netlog = 0.0f;
    while (a->running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0001f) dt = 0.0001f;
        last = now;
        elapsed += dt;

        handle_events(a);
        steam_bridge_tick();
        app_frame(a, dt);
        app_render(a);

        if (a->capture_path && frame == a->capture_frame) {
            bool ok = save_screenshot(a->w, a->h, a->capture_path);
            printf("[capture] %s -> %s\n", ok ? "saved" : "FAILED", a->capture_path);
            a->running = false;
        }
        SDL_GL_SwapWindow(a->win);
        reset_frame_edges(a);
        frame++;

        if (cli_netlog && elapsed >= next_netlog) {
            next_netlog = elapsed + 1.0f;
            printf("[net] t=%.1f %s\n", elapsed, a->net ? net_status(a->net) : "offline");
            fflush(stdout);
        }
        if (cli_seconds > 0.0f && elapsed >= cli_seconds) a->running = false;

        if (!a->no_swap_sleep) {
            Uint32 end = SDL_GetTicks();
            Uint32 spent = end - now;
            if (spent < 15) SDL_Delay(15 - spent);
        }
    }

    if (cli_stats && a->mode == MODE_GAME && a->game) {
        printf("[stats] elapsed=%.1fs players=%d navnodes=%d net=%s mode=%s score=%.0f:%.0f\n",
               elapsed, a->game->num_players, a->game->nav.count,
               a->game->server_mode ? "host" : (a->game->networked ? "client" : "local"),
               game_mode_short(a->game->game_mode),
               (float)a->game->team_score[0], (float)a->game->team_score[1]);
        if (a->game->zone_count > 1) {
            printf("[stats] zones=%d channels=%d total=%d active=%s\n",
                   a->game->zone_count, a->game->chan_count, a->game->zone_total,
                   a->game->zone_label[a->game->zone_id]);
        }
        for (int i = 0; i < a->game->num_players; i++) {
            Player* p = &a->game->players[i];
            printf("[stats] slot %d  %-16s  frags=%d deaths=%d  bot=%d alive=%d  pos=%.1f,%.1f,%.1f  weapon=%d\n",
                   i, p->name[0] ? p->name : "?", p->frags, p->deaths, p->bot ? 1 : 0, p->alive ? 1 : 0,
                   p->pos.x, p->pos.y, p->pos.z, p->weapon);
        }
    }
    app_shutdown(a);
    free(a);
    return 0;
}
