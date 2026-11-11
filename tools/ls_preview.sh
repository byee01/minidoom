#!/bin/bash
# Open the project in Lens Studio (GUI), let Preview run the lens, collect the lens's own
# [minidoom] log lines and a screenshot. usage: tools/ls_preview.sh [seconds]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECS=${1:-40}
PROJ="$(cd "${2:-$ROOT/lens}" && pwd)"
LS="/Applications/Lens Studio.app/Contents/MacOS/Lens Studio"
OUT="$ROOT/build/preview"
mkdir -p "$OUT"
rm -f "$OUT/ls.log" "$OUT/screen.png"
if pgrep -f "Lens Studio.app/Contents/MacOS/Lens Studio" >/dev/null; then
  echo "Lens Studio is already running; close it first (this script won't kill your session)."; exit 2
fi
(yes N || true) | "$LS" "$PROJ/minidoom.esproj" --force-project-update --log-file "$OUT/ls.log" > "$OUT/stdout.log" 2>&1 &
PID=$!
sleep "$SECS"
screencapture -x "$OUT/screen.png" 2>/dev/null || echo "screencapture unavailable"
LSPID=$(pgrep -f "Lens Studio.app/Contents/MacOS/Lens Studio $PROJ/minidoom.esproj" | head -1)
[ -n "$LSPID" ] && kill "$LSPID" 2>/dev/null; sleep 3
[ -n "$LSPID" ] && kill -9 "$LSPID" 2>/dev/null
echo "--- lens log"
cat "$OUT/ls.log" "$OUT/stdout.log" 2>/dev/null | grep -a -o "\[minidoom\].*\|Script.*[Ee]rror.*\|TypeError.*\|ReferenceError.*\|InternalError.*" | sort | uniq -c | sort -rn | head -40
