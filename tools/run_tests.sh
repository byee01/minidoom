#!/bin/bash
# Behavior tests: every script runs on the native build and the shipped JS build; frames must match.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -s -C engine -f Makefile.lens native >/dev/null
clang -O2 -DLENS -w -Iengine/doomgeneric/doomgeneric engine/lens/test_fixed.c engine/doomgeneric/doomgeneric/m_fixed.c -o engine/build/test_fixed
engine/build/test_fixed
fail=0
for t in tests/*.txt; do
  n=$(engine/build/headless build/minidoom.wad "$t" 2>/dev/null | tail -1) || { echo "FAIL native $t: $n"; fail=1; continue; }
  j=$(node tools/jsrun.js engine/build/doom.lens.js build/minidoom.wad "$t" 2>&1 | tail -1) || { echo "FAIL js $t: $j"; fail=1; continue; }
  echo "ok $t | native: $n | js: $j"
done
for f in build/*.js.ppm; do
  [ -e "$f" ] || continue
  cmp -s "$f" "${f%.js.ppm}.ppm" || { echo "FAIL frame mismatch native vs js: $f"; fail=1; }
done
exit $fail
