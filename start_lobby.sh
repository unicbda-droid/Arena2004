#!/usr/bin/env bash
# Start the Arena2004 lobby / NAT relay server.
# Usage: ./start_lobby.sh [udp-port]   (default 7777)
set -e
cd "$(dirname "$0")"
PORT="${1:-7777}"
if [ ! -x arena2004_lobby ]; then
    make arena2004_lobby
fi
exec ./arena2004_lobby "$PORT"
