#!/usr/bin/env bash
# Bot/simulation matrix: every mode, large listen-server scale, and 1k BR.
# Fails if bots do not move, paths break, or BR loses players.
set -u
cd "$(dirname "$0")/.."
FAIL=0

check() {
    local label="$1" seconds="$2"; shift 2
    local out
    out=$(timeout 240 ./arena2004 --hidden --bottest "$seconds" "$@" 2>&1)
    if printf '%s\n' "$out" | grep -q '^\[bottest\] PASS'; then
        printf 'PASS  %-18s ' "$label"
        printf '%s\n' "$out" | grep -E '^\[bottest\] (sim|movement|nav|combat):' | tr '\n' ' '
        printf '\n'
    else
        printf 'FAIL  %-18s\n' "$label"
        printf '%s\n' "$out" | grep -E '^\[bottest\]' || true
        FAIL=1
    fi
}

echo "== modes =="
check "DM"        10 --mode dm   --bots 9
check "TDM"       10 --mode tdm  --bots 10
check "CTF"       12 --mode ctf  --bots 10
check "VCTF"      12 --mode vctf --bots 8
check "Onslaught" 12 --mode onslaught --bots 10
check "Conquest"  12 --mode conquest  --bots 10
check "RPG"       12 --mode rpg  --bots 8
check "BR"        12 --mode br   --bots 10

echo "== scales =="
check "32v32"     8 --mode tdm --scale 32v32
check "128v128"   6 --mode tdm --scale 128v128
check "1k BR"     6 --mode br  --scale 1000
check "30k BR zone" 5 --mode br --scale 30000

echo
if [ "$FAIL" -eq 0 ]; then echo "BOT TEST: PASS"; else echo "BOT TEST: FAIL"; fi
exit "$FAIL"
