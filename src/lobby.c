#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* Arena2004 lobby / NAT relay server
 * Usage: ./arena2004_lobby [port]   (default 7777/udp)
 * Hosts register a room and receive a short code. Clients join by code.
 * All game packets are relayed through this server, so neither host nor
 * client needs a forwarded port (works behind NAT). */
#include "netproto.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#define MAX_ROOMS 64
#define MAX_MEMBERS 9
#define ROOM_TIMEOUT_MS 180000
#define MEMBER_TIMEOUT_MS 30000

typedef struct {
    char code[8];
    char name[24];
    struct sockaddr_in members[MAX_MEMBERS];
    int member_count;
    uint32_t last_ms;
    bool used;
} Room;

static Room g_rooms[MAX_ROOMS];
static plat_socket_t g_sock = PLAT_INVALID_SOCKET;
static volatile sig_atomic_t g_run = 1;

static uint32_t now_ms(void) {
#ifdef _WIN32
    return (uint32_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
#endif
}

static bool addr_eq(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void addr_str(const struct sockaddr_in* a, char* out, int cap) {
    char ip[64];
    char tmp[96];
    inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip));
    snprintf(tmp, sizeof(tmp), "%s:%d", ip, ntohs(a->sin_port));
    int i = 0;
    for (; tmp[i] && i < cap - 1; i++) out[i] = tmp[i];
    if (cap > 0) out[i] = 0;
}

static void make_code(char* out) {
    static const char* al = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int tries = 0; tries < 200; tries++) {
        for (int i = 0; i < 4; i++) out[i] = al[rand() % 32];
        out[4] = 0;
        bool clash = false;
        for (int i = 0; i < MAX_ROOMS; i++)
            if (g_rooms[i].used && strcmp(g_rooms[i].code, out) == 0) { clash = true; break; }
        if (!clash) return;
    }
    snprintf(out, 8, "%04X", (unsigned)(rand() & 0xffff));
}

static Room* find_room(const char* code) {
    for (int i = 0; i < MAX_ROOMS; i++)
        if (g_rooms[i].used && strncmp(g_rooms[i].code, code, 4) == 0) return &g_rooms[i];
    return NULL;
}

static Room* alloc_room(void) {
    for (int i = 0; i < MAX_ROOMS; i++) if (!g_rooms[i].used) return &g_rooms[i];
    /* recycle oldest */
    int oldest = 0;
    for (int i = 1; i < MAX_ROOMS; i++) if (g_rooms[i].last_ms < g_rooms[oldest].last_ms) oldest = i;
    return &g_rooms[oldest];
}

static void send_pkt(const struct sockaddr_in* to, uint8_t type, const void* payload, int len) {
    unsigned char buf[NET_MAX_PKT];
    PktHdr h;
    memset(&h, 0, sizeof(h));
    h.magic = NET_MAGIC;
    h.version = NET_VERSION;
    h.type = type;
    if (len > NET_MAX_PKT - (int)sizeof(h)) len = NET_MAX_PKT - (int)sizeof(h);
    memcpy(buf, &h, sizeof(h));
    if (len) memcpy(buf + sizeof(h), payload, len);
    sendto(g_sock, (const char*)buf, (int)(sizeof(h) + len), 0, (const struct sockaddr*)to, sizeof(*to));
}

static void send_found(const struct sockaddr_in* to, bool ok, const char* name) {
    PktFound f;
    memset(&f, 0, sizeof(f));
    f.ok = ok ? 1 : 0;
    if (name) snprintf(f.name, sizeof(f.name), "%s", name);
    send_pkt(to, PKT_FOUND, &f, sizeof(f));
}

static void handle_packet(unsigned char* buf, int r, const struct sockaddr_in* from, char* log, int logcap) {
    if (r < (int)sizeof(PktHdr)) return;
    PktHdr* h = (PktHdr*)buf;
    if (h->magic != NET_MAGIC || h->version != NET_VERSION) return;
    unsigned char* payload = buf + sizeof(PktHdr);
    int plen = r - (int)sizeof(PktHdr);
    char a[64];
    addr_str(from, a, sizeof(a));

    if (h->type == PKT_REGISTER) {
        if (plen < (int)sizeof(PktRegister)) return;
        PktRegister* reg = (PktRegister*)payload;
        Room* room = alloc_room();
        memset(room, 0, sizeof(*room));
        room->used = true;
        make_code(room->code);
        if (reg->name[0]) snprintf(room->name, sizeof(room->name), "%s", reg->name);
        else snprintf(room->name, sizeof(room->name), "Arena Deathmatch");
        room->members[0] = *from;
        room->member_count = 1;
        room->last_ms = now_ms();
        PktCode c;
        memset(&c, 0, sizeof(c));
        snprintf(c.code, sizeof(c.code), "%s", room->code);
        snprintf(c.name, sizeof(c.name), "%s", room->name);
        send_pkt(from, PKT_CODE, &c, sizeof(c));
        snprintf(log, logcap, "REGISTER %s '%s' -> room %s", a, room->name, room->code);
        return;
    }
    if (h->type == PKT_LOOKUP) {
        if (plen < (int)sizeof(PktCode)) return;
        PktCode* q = (PktCode*)payload;
        Room* room = find_room(q->code);
        if (room) { send_found(from, true, room->name); snprintf(log, logcap, "LOOKUP %s -> %s from %s", q->code, room->name, a); }
        else { send_found(from, false, NULL); snprintf(log, logcap, "LOOKUP %s -> not found from %s", q->code, a); }
        return;
    }
    if (h->type == PKT_JOINROOM) {
        Room* room = find_room(h->room);
        if (!room) { send_found(from, false, NULL); return; }
        bool have = false;
        for (int i = 0; i < room->member_count; i++) if (addr_eq(&room->members[i], from)) { have = true; break; }
        if (!have && room->member_count < MAX_MEMBERS) {
            room->members[room->member_count++] = *from;
        }
        room->last_ms = now_ms();
        send_found(from, true, room->name);
        snprintf(log, logcap, "JOIN %s -> room %s (%d members)", a, room->code, room->member_count);
        return;
    }
    /* everything else: relay to other room members */
    Room* room = find_room(h->room);
    if (!room) return;
    bool member = false;
    for (int i = 0; i < room->member_count; i++) if (addr_eq(&room->members[i], from)) { member = true; break; }
    if (!member) {
        /* allow client that came through lookup without explicit join */
        if (room->member_count < MAX_MEMBERS) { room->members[room->member_count++] = *from; member = true; }
        else return;
    }
    room->last_ms = now_ms();
    for (int i = 0; i < room->member_count; i++) {
        if (addr_eq(&room->members[i], from)) continue;
        sendto(g_sock, (const char*)buf, r, 0, (const struct sockaddr*)&room->members[i], sizeof(room->members[i]));
    }
    snprintf(log, logcap, "RELAY room %s type %d from %s (%d bytes)", room->code, h->type, a, r);
}

static void on_sigint(int sig) { (void)sig; g_run = 0; }

int main(int argc, char** argv) {
    int port = 7777;
    if (argc > 1) port = atoi(argv[1]);
    if (port <= 0) port = 7777;
    srand((unsigned)time(NULL) ^ (unsigned)plat_getpid());
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    if (plat_socket_startup() != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == PLAT_INVALID_SOCKET) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(g_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) { perror("bind"); return 1; }
    plat_set_nonblocking(g_sock);

    printf("Arena2004 Lobby / NAT Relay\n");
    printf("UDP port : %d\n", port);
    printf("Hosts    : register room, share 4-letter code\n");
    printf("Clients  : join with code (relayed through this server)\n");
    printf("CTRL+C   : shutdown\n");
    fflush(stdout);

    unsigned char buf[NET_MAX_PKT];
    uint32_t last_house = now_ms();
    while (g_run) {
        struct sockaddr_in from;
        plat_socklen_t fl2 = sizeof(from);
        plat_ssize_t r = recvfrom(g_sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl2);
        if (r > 0) {
            char log[256];
            snprintf(log, sizeof(log), "%s", "");
            handle_packet(buf, (int)r, &from, log, sizeof(log));
            if (log[0]) { printf("[lobby] %s\n", log); fflush(stdout); }
        } else {
            plat_sleep_ms(2);
        }
        if (now_ms() - last_house > 5000) {
            last_house = now_ms();
            for (int i = 0; i < MAX_ROOMS; i++) {
                if (g_rooms[i].used && now_ms() - g_rooms[i].last_ms > ROOM_TIMEOUT_MS) {
                    printf("[lobby] room %s expired\n", g_rooms[i].code);
                    g_rooms[i].used = false;
                }
            }
            /* drop stale members */
            for (int i = 0; i < MAX_ROOMS; i++) {
                if (!g_rooms[i].used) continue;
                for (int m = 0; m < g_rooms[i].member_count; m++) { (void)m; }
            }
            fflush(stdout);
        }
    }
    plat_close_socket(g_sock);
    plat_socket_cleanup();
    printf("\n[lobby] shutdown\n");
    return 0;
}
