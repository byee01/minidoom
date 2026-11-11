#!/bin/bash
# Full minidoom pipeline: engine -> tests -> stage -> Lens Studio project -> exported lens.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LS="/Applications/Lens Studio.app/Contents/MacOS/Lens Studio"
STAGE="$ROOT/build/stage"
# Debug builds (autotest, captures) go to their own project so lens/ is always the release.
if [ "${MINIDOOM_DEBUG:-0}" = "1" ]; then PROJ="$ROOT/lens_debug"; EXPORT="$ROOT/build/export_debug"; else PROJ="$ROOT/lens"; EXPORT="$ROOT/build/export"; fi
mkdir -p "$STAGE" build

echo "== deps"
tools/fetch_deps.sh

echo "== engine"
python3 tools/gen_state.py
make -s -C engine -f Makefile.lens native js
node tools/wrap_for_lens.js engine/build/doom.raw.js engine/build/doom.lens.js

echo "== data"
python3 tools/wadstrip.py data/freedoom-0.13.0/freedoom1.wad build/minidoom.wad

echo "== tests"
tools/run_tests.sh

echo "== stage"
cp engine/build/doom.lens.js "$STAGE/doomEngine.js"
cp lens_src/Main.js lens_src/Ui.js lens_src/DebugCapture.js "$STAGE/"
cp "/Applications/Lens Studio.app/Contents/JsPlugins/PresetsRegistry/Assets/Resources/flat.graphShader" "$STAGE/"
printf "exports.DEBUG = %s;\nexports.TRY_FORCE_RECORD = %s;\n" "${MINIDOOM_DEBUG:-0}" "${MINIDOOM_TRY_RECORD:-0}" > "$STAGE/BuildConfig.js"
python3 - "$ROOT/build/minidoom.wad" "$STAGE/wadData.js" <<'PY'
import base64, sys
b = base64.b64encode(open(sys.argv[1], "rb").read()).decode()
chunks = [b[i:i + 65536] for i in range(0, len(b), 65536)]
open(sys.argv[2], "w").write("// GENERATED: base64 of build/minidoom.wad (Freedoom, BSD-3). Do not edit.\nmodule.exports = [\n" + ",\n".join('"%s"' % c for c in chunks) + "\n];\n")
PY
[ -f data/music/music_e1m1.mp3 ] || { tools/fetch_deps.sh --soundfont; tools/render_music.sh; }
cp data/music/music_e1m1.mp3 data/icon/icon.png "$STAGE/"

echo "== project"
rm -rf "$PROJ"; mkdir -p "$PROJ"
tools/ls_cli.sh 300 build/ls_build.log --exec run-script -f "$ROOT/tools/ls_build_project.js" -a "$STAGE $PROJ" || true
grep -a -o "BUILD.*" build/ls_build.log || { tail -20 build/ls_build.log; exit 1; }
grep -a -q "BUILD saved OK" build/ls_build.log

echo "== export"
rm -rf "$EXPORT"; mkdir -p "$EXPORT"
tools/ls_cli.sh 600 build/ls_export.log --exec export-lens -f "$PROJ/minidoom.esproj" -o "$EXPORT" -p Zip || { grep -E "^E |error" build/ls_export.log | head -20; exit 1; }
ls -la "$EXPORT"
