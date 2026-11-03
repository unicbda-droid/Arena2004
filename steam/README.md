# Arena2004 auf Steam veröffentlichen

Der Quellcode und alle GitHub-Builds laufen **ohne Steamworks-SDK**. Steam ist
optional: `src/steam_bridge.c` lädt `steam_api` zur Laufzeit, sobald die
Bibliothek neben der EXE bzw. im Spielordner liegt. Dadurch bleiben DRM-freie
Builds und Steam-Builds identisch und du kannst denselben Code auf GitHub und
Steam veröffentlichen.

## 1. Steamworks-App anlegen

1. Steamworks-Partner-Account anlegen: <https://partner.steamgames.com/>
2. Neue App + Depots für Windows, Linux und macOS erstellen.
3. AppID und DepotIDs in die Vorlagen eintragen:
   - `steam/app_build.vdf.example` -> `steam/app_build.vdf`
   - `steam/depot_build_windows.vdf.example` -> `steam/depot_build_windows.vdf`
   - `steam/depot_build_linux.vdf.example` -> `steam/depot_build_linux.vdf`
   - `steam/depot_build_macos.vdf.example` -> `steam/depot_build_macos.vdf`
4. `steam_appid.txt` mit deiner AppID anlegen (nur für lokale Tests; nicht in
   den Release-Build legen). `steam_appid.txt.example` nutzt 480 (Spacewar).
5. In Steamworks unter *Installation > General* die Startoption
   `arena2004` als ausführbare Datei eintragen.

## 2. Builds erzeugen

```bash
# Linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MSYS2 MinGW64)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# macOS (Homebrew)
brew install sdl2 glew libpng cmake pkg-config
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

Danach mit `scripts/package.sh` die Ordner `dist/windows`, `dist/linux`,
`dist/macos` erzeugen. Die Steam-Depot-Vorlagen mappen genau diese Ordner.

## 3. Hochladen

```bash
export STEAMCMD="$HOME/steamcmd/steamcmd.sh"
export STEAM_USER="dein_account"
scripts/steam_upload.sh
```

`steamcmd` fragt beim ersten Mal nach Passwort und Steam-Guard-Code. Für CI
kannst du einen Build-Account mit `+login $STEAM_USER $STEAM_PASSWORD` nutzen
(Passwort niemals committen).

## 4. Optional: Achievements / Cloud / Lobby

Die Brücke initialisiert bereits `SteamAPI_Init`, `SteamAPI_RunCallbacks` und
`SteamAPI_Shutdown`. Für Achievements, Cloud-Saves oder Steam-Lobbies müssen
nur die entsprechenden Steamworks-Header eingebunden und Aufrufe ergänzt
werden; die AppID-/Depot-Konfiguration bleibt unverändert. Eine Vorschlagsliste
für Achievements liegt in `steam/achievements.json`.

## 5. GitHub Release

Der Workflow `.github/workflows/build.yml` baut Linux, macOS und Windows bei
jedem Push und hängt ZIP-Artefakte an den Run. Für ein Release:

```bash
git tag v0.9.2
git push origin v0.9.2
```

Danach die drei Artefakte unter *Releases* veröffentlichen. Dieselben ZIPs
können als Steam-Depot-Inhalt dienen.
