/* Arena2004 - UDP wire protocol shared by game and lobby server */
#ifndef NETPROTO_H
#define NETPROTO_H

#include <stdint.h>
#include <stdbool.h>

#define NET_MAGIC 0x41324B34u   /* "A2K4" */
#define NET_VERSION 7
#define NET_MAX_PKT 1200

typedef enum {
    PKT_PING = 1,
    PKT_PONG,
    PKT_HELLO,
    PKT_WELCOME,
    PKT_INPUT,
    PKT_SNAPSHOT,
    PKT_BYE,
    PKT_LOOKUP,
    PKT_REGISTER,
    PKT_CODE,
    PKT_FOUND,
    PKT_JOINROOM,
    PKT_DENIED,
    PKT_VEHICLES
} PktType;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t player;
    uint32_t seq;
    uint32_t ack;
    char room[8];
} PktHdr;

typedef struct {
    char name[16];
    uint32_t token;
    uint8_t bots;
    uint8_t pad[3];
    char password[32];
} PktHello;

typedef struct {
    char reason[48];
} PktDenied;

typedef struct {
    uint32_t token;
    uint8_t slot;
    uint8_t count;
    uint8_t mode;      /* GameMode of the host (GM_*) */
    uint8_t pad;
} PktWelcome;

typedef struct {
    uint8_t count;   /* players in this chunk */
    uint8_t start;   /* first player slot in this chunk */
    uint8_t total;   /* total players in the snapshot */
    uint8_t frame;   /* snapshot generation, wraps at 255 */
    /* invasion wave state, identical for every chunk of the snapshot */
    uint8_t wave, hostiles, queued, survivors;
} PktSnapHead;

typedef struct {
    float x, y, z;
    float yaw, pitch;
    float health, armor;
    int32_t frags, deaths;
    uint16_t ammo[8];
    uint8_t weapon;
    uint8_t alive;
    uint8_t bot;
    uint8_t active;
    uint8_t is_local;
    uint8_t zombie;     /* invasion zombie slot */
    uint8_t zombie_type;/* 0 walker, 1 runner, 2 brute, 3 spitter */
    uint8_t pad;
    float death_time;   /* corpse age: same dissolve on every client */
    char name[16];
} PktPlayer;

/* UT2004 listen-server scale. A single snapshot is split into chunks so it
 * always fits into NET_MAX_PKT. */
#define PKT_MAX_PLAYERS 32

/* Vehicle state is sent as its own compact packet at 15 Hz. 16 vehicles
 * (the engine limit) fit into one datagram without chunking. */
#define PKT_MAX_VEHICLES 16

typedef struct {
    uint8_t count;
    uint8_t frame;
    uint8_t pad[2];
} PktVehHead;

typedef struct {
    float x, y, z;
    float yaw, pitch;
    float hp, max_hp;
    float respawn;      /* remaining wreck time, identical on all clients */
    int32_t driver;
    uint8_t type;
    uint8_t team;
    uint8_t active;
    uint8_t pad;
    /* module HP as 0..255 percent (wheels, engine, weapon). Sent so a
     * client renders the same detached wheels / smoke / dead barrel. */
    uint8_t module[6];
} PktVehicle;

typedef struct {
    char name[24];
    uint8_t bots;
    uint8_t pad[3];
} PktRegister;

typedef struct {
    char code[8];
    char name[24];
} PktCode;

typedef struct {
    uint8_t ok;
    uint8_t pad[3];
    char name[24];
} PktFound;

#endif
