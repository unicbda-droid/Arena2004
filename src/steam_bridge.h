/* Arena2004 - optional Steamworks bridge.
 * The game builds and runs without the Steamworks SDK. If the SDK runtime
 * library is shipped next to the executable (or installed by Steam), this
 * bridge initialises it dynamically, so GitHub builds and DRM-free builds
 * stay completely independent from Steam. */
#ifndef ARENA_STEAM_BRIDGE_H
#define ARENA_STEAM_BRIDGE_H

#include <stdbool.h>

bool steam_bridge_init(void);
void steam_bridge_shutdown(void);
void steam_bridge_tick(void);
bool steam_bridge_active(void);
const char* steam_bridge_status(void);

#endif
