#!/bin/bash
# Fetch third-party inputs that are not stored in this repo, pinned and checksummed:
#   - doomgeneric (GPLv2) at a fixed upstream commit, with engine/patches/minidoom-lens.patch applied.
#     Mirrored unmodified (same commit hash) on this repo's doomgeneric-upstream branch, so the
#     full corresponding source stays available here; upstream is github.com/ozkl/doomgeneric.
#   - Freedoom 0.13.0 (BSD-3): freedoom1.wad + its license/credits
#   - FluidR3Mono soundfont (MIT): only needed to re-render data/music/music_e1m1.mp3
# Idempotent: anything already present is left alone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DG_URL=https://github.com/byee01/minidoom.git
DG_BRANCH=doomgeneric-upstream
DG_COMMIT=63e2d26a8c6b67fa922aff7782e315e548bf28e3
FD_URL=https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip
FD_SHA=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59
SF_URL=https://github.com/musescore/MuseScore/raw/v4.4.4/share/sound/FluidR3Mono_GM.sf3
SF_SHA=2aacd036d7058d40a371846ef2f5dc5f130d648ab3837fe2626591ba49a71254

sha_ok() { [ "$(shasum -a 256 "$1" | cut -d' ' -f1)" = "$2" ]; }
fetch() {   # url sha dest
  mkdir -p "$(dirname "$3")"
  curl -fsSL -o "$3.part" "$1"
  sha_ok "$3.part" "$2" || { echo "fetch_deps: checksum mismatch for $1" >&2; rm -f "$3.part"; exit 1; }
  mv "$3.part" "$3"
}

if [ ! -d engine/doomgeneric/doomgeneric ]; then
  echo "fetch_deps: doomgeneric @ ${DG_COMMIT:0:7} + minidoom patch"
  git clone -q --single-branch --branch "$DG_BRANCH" "$DG_URL" engine/doomgeneric
  git -C engine/doomgeneric checkout -q "$DG_COMMIT"
  git -C engine/doomgeneric apply --whitespace=nowarn "$ROOT/engine/patches/minidoom-lens.patch"
fi

if [ ! -f data/freedoom-0.13.0/freedoom1.wad ]; then
  echo "fetch_deps: Freedoom 0.13.0"
  fetch "$FD_URL" "$FD_SHA" data/freedoom.zip
  unzip -q -o data/freedoom.zip "freedoom-0.13.0/freedoom1.wad" "freedoom-0.13.0/COPYING.txt" "freedoom-0.13.0/CREDITS.txt" -d data
fi

if [ "${1:-}" = "--soundfont" ] && [ ! -f data/soundfont/FluidR3Mono_GM.sf3 ]; then
  echo "fetch_deps: FluidR3Mono soundfont"
  fetch "$SF_URL" "$SF_SHA" data/soundfont/FluidR3Mono_GM.sf3
fi
