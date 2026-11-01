#!/usr/bin/env bash
# End-to-end network smoke test:
#   lobby server -> host registers room -> wrong password is denied ->
#   correct password connects -> host mode (CTF) reaches the client.
set -u
cd "$(dirname "$0")/.."

make -s arena2004 arena2004_lobby || exit 1

LOBBY_PORT=7791
HOST_PORT=7802
LOBBY_LOG=/tmp/a2k4_net_lobby.log
HOST_LOG=/tmp/a2k4_net_host.log
BAD_LOG=/tmp/a2k4_net_badpw.log
CLIENT_LOG=/tmp/a2k4_net_client.log
rm -f "$LOBBY_LOG" "$HOST_LOG" "$BAD_LOG" "$CLIENT_LOG"

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
    --lobby "127.0.0.1:$LOBBY_PORT" --room NetTest --bots 2 \
    --mode ctf --password geheim --seconds 30 --netlog >"$HOST_LOG" 2>&1 &
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
# 1) wrong password must be rejected
./arena2004 --hidden --join 127.0.0.1 --port "$HOST_PORT" \
    --lobby "127.0.0.1:$LOBBY_PORT" --code "$CODE" \
    --seconds 3 --netlog --joinpassword falsch >"$BAD_LOG" 2>&1

# 2) correct password connects and receives the host mode (CTF)
./arena2004 --hidden --join 127.0.0.1 --port "$HOST_PORT" \
    --lobby "127.0.0.1:$LOBBY_PORT" --code "$CODE" \
    --seconds 7 --netlog --stats --joinpassword geheim >"$CLIENT_LOG" 2>&1

FAIL=0
grep -q "Zugriff verweigert" "$BAD_LOG" || { echo "FAIL: wrong password was not rejected"; FAIL=1; }
grep -q "Beitritt abgelehnt" "$HOST_LOG" || { echo "FAIL: host did not log the rejected password"; FAIL=1; }
grep -q "Verbunden als Slot" "$CLIENT_LOG" || { echo "FAIL: client did not connect"; FAIL=1; }
grep -q "Peers 1" "$HOST_LOG" || { echo "FAIL: host did not see peer"; FAIL=1; }
grep -q "Pakete" "$CLIENT_LOG" || { echo "FAIL: no packet counters"; FAIL=1; }
grep -q "mode=CTF" "$CLIENT_LOG" || { echo "FAIL: host mode CTF did not reach the client"; FAIL=1; }

echo "--- wrong password ---"
cat "$BAD_LOG"
echo "--- client ---"
cat "$CLIENT_LOG"
echo "--- host (tail) ---"
tail -8 "$HOST_LOG"
echo "--- lobby (tail) ---"
tail -4 "$LOBBY_LOG"

if [ "$FAIL" -ne 0 ]; then
    echo "NET TEST: FAIL"
    exit 1
fi
echo "NET TEST: PASS"
