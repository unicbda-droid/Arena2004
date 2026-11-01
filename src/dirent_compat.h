/* Arena2004 - portable directory iteration.
 * POSIX/macOS/MinGW have <dirent.h>. MSVC does not, so provide the small
 * subset used by the map browser on top of the Win32 API. */
#ifndef ARENA_DIRENT_COMPAT_H
#define ARENA_DIRENT_COMPAT_H

#if defined(_WIN32) && !defined(__MINGW32__)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <stdbool.h>

  typedef struct {
      HANDLE handle;
      WIN32_FIND_DATAA data;
      bool first;
      bool valid;
      struct dirent { char d_name[260]; } ent;
  } DIR;

  static inline DIR* opendir(const char* path) {
      char pattern[512];
      snprintf(pattern, sizeof(pattern), "%s\\*", path);
      DIR* d = (DIR*)calloc(1, sizeof(DIR));
      if (!d) return NULL;
      d->handle = FindFirstFileA(pattern, &d->data);
      if (d->handle == INVALID_HANDLE_VALUE) { free(d); return NULL; }
      d->first = true;
      d->valid = true;
      return d;
  }

  static inline struct dirent* readdir(DIR* d) {
      if (!d || !d->valid) return NULL;
      if (d->first) d->first = false;
      else if (!FindNextFileA(d->handle, &d->data)) { d->valid = false; return NULL; }
      snprintf(d->ent.d_name, sizeof(d->ent.d_name), "%s", d->data.cFileName);
      return &d->ent;
  }

  static inline int closedir(DIR* d) {
      if (!d) return -1;
      if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
      free(d);
      return 0;
  }
#else
  #include <dirent.h>
#endif

#endif /* ARENA_DIRENT_COMPAT_H */
