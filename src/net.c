#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "net.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>

static uint32_t now_ms(void) { return (uint32_t)SDL_GetTicks(); }

static plat_socket_t udp_socket(int port) {
    plat_socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == PLAT_INVALID_SOCKET) return PLAT_INVALID_SOCKET;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
        plat_close_socket(s);
        return PLAT_INVALID_SOCKET;
    }
    plat_set_nonblocking(s);
    return s;
}

static bool resolve(const char* host, int port, struct sockaddr_in* out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((uint16_t)port);
    if (!host || !host[0]) host = "127.0.0.1";
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) return true;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    out->sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

static int send_to(Net* n, const struct sockaddr_in* to, uint8_t type,
                   uint16_t player, uint32_t seq, uint32_t ack,
                   const void* payload, int len) {
    if (!n || n->sock == PLAT_INVALID_SOCKET || !to) return -1;
    unsigned char buf[NET_MAX_PKT];
    if (len < 0) len = 0;
    if (len > NET_MAX_PKT - (int)sizeof(PktHdr)) len = NET_MAX_PKT - (int)sizeof(PktHdr);
    PktHdr* h = (PktHdr*)buf;
    memset(h, 0, sizeof(*h));
    h->magic = NET_MAGIC;
    h->version = NET_VERSION;
    h->type = type;
    h->player = player;
    h->seq = seq;
    h->ack = ack;
    if (n->room_code[0]) snprintf(h->room, sizeof(h->room), "%.7s", n->room_code);
    if (len) memcpy(buf + sizeof(*h), payload, len);
    plat_ssize_t r = sendto(n->sock, (const char*)buf, (int)(sizeof(*h) + len), 0, (const struct sockaddr*)to, sizeof(*to));
    if (r > 0) { n->pkts_sent++; n->bytes_sent += (int)r; }
    return (int)r;
}

static void client_send(Net* n, uint8_t type, uint16_t player, uint32_t seq, uint32_t ack,
                        const void* payload, int len) {
    const struct sockaddr_in* to = n->use_lobby ? &n->lobby_addr : &n->host_addr;
    send_to(n, to, type, player, seq, ack, payload, len);
}

bool net_init(void) { return plat_socket_startup() == 0; }
void net_shutdown(void) { plat_socket_cleanup(); }

void net_close(Net* n) {
    if (!n) return;
    if (n->sock != PLAT_INVALID_SOCKET) plat_close_socket(n->sock);
    n->sock = PLAT_INVALID_SOCKET;
    n->role = NET_ROLE_NONE;
    n->connected = false;
}

bool net_host(Net* n, int port, const char* lobby_host, int lobby_port, const char* room_name, int bots, const char* password) {
    memset(n, 0, sizeof(*n));
    str_copy(n->password, password ? password : "", sizeof(n->password));
    n->sock = udp_socket(port);
    if (n->sock == PLAT_INVALID_SOCKET) { snprintf(n->status, sizeof(n->status), "Port %d belegt / nicht verfuegbar", port); return false; }
    n->role = NET_ROLE_HOST;
    n->local_slot = 0;
    n->connected = true;
    n->ping = 0;
    str_copy(n->room_name, room_name && room_name[0] ? room_name : "Arena Server", sizeof(n->room_name));
    (void)bots;
    if (lobby_host && lobby_host[0] && lobby_port > 0) {
        if (resolve(lobby_host, lobby_port, &n->lobby_addr)) {
            n->use_lobby = true;
            n->relay = true;
            PktRegister reg;
            memset(&reg, 0, sizeof(reg));
            str_copy(reg.name, n->room_name, sizeof(reg.name));
            reg.bots = (uint8_t)bots;
            send_to(n, &n->lobby_addr, PKT_REGISTER, 0, 1, 0, &reg, sizeof(reg));
            /* wait up to 2s for room code */
            uint32_t start = now_ms();
            while (now_ms() - start < 2000) {
                unsigned char buf[NET_MAX_PKT];
                struct sockaddr_in from;
                plat_socklen_t fl = sizeof(from);
                plat_ssize_t r = recvfrom(n->sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl);
                if (r >= (plat_ssize_t)sizeof(PktHdr)) {
                    PktHdr* h = (PktHdr*)buf;
                    if (h->magic == NET_MAGIC && h->type == PKT_CODE && r >= (plat_ssize_t)(sizeof(PktHdr) + sizeof(PktCode))) {
                        PktCode* c = (PktCode*)(buf + sizeof(PktHdr));
                        str_copy(n->room_code, c->code, sizeof(n->room_code));
                        str_copy(n->server_name, c->name, sizeof(n->server_name));
                        break;
                    }
                }
                SDL_Delay(4);
            }
            if (n->room_code[0]) {
                snprintf(n->status, sizeof(n->status), "Lobby-Raum %s (Server %s:%d)", n->room_code, lobby_host, lobby_port);
            } else {
                snprintf(n->status, sizeof(n->status), "Lobby nicht erreichbar - Direktmodus Port %d", port);
                n->use_lobby = false;
                n->relay = false;
            }
            return true;
        }
    }
    snprintf(n->status, sizeof(n->status), "Direkter Server auf Port %d", port);
    return true;
}

bool net_join(Net* n, const char* host, int port, const char* lobby_host, int lobby_port, const char* code, const char* password) {
    memset(n, 0, sizeof(*n));
    str_copy(n->password, password ? password : "", sizeof(n->password));
    n->sock = udp_socket(0);
    if (n->sock == PLAT_INVALID_SOCKET) { snprintf(n->status, sizeof(n->status), "Kein UDP-Socket"); return false; }
    n->role = NET_ROLE_CLIENT;
    n->local_slot = 0;
    n->handshake_token = now_ms() ^ 0xA5A5A5u;
    if (n->handshake_token == 0) n->handshake_token = 1;
    if (code && code[0] && lobby_host && lobby_host[0]) {
        if (!resolve(lobby_host, lobby_port, &n->lobby_addr)) {
            snprintf(n->status, sizeof(n->status), "Lobby-Adresse ungueltig");
            return false;
        }
        n->use_lobby = true;
        n->relay = true;
        str_copy(n->room_code, code, sizeof(n->room_code));
        /* lookup room */
        PktCode q;
        memset(&q, 0, sizeof(q));
        str_copy(q.code, code, sizeof(q.code));
        send_to(n, &n->lobby_addr, PKT_LOOKUP, 0, 2, 0, &q, sizeof(q));
        uint32_t start = now_ms();
        bool found = false;
        while (now_ms() - start < 2000) {
            unsigned char buf[NET_MAX_PKT];
            struct sockaddr_in from;
            plat_socklen_t fl = sizeof(from);
            plat_ssize_t r = recvfrom(n->sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl);
            if (r >= (plat_ssize_t)(sizeof(PktHdr) + sizeof(PktFound))) {
                PktHdr* h = (PktHdr*)buf;
                if (h->magic == NET_MAGIC && h->type == PKT_FOUND) {
                    PktFound* f = (PktFound*)(buf + sizeof(PktHdr));
                    if (f->ok) { str_copy(n->server_name, f->name, sizeof(n->server_name)); found = true; break; }
                }
            }
            SDL_Delay(4);
        }
        if (!found) {
            snprintf(n->status, sizeof(n->status), "Raum %s nicht gefunden", code);
            return false;
        }
        send_to(n, &n->lobby_addr, PKT_JOINROOM, 0, 3, 0, NULL, 0);
        snprintf(n->status, sizeof(n->status), "Verbinde ueber Lobby-Raum %s ...", code);
    } else {
        if (!resolve(host, port, &n->host_addr)) {
            snprintf(n->status, sizeof(n->status), "Host-Adresse ungueltig");
            return false;
        }
        snprintf(n->status, sizeof(n->status), "Verbinde mit %s:%d ...", host && host[0] ? host : "127.0.0.1", port);
    }
    /* send hello a few times */
    PktHello hello;
    memset(&hello, 0, sizeof(hello));
    str_copy(hello.name, "Player", sizeof(hello.name));
    hello.token = n->handshake_token;
    hello.bots = (uint8_t)0;
    str_copy(hello.password, n->password, sizeof(hello.password));
    for (int i = 0; i < 6; i++) {
        client_send(n, PKT_HELLO, 0, (uint32_t)i + 1, n->handshake_token, &hello, sizeof(hello));
        SDL_Delay(45);
    }
    snprintf(n->status, sizeof(n->status), "Warte auf Host-Antwort ...");
    return true;
}

/* ------------------------------------------------------------------ */
static void apply_snapshot(Net* n, Game* g) {
    if (!n->have_snapshot) return;
    int cnt = n->snap_count;
    if (cnt < 1) return;
    if (cnt > PKT_MAX_PLAYERS) cnt = PKT_MAX_PLAYERS;
    g->num_players = cnt;
    int ls = n->local_slot;
    if (ls < 0 || ls >= PKT_MAX_PLAYERS) ls = 0;
    g->local_player = ls;
    for (int i = 0; i < cnt; i++) {
        PktPlayer* sp = &n->snap[i];
        Player* p = &g->players[i];
        if (!p->active) {
            p->id = i;
            p->active = true;
            p->alive = sp->alive;
            p->health = 100;
            p->bot = sp->bot;
        }
        if (i != ls) {
            p->pos = v3(sp->x, sp->y, sp->z);
            p->render_pos = p->pos;
            p->yaw = sp->yaw;
            p->pitch = sp->pitch;
            p->render_yaw = sp->yaw;
            p->render_pitch = sp->pitch;
            p->vel = v3(0, 0, 0);
        } else {
            /* light correction only, keep local prediction */
            Vec3 sp_pos = v3(sp->x, sp->y, sp->z);
            if (v3len(v3sub(sp_pos, p->pos)) > 2.5f) p->pos = sp_pos;
        }
        p->health = sp->health;
        p->armor = sp->armor;
        p->frags = sp->frags;
        p->deaths = sp->deaths;
        p->weapon = sp->weapon;
        p->alive = sp->alive;
        p->bot = sp->bot;
        p->active = sp->active;
        if (p->weapon >= 0 && p->weapon < WPN_COUNT) p->has[p->weapon] = true;
        p->has[WPN_AR] = true;
        for (int w = 0; w < WPN_COUNT && w < 8; w++) p->ammo[w] = sp->ammo[w];
        str_copy(p->name, sp->name, sizeof(p->name));
    }
}

static void host_add_player(Net* n, Game* g, const PktHello* hello, const struct sockaddr_in* from, uint32_t token) {
    int slot = -1;
    for (int i = 1; i < PKT_MAX_PLAYERS; i++) {
        if (!n->remote_alive[i]) { slot = i; break; }
    }
    /* Listen-server etiquette: a human joining a full bot match takes over a
     * bot slot instead of being rejected. */
    if (slot < 0) {
        for (int i = 1; i < PKT_MAX_PLAYERS; i++) {
            if (i < g->num_players && g->players[i].active && g->players[i].bot) { slot = i; break; }
        }
    }
    if (slot < 0) return;
    if (slot >= g->num_players) g->num_players = slot + 1;
    n->remote_alive[slot] = true;
    n->remote_last_ms[slot] = now_ms();
    if (from) n->peers[slot] = *from;
    Player* p = &g->players[slot];
    p->id = slot;
    p->active = true;
    p->bot = false;
    str_copy(p->name, hello->name[0] ? hello->name : "Client", sizeof(p->name));
    if (!p->alive) game_respawn(g, p);
    p->protection = 1.0f;
    game_announce(g, "CLIENT CONNECTED", col3(0.4f, 1.0f, 0.55f), 2.0f);
    game_add_killfeed(g, "%s joined the match", p->name);
    PktWelcome wel;
    memset(&wel, 0, sizeof(wel));
    wel.token = token;
    wel.slot = (uint8_t)slot;
    wel.count = (uint8_t)(g->num_players > 255 ? 255 : g->num_players);
    wel.mode = (uint8_t)g->game_mode;
    if (n->use_lobby) send_to(n, &n->lobby_addr, PKT_WELCOME, (uint16_t)slot, 1, token, &wel, sizeof(wel));
    else if (from) send_to(n, from, PKT_WELCOME, (uint16_t)slot, 1, token, &wel, sizeof(wel));
    n->num_peers++;
    snprintf(n->status, sizeof(n->status), "%s verbunden als Slot %d", p->name, slot);
    n->status_t = 5.0f;
}

void net_poll(Net* n, Game* g, float dt) {
    if (!n || n->sock == PLAT_INVALID_SOCKET) return;
    if (n->status_t > 0) n->status_t -= dt;
    unsigned char buf[NET_MAX_PKT];
    for (;;) {
        struct sockaddr_in from;
        plat_socklen_t fl = sizeof(from);
        plat_ssize_t r = recvfrom(n->sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl);
        if (r <= 0) break;
        n->pkts_recv++;
        n->bytes_recv += (int)r;
        n->last_recv_ms = now_ms();
        if (r < (plat_ssize_t)sizeof(PktHdr)) continue;
        PktHdr* h = (PktHdr*)buf;
        if (h->magic != NET_MAGIC || h->version != NET_VERSION) continue;
        unsigned char* payload = buf + sizeof(PktHdr);
        int plen = (int)(r - sizeof(PktHdr));
        if (n->room_code[0] && h->room[0] && strncmp(h->room, n->room_code, sizeof(h->room)) != 0) continue;

        switch (h->type) {
        case PKT_PING:
            if (n->role == NET_ROLE_HOST) {
                if (n->use_lobby) send_to(n, &n->lobby_addr, PKT_PONG, h->player, h->seq, h->seq, NULL, 0);
                else send_to(n, &from, PKT_PONG, h->player, h->seq, h->seq, NULL, 0);
            }
            break;
        case PKT_PONG:
            if (n->role == NET_ROLE_CLIENT) {
                uint32_t t = h->ack;
                uint32_t d = now_ms() - t;
                n->ping = n->ping * 0.7f + (float)d * 0.3f;
            }
            break;
        case PKT_HELLO:
            if (n->role == NET_ROLE_HOST && plen >= (int)sizeof(PktHello)) {
                PktHello* hello = (PktHello*)payload;
                if (n->password[0] && strncmp(hello->password, n->password, sizeof(n->password) - 1) != 0) {
                    PktDenied den;
                    memset(&den, 0, sizeof(den));
                    str_copy(den.reason, "Passwort falsch / access denied", sizeof(den.reason));
                    if (n->use_lobby) send_to(n, &n->lobby_addr, PKT_DENIED, 0, 1, hello->token, &den, sizeof(den));
                    else send_to(n, &from, PKT_DENIED, 0, 1, hello->token, &den, sizeof(den));
                    snprintf(n->status, sizeof(n->status), "Beitritt abgelehnt (Passwort): %s", hello->name);
                    n->status_t = 6.0f;
                    break;
                }
                /* avoid duplicate welcome for the same token/remote */
                bool exists = false;
                for (int i = 1; i < PKT_MAX_PLAYERS; i++) {
                    if (n->remote_alive[i] && g->players[i].name[0] && strncmp(g->players[i].name, hello->name, 15) == 0) { exists = true; break; }
                }
                if (!exists) host_add_player(n, g, hello, &from, hello->token);
                else {
                    /* re-send welcome for retransmitted hellos */
                    uint32_t token = hello->token;
                    int slot = -1;
                    for (int i = 1; i < PKT_MAX_PLAYERS; i++)
                        if (n->remote_alive[i] && strncmp(g->players[i].name, hello->name, 15) == 0) { slot = i; break; }
                    if (slot >= 0) {
                        PktWelcome wel; memset(&wel, 0, sizeof(wel));
                        wel.token = token; wel.slot = (uint8_t)slot;
                        wel.count = (uint8_t)(g->num_players > 255 ? 255 : g->num_players);
                        wel.mode = (uint8_t)g->game_mode;
                        if (n->use_lobby) send_to(n, &n->lobby_addr, PKT_WELCOME, (uint16_t)slot, 1, token, &wel, sizeof(wel));
                        else send_to(n, &from, PKT_WELCOME, (uint16_t)slot, 1, token, &wel, sizeof(wel));
                    }
                }
            }
            break;
        case PKT_DENIED:
            if (n->role == NET_ROLE_CLIENT && plen >= (int)sizeof(PktDenied)) {
                PktDenied* den = (PktDenied*)payload;
                n->connected = false;
                n->handshake_done = false;
                snprintf(n->status, sizeof(n->status), "Zugriff verweigert: %s", den->reason[0] ? den->reason : "Passwort falsch");
                n->status_t = 8.0f;
            }
            break;
        case PKT_WELCOME:
            if (n->role == NET_ROLE_CLIENT && plen >= (int)sizeof(PktWelcome)) {
                PktWelcome* wel = (PktWelcome*)payload;
                if (wel->token == n->handshake_token) {
                    n->local_slot = wel->slot;
                    n->connected = true;
                    n->handshake_done = true;
                    g->local_player = wel->slot;
                    if (wel->count > g->num_players) g->num_players = wel->count;
                    if (wel->mode < 32) game_set_mode(g, wel->mode);
                    snprintf(n->status, sizeof(n->status), "Verbunden als Slot %d", wel->slot);
                    n->status_t = 5.0f;
                }
            }
            break;
        case PKT_INPUT:
            if (n->role == NET_ROLE_HOST && plen >= (int)sizeof(PlayerInput)) {
                int slot = h->player;
                if (slot >= 1 && slot < PKT_MAX_PLAYERS && n->remote_alive[slot]) {
                    memcpy(&g->players[slot].input, payload, sizeof(PlayerInput));
                    g->players[slot].bot = false;
                    g->players[slot].input.dodge = ((PlayerInput*)payload)->dodge;
                    n->remote_last_ms[slot] = now_ms();
                }
            }
            break;
        case PKT_SNAPSHOT:
            if (n->role == NET_ROLE_CLIENT && plen >= (int)sizeof(PktSnapHead)) {
                PktSnapHead* sh = (PktSnapHead*)payload;
                int cnt = sh->count;
                int start = sh->start;
                int total = sh->total;
                if (cnt < 0) cnt = 0;
                if (cnt > PKT_MAX_PLAYERS) cnt = PKT_MAX_PLAYERS;
                if (total < 0 || total > PKT_MAX_PLAYERS) total = PKT_MAX_PLAYERS;
                if (start < 0 || start > PKT_MAX_PLAYERS) start = 0;
                if (start + cnt > PKT_MAX_PLAYERS) cnt = PKT_MAX_PLAYERS - start;
                if (sh->frame != n->snap_frame) {
                    n->snap_frame = sh->frame;
                    n->snap_mask = 0;
                    n->snap_count = total;
                    n->have_snapshot = false;
                }
                if (cnt > 0 && plen >= (int)(sizeof(PktSnapHead) + cnt * sizeof(PktPlayer))) {
                    memcpy(&n->snap[start], payload + sizeof(PktSnapHead), cnt * sizeof(PktPlayer));
                    for (int k = 0; k < cnt; k++) n->snap_mask |= (1ull << (start + k));
                    uint64_t need = total >= 64 ? ~0ull : ((1ull << total) - 1ull);
                    if ((n->snap_mask & need) == need) {
                        n->snap_count = total;
                        n->have_snapshot = true;
                        n->snap_age = 0;
                        apply_snapshot(n, g);
                    }
                }
            }
            break;
        case PKT_BYE:
            if (n->role == NET_ROLE_HOST) {
                int slot = h->player;
                if (slot >= 1 && slot < PKT_MAX_PLAYERS) {
                    n->remote_alive[slot] = false;
                    g->players[slot].bot = true;
                    g->players[slot].name[0] = 0;
                    str_copy(g->players[slot].name, "Bot", sizeof(g->players[slot].name));
                    g->players[slot].bot_think_t = 0;
                }
            } else if (n->role == NET_ROLE_CLIENT) {
                n->connected = false;
                snprintf(n->status, sizeof(n->status), "Host hat die Verbindung beendet");
                n->status_t = 10.0f;
            }
            break;
        case PKT_CODE:
            if (n->role == NET_ROLE_HOST && plen >= (int)sizeof(PktCode)) {
                PktCode* c = (PktCode*)payload;
                str_copy(n->room_code, c->code, sizeof(n->room_code));
                if (c->name[0]) str_copy(n->server_name, c->name, sizeof(n->server_name));
            }
            break;
        case PKT_FOUND:
            break;
        default:
            break;
        }
    }
    /* client timeouts */
    if (n->role == NET_ROLE_CLIENT && n->connected && now_ms() - n->last_recv_ms > 8000) {
        n->connected = false;
        snprintf(n->status, sizeof(n->status), "Verbindung verloren (Timeout)");
        n->status_t = 10.0f;
    }
    if (n->role == NET_ROLE_HOST) {
        for (int i = 1; i < PKT_MAX_PLAYERS; i++) {
            if (n->remote_alive[i] && now_ms() - n->remote_last_ms[i] > 10000) {
                n->remote_alive[i] = false;
                g->players[i].bot = true;
                str_copy(g->players[i].name, "Bot", sizeof(g->players[i].name));
                g->players[i].bot_think_t = 0;
            }
        }
    }
}

void net_host_apply(Net* n, Game* g) {
    (void)n;
    (void)g;
}

void net_client_send_input(Net* n, Game* g, PlayerInput in, float dt) {
    (void)g;
    if (!n || n->role != NET_ROLE_CLIENT) return;
    n->input_accum += dt;
    if (n->input_accum < 1.0f / 30.0f) return;
    n->input_accum = 0;
    n->seq++;
    client_send(n, PKT_INPUT, (uint16_t)n->local_slot, n->seq, 0, &in, sizeof(in));
    n->ping_timer += dt;
    if (now_ms() - n->ping_sent_ms > 1000) {
        n->ping_sent_ms = now_ms();
        client_send(n, PKT_PING, (uint16_t)n->local_slot, now_ms(), 0, NULL, 0);
    }
}

void net_host_broadcast(Net* n, Game* g, float dt) {
    if (!n || n->role != NET_ROLE_HOST) return;
    n->snap_accum += dt;
    if (n->snap_accum < 1.0f / 20.0f) return;
    n->snap_accum = 0;
    static unsigned char buf[NET_MAX_PKT];
    int cnt = g->num_players;
    if (cnt > PKT_MAX_PLAYERS) cnt = PKT_MAX_PLAYERS;
    if (cnt < 1) return;
    int per = (int)((NET_MAX_PKT - sizeof(PktHdr) - sizeof(PktSnapHead)) / sizeof(PktPlayer));
    if (per < 1) per = 1;
    if (per > PKT_MAX_PLAYERS) per = PKT_MAX_PLAYERS;
    uint8_t frame = (uint8_t)(n->snap_frame_out + 1);
    n->snap_frame_out = frame;
    for (int start = 0; start < cnt; start += per) {
        int chunk = cnt - start;
        if (chunk > per) chunk = per;
        PktSnapHead* sh = (PktSnapHead*)buf;
        sh->count = (uint8_t)chunk;
        sh->start = (uint8_t)start;
        sh->total = (uint8_t)cnt;
        sh->frame = frame;
        PktPlayer* pp = (PktPlayer*)(buf + sizeof(PktSnapHead));
        for (int k = 0; k < chunk; k++) {
            int i = start + k;
            Player* p = &g->players[i];
            PktPlayer* o = &pp[k];
            memset(o, 0, sizeof(*o));
            o->x = p->pos.x; o->y = p->pos.y; o->z = p->pos.z;
            o->yaw = p->yaw; o->pitch = p->pitch;
            o->health = p->health; o->armor = p->armor;
            o->frags = p->frags; o->deaths = p->deaths;
            for (int w = 0; w < WPN_COUNT && w < 8; w++) o->ammo[w] = (uint16_t)clampf((float)p->ammo[w], 0, 999);
            o->weapon = (uint8_t)(p->weapon < 0 ? 0 : p->weapon);
            o->alive = p->alive ? 1 : 0;
            o->bot = p->bot ? 1 : 0;
            o->active = p->active ? 1 : 0;
            o->is_local = (i == g->local_player) ? 1 : 0;
            str_copy(o->name, p->name, sizeof(o->name));
        }
        int len = (int)(sizeof(PktSnapHead) + chunk * sizeof(PktPlayer));
        if (n->use_lobby) send_to(n, &n->lobby_addr, PKT_SNAPSHOT, 0, ++n->seq, 0, buf, len);
        else {
            for (int i = 0; i < PKT_MAX_PLAYERS; i++)
                if (n->remote_alive[i]) send_to(n, &n->peers[i], PKT_SNAPSHOT, 0, ++n->seq, 0, buf, len);
        }
    }
}

const char* net_status(Net* n) {
    if (!n) return "";
    if (n->status_t <= 0 && n->connected) {
        if (n->role == NET_ROLE_HOST)
            snprintf(n->status, sizeof(n->status), "Host Port aktiv  |  Raum %s  |  Peers %d  |  Ping %.0f ms",
                     n->room_code[0] ? n->room_code : "-", n->num_peers, n->ping);
        else
            snprintf(n->status, sizeof(n->status), "Client Slot %d  |  Ping %.0f ms  |  %d/%d Pakete",
                     n->local_slot, n->ping, n->pkts_recv, n->pkts_sent);
    }
    return n->status;
}
