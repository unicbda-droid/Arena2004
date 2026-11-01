/* Arena2004 - small cross-platform layer (Linux / macOS / Windows)
 * Keep OS specific code here so the game and the lobby server stay portable. */
#ifndef ARENA_PLATFORM_H
#define ARENA_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0601
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <direct.h>
  #include <process.h>
  #include <io.h>
  #include <windows.h>
  typedef SOCKET plat_socket_t;
  typedef int plat_socklen_t;
  typedef intptr_t plat_ssize_t;
  #define PLAT_INVALID_SOCKET INVALID_SOCKET
  #define PLAT_SOCK_ERR       SOCKET_ERROR
  #define plat_close_socket   closesocket
  static inline int plat_socket_startup(void) {
      WSADATA w;
      return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
  }
  static inline void plat_socket_cleanup(void) { WSACleanup(); }
  static inline int plat_set_nonblocking(plat_socket_t s) {
      u_long mode = 1;
      return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
  }
  static inline void plat_sleep_ms(int ms) { Sleep((DWORD)(ms < 0 ? 0 : ms)); }
  static inline int plat_mkdir(const char* path) { return _mkdir(path); }
  static inline int plat_getpid(void) { return _getpid(); }
  #ifndef strcasecmp
  #define strcasecmp  _stricmp
  #endif
  #ifndef strncasecmp
  #define strncasecmp _strnicmp
  #endif
  #define strtok_r strtok_s
  static inline struct tm* plat_localtime_r(const time_t* t, struct tm* out) {
      return localtime_s(out, t) == 0 ? out : NULL;
  }
  #define localtime_r plat_localtime_r
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <sys/stat.h>
  #include <strings.h>
  typedef int plat_socket_t;
  typedef socklen_t plat_socklen_t;
  typedef ssize_t plat_ssize_t;
  #define PLAT_INVALID_SOCKET (-1)
  #define PLAT_SOCK_ERR       (-1)
  #define plat_close_socket   close
  static inline int plat_socket_startup(void) { return 0; }
  static inline void plat_socket_cleanup(void) {}
  static inline int plat_set_nonblocking(plat_socket_t s) {
      int fl = fcntl(s, F_GETFL, 0);
      if (fl < 0) return -1;
      return fcntl(s, F_SETFL, fl | O_NONBLOCK);
  }
  static inline void plat_sleep_ms(int ms) {
      if (ms > 0) usleep((useconds_t)ms * 1000u);
  }
  static inline int plat_mkdir(const char* path) { return mkdir(path, 0755); }
  static inline int plat_getpid(void) { return (int)getpid(); }
#endif

/* In-place "create directory if it does not exist". Existing dirs are fine. */
static inline int plat_ensure_dir(const char* path) {
    int r = plat_mkdir(path);
    if (r == 0) return 0;
    /* EEXIST on POSIX, ERROR_ALREADY_EXISTS on Windows */
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return 0; }
    return r;
}

/* strdup is not available everywhere under strict C11. */
static inline char* plat_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

#endif /* ARENA_PLATFORM_H */
