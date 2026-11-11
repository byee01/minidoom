// Run the JS engine build under node with the same script format as engine/lens/headless.c.
// Proves the shipped JS artifact behaves identically to the native build.
// usage: node tools/jsrun.js <doom.js> <wad> <script>
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const [, , jsPath, wadPath, scriptPath] = process.argv;
const LS = (() => {
  const src = fs.readFileSync(path.join(__dirname, "../engine/lens/lens_state.h"), "utf8");
  const o = {};
  for (const m of src.matchAll(/#define LS_(\w+) (\d+)/g)) o[m[1].toLowerCase()] = +m[2];
  return o;
})();
const KEYS = { UP: 0xad, DOWN: 0xaf, LEFT: 0xac, RIGHT: 0xae, STRAFE_L: 0xa0, STRAFE_R: 0xa1,
  USE: 0xa2, FIRE: 0xa3, RUN: 0x80 + 0x36, ESC: 27 };
const key = (k) => KEYS[k] ?? k.charCodeAt(0);

// Lens-like sandbox: no console/process/require, only print. Matches what the wrapper must shim.
const sandbox = { print: () => {}, printErr: (s) => process.stderr.write(s + "\n") };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
let code = fs.readFileSync(jsPath, "utf8");
vm.runInContext(code + "\n;globalThis.__factory = (typeof createDoom!=='undefined'?createDoom:module.exports);", sandbox,
  { filename: path.basename(jsPath) });

(async () => {
  const doom = await sandbox.__factory({});
  const wad = fs.readFileSync(wadPath);
  const ptr = doom._lens_wad_alloc(wad.length);
  doom.HEAPU8.set(wad, ptr);
  doom._lens_create();
  const st = () => new Int32Array(doom.HEAP32.buffer, doom._lens_state_ptr(), LS.count);
  const fieldOff = (spec) => {
    const m = /^(\w+)(?:\[(\d+)\])?$/.exec(spec);
    const base = LS[m[1]];
    if (base === undefined) throw new Error("unknown field " + spec);
    return base + (m[2] ? +m[2] : 0);
  };
  let tic = 0, checks = 0, failures = 0;
  const t0 = process.hrtime.bigint();
  const ms = () => Number(process.hrtime.bigint() - t0) / 1e6;
  for (let line of fs.readFileSync(scriptPath, "utf8").split("\n")) {
    line = line.split("#")[0].trim();
    if (!line) continue;
    const [atS, cmd, a1, a2, a3] = line.split(/\s+/);
    const at = +atS;
    while (tic < at) { doom._lens_tick(1); doom._lens_audio_mix(1260, 2); tic++; }
    if (cmd === "down") doom._lens_post_key(1, key(a1));
    else if (cmd === "up") doom._lens_post_key(0, key(a1));
    else if (cmd === "tap") doom._lens_tap_key(key(a1));
    else if (cmd === "mouse") doom._lens_post_mouse(+a1);
    else if (cmd === "type") for (const c of a1) { doom._lens_post_key(1, c.charCodeAt(0)); doom._lens_post_key(0, c.charCodeAt(0)); }
    else if (cmd === "restart") doom._lens_restart();
    else if (cmd === "damage") doom._lens_test_damage(+a1);
    else if (cmd === "exit") doom._lens_test_exit();
    else if (cmd === "analog") doom._lens_set_analog(+a1, +a2, +a3);
    else if (cmd === "spawn") doom._lens_test_spawn_enemy(+a1);
    else if (cmd === "place") doom._lens_test_place(+a1, +a2, +a3);
    else if (cmd === "ceiling") console.log(`tic ${tic}: ceiling(${a1},${a2}) = ${doom._lens_test_ceiling(+a1, +a2)}`);
    else if (cmd === "expect_ceiling") { const c = doom._lens_test_ceiling(+a1, +a2); checks++; if (!(c > +a3)) { failures++; console.log(`FAIL tic ${tic}: ceiling(${a1},${a2})=${c} not > ${a3}`); } }
    else if (cmd === "face") { if (+a1) doom._lens_test_fake_face(); doom._lens_face_set_live(+a1); }
    else if (cmd === "hit") console.log(`tic ${tic}: hit(${a1},${a2}) = ${doom._lens_hud_hit(+a1, +a2)}`);
    else if (cmd === "dump") {
      // Same layout as headless.c: composite, then the face canvas at 3x on the right.
      const OW = 320, OH = 240, FW = 56, FH = 62, p = doom._lens_present(), fp = doom._lens_face_ptr();
      const px = doom.HEAPU8.subarray(p, p + OW * OH * 4), fc = doom.HEAPU8.subarray(fp, fp + FW * FH * 4);
      const W = OW + FW * 3, H = Math.max(OH, FH * 3), out = Buffer.alloc(W * H * 3);
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        let s = -1, src = px;
        if (x < OW) { if (y < OH) s = (y * OW + x) * 4; }
        else if (y < FH * 3) { s = (Math.floor(y / 3) * FW + Math.floor((x - OW) / 3)) * 4; src = fc; }
        const d = (y * W + x) * 3;
        if (s >= 0) { out[d] = src[s]; out[d + 1] = src[s + 1]; out[d + 2] = src[s + 2]; }
      }
      fs.writeFileSync(a1.replace(/\.ppm$/, ".js.ppm"), Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), out]));
    } else if (cmd === "time") console.log(`tic ${tic}: ${(ms() / (tic || 1)).toFixed(3)} ms/tic`);
    else if (cmd === "print") console.log(`tic ${tic}: ${a1} = ${st()[fieldOff(a1)]}`);
    else if (cmd === "expect") {
      const v = st()[fieldOff(a1)], w = +a3;
      const ok = { "==": v === w, "!=": v !== w, "<": v < w, "<=": v <= w, ">": v > w, ">=": v >= w }[a2];
      checks++;
      if (!ok) { failures++; console.log(`FAIL tic ${tic}: ${a1} (=${v}) ${a2} ${w}`); }
    } else if (cmd === "end") break;
    else throw new Error("bad cmd " + cmd);
  }
  console.log(`${tic} tics, ${checks - failures}/${checks} checks passed, ${(ms() / (tic || 1)).toFixed(3)} ms/tic`);
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
