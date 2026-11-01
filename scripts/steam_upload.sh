#!/usr/bin/env bash
# Upload dist/ to Steam via steamcmd. Requires a Steamworks app + depots and
# edited steam/*.vdf files (see steam/README.md).
set -euo pipefail
cd "$(dirname "$0")/.."

STEAMCMD="${STEAMCMD:-steamcmd}"
STEAM_USER="${STEAM_USER:-}"
APP_BUILD="${APP_BUILD:-steam/app_build.vdf}"

if [ -z "$STEAM_USER" ]; then
    echo "Set STEAM_USER to your Steamworks build account." >&2
    exit 1
fi
if [ ! -f "$APP_BUILD" ]; then
    echo "Missing $APP_BUILD. Copy steam/app_build.vdf.example and set AppID/depot IDs." >&2
    exit 1
fi
for f in windows linux macos; do
    if [ ! -f "steam/depot_build_$f.vdf" ]; then
        echo "Missing steam/depot_build_$f.vdf (copy from .example and set DepotID)." >&2
        exit 1
    fi
done
if [ ! -d dist/windows ] && [ ! -d dist/linux ] && [ ! -d dist/macos ]; then
    echo "Run scripts/package.sh on the target OS first (or copy builds into dist/)." >&2
    exit 1
fi

echo "Uploading build for $STEAM_USER ..."
"$STEAMCMD" +login "$STEAM_USER" +run_app_build "$(pwd)/$APP_BUILD" +quit
echo "Upload finished. Set the build live in Steamworks > Builds."
