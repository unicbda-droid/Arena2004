#!/usr/bin/env bash
# Large listen-server test: host with 20 bots (>14, so snapshots must be
# split into multiple UDP chunks), then join and verify count + mode.
set -u
cd "$(dirname "$0")/.."

make -s arena2004 arena2004_lobby || exit 1

LOBBY_PORT=7793
HOST_PORT=7806
LOBBY_LOG=/tmp/a2k4_listen_lobby.log
HOST_LOG=/tmp/a2k4_listen_host.log
CLIENT_LOG=/tmp/a2k4_listen_client.log
rm -f "$LOBBY_LOG" "$HOST_LOG" "$CLIENT_LOG"

./arena2004_lobby "$LOBBY_PORT" >"$LOBBY_LOG" 2>&1 &
LOBBY_PID=$!
HOST_PID=""
cleanup() {
    [ -n "$HOST_PID" ] && kill "$HOST_PID" 2>/dev/null
    kill "$LOBBY_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

sleep 0.7
./arena2004 --hidden --host --port "$HOST_PORT" \
    --lobby "127.0.0.1:$LOBBY_PORT" --room BigListen --bots 20 \
    --mode br --seconds 30 --netlog >"$HOST_LOG" 2>&1 &
HOST_PID=$!

CODE=""
for _ in $(seq 1 30); do
    CODE=$(grep -oE 'Raum [A-Z0-9]{4}' "$HOST_LOG" | head -1 | awk '{print $2}')
    [ -n "$CODE" ] && break
    sleep 0.3
done
if [ -z "$CODE" ]; then
    echo "FAIL: host did not receive a room code"
    cat "$HOST_LOG"
    exit 1
fi
echo "room code: $CODE"

sleep 1
./arena2004 --hidden --join 127.0.0.1 --port "$HOST_PORT" \
    --lobby "127.0.0.1:$LOBBY_PORT" --code "$CODE" \
    --seconds 7 --stats >"$CLIENT_LOG" 2>&1

FAIL=0
grep -q "players=21 " "$CLIENT_LOG" || { echo "FAIL: client did not receive all 21 players (chunked snapshots)"; FAIL=1; }
grep -q "mode=BR" "$CLIENT_LOG" || { echo "FAIL: BR mode did not reach the client"; FAIL=1; }
grep -q "Peers 1" "$HOST_LOG" || { echo "FAIL: host did not see peer"; FAIL=1; }

echo "--- client ---"
tail -4 "$CLIENT_LOG"
echo "--- host (tail) ---"
tail -4 "$HOST_LOG"

if [ "$FAIL" -ne 0 ]; then
    echo "LISTEN TEST: FAIL"
    exit 1
fi
echo "LISTEN TEST: PASS"
