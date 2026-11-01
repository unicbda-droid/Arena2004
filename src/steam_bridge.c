#include "steam_bridge.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  typedef HMODULE steam_lib_t;
  #define STEAM_OPEN(name) LoadLibraryA(name)
  #define STEAM_SYM(lib, name) ((void*)GetProcAddress((lib), (name)))
  #define STEAM_CLOSE(lib) FreeLibrary(lib)
#else
  #include <dlfcn.h>
  typedef void* steam_lib_t;
  #define STEAM_OPEN(name) dlopen((name), RTLD_LAZY | RTLD_LOCAL)
  #define STEAM_SYM(lib, name) dlsym((lib), (name))
  #define STEAM_CLOSE(lib) dlclose(lib)
#endif

typedef int  (*steam_init_fn)(void);
typedef void (*steam_void_fn)(void);
typedef int  (*steam_int_fn)(void);

static steam_lib_t g_lib;
static steam_init_fn g_init;
static steam_void_fn g_shutdown;
static steam_void_fn g_callbacks;
static bool g_active;
static char g_status[192] = "Steam: not loaded (DRM-free build)";

#ifdef _WIN32
static const char* STEAM_LIBS[] = {
    "steam_api64.dll", "steam_api.dll", "steam/steam_api64.dll", "steam/steam_api.dll"
};
#elif defined(__APPLE__)
static const char* STEAM_LIBS[] = {
    "libsteam_api.dylib", "./libsteam_api.dylib",
    "steam/libsteam_api.dylib", "../Frameworks/libsteam_api.dylib"
};
#else
static const char* STEAM_LIBS[] = {
    "libsteam_api.so", "./libsteam_api.so",
    "steam/libsteam_api.so", "../lib/libsteam_api.so", "/usr/lib/libsteam_api.so"
};
#endif

bool steam_bridge_init(void) {
    if (g_active) return true;
    for (size_t i = 0; i < sizeof(STEAM_LIBS) / sizeof(STEAM_LIBS[0]); i++) {
        g_lib = STEAM_OPEN(STEAM_LIBS[i]);
        if (g_lib) break;
    }
    if (!g_lib) {
        snprintf(g_status, sizeof(g_status), "Steam: runtime not found (DRM-free build)");
        return false;
    }
    g_init = (steam_init_fn)STEAM_SYM(g_lib, "SteamAPI_Init");
    if (!g_init) g_init = (steam_init_fn)STEAM_SYM(g_lib, "SteamAPI_InitFlat");
    g_shutdown = (steam_void_fn)STEAM_SYM(g_lib, "SteamAPI_Shutdown");
    g_callbacks = (steam_void_fn)STEAM_SYM(g_lib, "SteamAPI_RunCallbacks");
    steam_int_fn running = (steam_int_fn)STEAM_SYM(g_lib, "SteamAPI_IsSteamRunning");
    if (!g_init) {
        snprintf(g_status, sizeof(g_status), "Steam: runtime found, but SteamAPI_Init missing");
        STEAM_CLOSE(g_lib);
        g_lib = NULL;
        return false;
    }
    if (running && !running()) {
        snprintf(g_status, sizeof(g_status), "Steam: client not running (offline mode)");
        STEAM_CLOSE(g_lib);
        g_lib = NULL;
        return false;
    }
    if (!g_init()) {
        snprintf(g_status, sizeof(g_status), "Steam: SteamAPI_Init failed (app id / steam_appid.txt?)");
        g_lib = NULL;
        return false;
    }
    g_active = true;
    snprintf(g_status, sizeof(g_status), "Steam: active (lobby / overlay / cloud available)");
    return true;
}

void steam_bridge_shutdown(void) {
    if (g_active && g_shutdown) g_shutdown();
    if (g_lib) STEAM_CLOSE(g_lib);
    g_lib = NULL;
    g_init = NULL;
    g_shutdown = NULL;
    g_callbacks = NULL;
    g_active = false;
}

void steam_bridge_tick(void) {
    if (g_active && g_callbacks) g_callbacks();
}

bool steam_bridge_active(void) { return g_active; }
const char* steam_bridge_status(void) { return g_status; }
