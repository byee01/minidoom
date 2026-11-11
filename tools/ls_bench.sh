#!/bin/bash
# Benchmark the shipped engine JS inside Lens Studio's script runtime (run-script, QuickJS family).
# usage: tools/ls_bench.sh [ticks]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N=${1:-105}
OUT="$ROOT/build/ls_bench"
mkdir -p "$OUT"
python3 - "$ROOT" "$N" "$OUT/bench.js" <<'EOF'
import base64, sys
root, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
js = open(f"{root}/engine/build/doom.lens.js").read()
wad = base64.b64encode(open(f"{root}/build/minidoom.wad", "rb").read()).decode()
open(out, "w").write(f"""const WAD='{wad}';
globalThis.print = function(){{}};
{js}
(async () => {{
  const t0 = Date.now();
  const doom = await createDoom({{print: function(){{}}, printErr: function(){{}}}});
  const wad = Base64.decode(WAD);
  const p = doom._lens_wad_alloc(wad.length); doom.HEAPU8.set(wad, p);
  doom._lens_create();
  console.log("BENCH boot_ms " + (Date.now() - t0));
  doom._lens_post_key(1, 0xad);
  let t;
  for (const [b, d] of [[11, 0], [11, 1]]) {{
    doom._lens_set_view(b, d);
    doom._lens_tick(1);
    t = Date.now();
    for (let i = 0; i < {n}; i++) doom._lens_tick(1);
    console.log("BENCH render_ms_per_tic blocks=" + b + " detail=" + d + " " + ((Date.now() - t) / {n}).toFixed(2));
  }}
  doom._lens_set_view(11, 0);
  t = Date.now();
  for (let i = 0; i < {n}; i++) doom._lens_present();
  console.log("BENCH present_ms " + ((Date.now() - t) / {n}).toFixed(2));
  doom._lens_test_fake_face(); doom._lens_face_set_live(1);
  t = Date.now();
  for (let i = 0; i < {n}; i++) doom._lens_present();
  console.log("BENCH present_face_ms " + ((Date.now() - t) / {n}).toFixed(2));
  const t0b = Date.now();
  const big = WAD; const dec = Base64.decode(big);
  console.log("BENCH base64_decode_ms " + (Date.now() - t0b));
  t = Date.now();
  for (let i = 0; i < {n}; i++) doom._lens_tick(0);
  console.log("BENCH logic_ms_per_tic " + ((Date.now() - t) / {n}).toFixed(2));
  t = Date.now();
  for (let i = 0; i < {n}; i++) doom._lens_audio_mix(1260, 1);
  console.log("BENCH mix_ms_per_tic " + ((Date.now() - t) / {n}).toFixed(2));
}})().catch(e => console.log("BENCH ERR " + e + " " + e.stack));
""")
EOF
LS="/Applications/Lens Studio.app/Contents/MacOS/Lens Studio"
"$ROOT/tools/ls_cli.sh" 280 "$OUT/bench.log" --exec run-script -f "$OUT/bench.js" &
PID=$!
for i in $(seq 1 150); do sleep 2; kill -0 $PID 2>/dev/null || break; done
kill $PID 2>/dev/null || true
grep -o "BENCH.*\|Error message.*" "$OUT/bench.log" || tail -5 "$OUT/bench.log"
