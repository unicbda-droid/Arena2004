/* Arena2004 - networking: direct UDP + NAT-friendly lobby relay */
#ifndef NET_H
#define NET_H

#include "game.h"
#include "netproto.h"
#include "platform.h"
#include <netinet/in.h>

typedef enum { NET_ROLE_NONE = 0, NET_ROLE_HOST, NET_ROLE_CLIENT } NetRole;

typedef struct {
    float x, y, z;
    float yaw, pitch;
    float hp, max_hp;
    float respawn;
    int driver;
    int type, team;
    bool active;
    uint8_t module[VEH_MODULES];
} NetVehicleState;

typedef struct {
    NetRole role;
    plat_socket_t sock;
    struct sockaddr_in host_addr;   /* client: direct host */
    struct sockaddr_in lobby_addr;  /* relay / lobby server */
    bool relay;
    bool use_lobby;
    bool connected;
    bool handshake_done;
    int local_slot;
    uint32_t seq, ack;
    uint32_t handshake_token;
    uint32_t last_recv_ms;
    float ping;
    uint32_t ping_sent_ms;
    float ping_timer;
    float input_accum;
    float snap_accum;
    /* client: latest host snapshot */
    bool have_snapshot;
    int snap_count;
    PktPlayer snap[PKT_MAX_PLAYERS];
    uint64_t snap_mask;      /* received chunks of the current snapshot */
    uint8_t snap_frame;      /* snapshot generation */
    float snap_age;
    /* invasion state mirrored from the host snapshot */
    int wave, hostiles, queued, survivors;
    /* client: latest host vehicle state, interpolated between packets */
    bool veh_have;
    int veh_count;
    NetVehicleState veh[PKT_MAX_VEHICLES];
    float veh_age;
    /* host: remote players */
    bool remote_alive[PKT_MAX_PLAYERS];
    uint8_t snap_frame_out;
    uint8_t veh_frame_out;
    float veh_accum;
    uint32_t remote_last_ms[PKT_MAX_PLAYERS];
    struct sockaddr_in peers[PKT_MAX_PLAYERS];
    /* lobby registration */
    char room_code[16];
    char room_name[24];
    char server_name[24];
    char password[32];
    int num_peers;
    /* diagnostics */
    char status[160];
    int pkts_sent, pkts_recv;
    int bytes_sent, bytes_recv;
    float status_t;
} Net;

bool net_init(void);
void net_shutdown(void);
bool net_host(Net* n, int port, const char* lobby_host, int lobby_port, const char* room_name, int bots, const char* password);
bool net_join(Net* n, const char* host, int port, const char* lobby_host, int lobby_port, const char* code, const char* password);
void net_close(Net* n);
void net_poll(Net* n, Game* g, float dt);
void net_client_send_input(Net* n, Game* g, PlayerInput in, float dt);
void net_host_broadcast(Net* n, Game* g, float dt);
void net_host_apply(Net* n, Game* g);
const char* net_status(Net* n);

#endif
