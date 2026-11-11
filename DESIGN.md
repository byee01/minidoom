# minidoom: Doom E1M1 as a Snapchat Lens — Design Doc

Status: draft v2 (reviewed) · Date: 2026-11-10 · Target: Lens Studio 5.24.x, Snapchat mobile (portrait)

### Changes in v2 (review)

- **Confirmed by owner:** face = user's live face in the HUD slot (§2); game data = Freedoom (§2). See §15.
- **Fixed engine hang:** Lens `getTime()` likely holds one value per frame, and `DG_SleepMs` is a no-op, so Doom's wait loops (`TryRunTics`, screen-wipe) would spin forever. The JS side is now the clock, each `doomgeneric_Tick` runs exactly one tic, and the wipe is off (§3.1, §3.2).
- **Removed wasted work:** the doomgeneric 320×200 → 640×400 copy in `I_FinishUpdate` is skipped; Emscripten FS replaced with a memory-backed WAD (§3.1, §3.3).
- **Controls:** double-tap is Snapchat's camera-flip gesture, so run is now tied to how far the stick is pushed (§4).
- **WAD stripping:** listed the lumps the engine fails without, and replaced the sprite-rotation lever, which would make the engine fail at startup (§8).
- **Perf levers:** `screenblocks 9–10` conflicted with the hidden status bar; clarified (§9).
- **New verification items V12–V15** (§12).

## As built (2026-11-10)

Built and verified on Lens Studio 5.24.1 (the session started on 5.18.1, and Lens Studio auto-updated partway through). Pipeline: `tools/build_all.sh` (add `MINIDOOM_DEBUG=1` for the in-Lens autotest build). It runs engine → tests → WAD strip → Lens Studio project (created from scratch by `tools/ls_build_project.js`) → `export-lens`.

Deviations from the plan, and why:

| Plan | Built | Why |
|---|---|---|
| Palette lookup in a Material Editor shader (§6) | **Truecolor drawers**: `r_draw.c` column/span drawers write 32-bit RGBA through `lens_colormaps32` (light tables × current palette) straight into the uploaded image. The HUD is 8-bit, expanded only when it changes | Material graphs are binary and can't be authored reliably from a script. The JS expansion pass cost 13 ms/frame in the Lens interpreter; the truecolor drawers add no per-pixel work |
| Separate HUD Images + sprite atlases (§4, §5) | HUD drawn in C from Freedoom's `ST*` lumps into the bottom 96 rows of the same image (320×296, shown at 1.2 pixel aspect) | One texture and one upload, and Doom's damage/pickup palette flashes tint the HUD as they did originally |
| Face: FaceCrop + Doomify shader, landmark-anchored overlays (§5.2) | FaceCrop → 24×29 render target (GPU downsample) → read back at 15 Hz → quantized to PLAYPAL in C (LUT precomputed into the WAD as `LENSLUT`) → state overlays at fixed crop positions | No custom shader needed. The face is then literally Doom-resolution, palette-exact and flash-tinted. The crop is face-centred, so fixed positions line up |
| `screenblocks 10` perf lever | Quality ladder: full detail → low detail → render every 2nd frame (tics stay at 35 Hz) | Keeps the full 320×200 view; adaptive at runtime |
| Run on stick ≥ 75% | as planned | |
| Ouch face on hits > 20 | Engine's own rule, kept vanilla (ouch is Doom's health-*rise* bug; sourceless damage shows the rampage face) | "Timing and priority are exactly the engine's" (§5.1) |
| Rows bottom-up for `setPixels` | Rows top-down; the Image sets `flipY` | Renderer loops stay vanilla |
| FIRE + USE buttons, stick bottom-left (§4) | **No buttons.** Auto-fire (`engine/lens/lens_assist.c`): fire is held only while Doom's own `P_AimLineAttack` hits a live enemy, never a barrel or empty space, with a ±4° aim assist checked every other tic. Auto-use: taps USE when facing a special line within use range; manual doors only when closed and idle (never shuts one on the player); switches have a 2 s per-line cooldown. Small stick centred in a band under the HUD; drag anywhere else turns | Owner request, 2026-11-10 |
| Single centred stick (earlier revision) | **Twin analog sticks**: left d-pad = move/strafe, right ◀▶ pill = turn rate, no text labels. Movement capped at ~75% of Doom walk speed and the 75%-deflection auto-run is gone (`lens_set_analog`, hook in `G_BuildTiccmd`). Free drags on the view or face still turn | On-device feedback: too fast, clearer controls |
| Black background | Freedoom STBAR stone texture (text-free patch from the ammo panel) on the HUD panels and tiled over the screen background | On-device feedback |
| Face in the HUD centre slot (§4, §5) | HUD is a 320×40 strip (ammo, weapons 2–7, health, armor, keys). The face is its own 56×62 canvas (camera render target at the same size, Doom palette, overlays scaled from the 24×29 grid), shown as large as the space from the stick band to the safe-region bottom allows | Owner request: face as large as possible |
| Lens starts recording the Snap when the player moves | **Not possible:** lenses can observe Snap recording (`SnapRecordStartEvent`) but can't start it. Built instead: (1) starting a Snap recording on the title card starts the run, so the Snap begins with the game; (2) `VideoRecorder` records the lens output from the player's first move (capped at 60 s) and plays it back on the end card for the user to Snap | Platform limit |

Additional engine patches (all `#ifdef LENS`): bit-exact 32-bit `FixedMul`/`FixedDiv` (the wasm2js i64 emulation was a top hotspot; checked against the originals over 40M cases); tic-spanning key taps (Doom polls `gamekeydown[]` per tic, so a press and release inside one tic was dropped).

Measured (Lens Studio 5.24.1 Preview, M1 Pro): 30 fps (the Preview cap), 35 tics/s, engine 6.7 ms/frame at full detail, present 0.5 ms, total script 8.5 ms/frame, boot 0.1 s. Package 3.5 MB zipped. Not yet measured on phones (see Status).

Verification: `tools/run_tests.sh` runs every `tests/*.txt` script on the native build and on the shipped JS build, and asserts their composite frames are byte-identical. The debug build runs an in-Lens autotest (`AUTOTEST PASS/FAIL` in the log) that drives the real touch handler and captures the real render target (`tools/decode_capture.py`).

## 0. Sources

| # | Source | Type | What it establishes |
|---|---|---|---|
| S1 | [Lens Studio download page](https://ar.snap.com/download) / [v5.24.1 notes](https://ar.snap.com/download) | Primary | Latest release 5.24.1, Oct 7 2026 |
| S2 | [Lens Studio 5.14.0 release notes](https://ar.snap.com/download/v5-14-0) | Primary | "JavaScript engine now supports up to ES2021" |
| S3 | [Scripting Introduction](https://developers.snap.com/lens-studio/features/scripting/scripting-introduction) | Primary | Lens scripts are JS/TS; TS components extend `BaseScriptComponent` with `@component`. No WebAssembly documented |
| S4 | [ProceduralTextureProvider](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.ProceduralTextureProvider.html) | Primary | `setPixels(x, y, w, h, Uint8Array)`; "use TextureFormat.RGBA8Unorm"; region "extends rightward by width and upward by height" |
| S5 | [Head](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.Head.html), [Face Landmark guide](https://developers.snap.com/lens-studio/features/ar-tracking/face/face-landmark) | Primary | `getLandmarks()`, `onLandmarksUpdate`, `getFacesCount()`; "positions are in screen space" |
| S6 | [FaceCropTextureProvider](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.FaceCropTextureProvider), [FaceInsetVisual](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.FaceInsetVisual.html), [Face Inset guide](https://developers.snap.com/lens-studio/features/ar-tracking/face/face-inset) | Primary | Cropped face texture "calculated based on face position"; FaceInsetVisual draws a face region |
| S7 | [AudioOutputProvider](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.AudioOutputProvider.html), [Audio Track Assets](https://developers.snap.com/lens-studio/features/audio/audio-track-assets) | Primary | `enqueueAudioFrame(Float32Array, vec3)`; "get the amount of samples required for the current frame" |
| S8 | [Touch input](https://developers.snap.com/lens-studio/features/scripting/touch-input), [TouchStartEvent](https://developers.snap.com/lens-studio/api/lens-scripting/classes/Built-In.TouchStartEvent.html), [touchSystem](https://developers.snap.com/lens-studio/api/lens-scripting/variables/Built_In.global.touchSystem.html) | Primary | `getTouchId()` "distinguishing between touches when multiple are occurring simultaneously" |
| S9 | [Screen Transform overview](https://developers.snap.com/lens-studio/lens-studio-workflow/scene-set-up/2d/screen-transform-overview), [Screen Region](https://developers.snap.com/lens-studio/lens-studio-workflow/scene-set-up/2d/screen-region-device-simulation) | Primary | Safe Render region: "will always be visible and not overlapped by Snapchat UI" |
| S10 | [Performance optimization guide](https://developers.snap.com/lens-studio/publishing/optimization/performance-optimization-guide), [Remote Assets](https://developers.snap.com/lens-studio/platform-solutions/lens-cloud/remote_assets/remote-assets-overview) | Primary | "Lens size should be less than 8 MB"; remote assets 25 MB/lens, 10 MB/asset, outside the cap |
| S11 | [Submission guidelines](https://developers.snap.com/lens-studio/publishing/submitting/submission-guidelines) | Primary | Fictional/cartoonish weapons allowed; "Realistic, disturbing harm" prohibited |
| S12 | [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) (`doomgeneric.h`, `st_stuff.c`, `LICENSE`) | Primary | 5-function port surface; `DOOMGENERIC_RESX 640`/`RESY 400` defaults; `CMAP256`; GPLv2 |
| S13 | [Emscripten `settings.js`](https://github.com/emscripten-core/emscripten/blob/main/src/settings.js) | Primary | `-sWASM=0`: "compile to JS instead of wasm (deprecated)" |
| S14 | [Freedoom `COPYING.adoc`](https://github.com/freedoom/freedoom/blob/master/COPYING.adoc) | Primary | 3-clause BSD |
| S15 | [Debian doom-wad-shareware copyright](https://sources.debian.org/src/doom-wad-shareware/1.9.fixed-2/debian/copyright) | Secondary (quotes primary) | Carmack 1999: shareware wad "freely distributable"; license text "does not appear to grant distribution permission" |
| S16 | [Chocolate Doom wiki](https://www.chocolate-doom.org/wiki/index.php/Chocolate_Doom) | Primary | Upstream of doomgeneric's engine code |

Note: developers.snap.com was not directly fetchable from the authoring environment; Snap quotes above come from search-indexed copies of those official pages. Section 12 lists every API behavior that must be confirmed in Lens Studio before build.

---

## 1. Goal

Ship one Snapchat Lens that plays the first level (E1M1) of Doom-engine content, end to end, with:

- All code and game data inside the lens package (no network at runtime).
- Portrait-first UI with touch controls.
- The status-bar "Doomguy face" health indicator, centered in the HUD, replaced by the player's live face from the front camera, damaged and animated by in-game health state.

### Non-goals (v1)

- Episodes/levels beyond E1M1. Level exit ends the run.
- Save/load, multiplayer, demos, mods, automap polish.
- OPL/MIDI music synthesis at runtime (music is pre-rendered).
- Spectacles or Camera Kit targets.

---

## 2. Key decisions

| Decision | Choice | Reason |
|---|---|---|
| Engine | doomgeneric (Chocolate Doom derived), C | 5-function platform surface (S12); vanilla-accurate |
| Runtime target | C → Emscripten → **pure JS** (wasm2js / `-sWASM=0`) | Lens scripting is JS/TS only (S3); no documented WebAssembly |
| Game data | **Freedoom Phase 1** (`freedoom1.wad`), E1M1 only, stripped | BSD-3 (S14) allows modification + redistribution. Shareware `doom1.wad` license text does not grant distribution and asks that levels not be modified (S15) |
| Branding | Lens title **"minidoom"** (owner decision). No id logo or logo-style font in the icon | Owner accepted the trademark risk (§14) |
| Framebuffer | 320×200 8-bit indexed (`-DCMAP256`), packed 4 px/texel, palette lookup in shader | 16 KB/frame upload vs 1 MB RGBA at 640×400 |
| Status bar | Doom's own status bar disabled; native Lens HUD built from engine state | Portrait layout needs a different arrangement than the 320×32 bar |
| Face indicator | `FaceCropTexture` from front camera, Doom-palette quantize + pixelate shader, damage overlays keyed to `st_faceindex` | Matches Doom's 5 pain tiers × 3 look directions + ouch/grin/dead/god states |
| Audio | SFX mixed in C, pushed via `AudioOutputProvider.enqueueAudioFrame`; music = pre-rendered looped `AudioTrackAsset` | Raw PCM path exists (S7); runtime MIDI synth too heavy |
| License compliance | Publish modified doomgeneric source + build scripts publicly; link in lens description | doomgeneric is GPLv2 (S12) |

### Interpretation of the face requirement

"Replace the person's face with Doomguy's health indicator in the center" is implemented as: **the HUD's center face slot shows the player's own face**, live from the front camera, styled as a Doom status-bar portrait (palette-quantized, pixelated), and it reacts to the game exactly like Doomguy's face does (bloodier as health drops, flinch on big hits, glance toward the attacker, grin on weapon pickup, dead face at 0 HP). The original Doomguy sprite is the fallback when no face is tracked. An optional inverse mode (Doomguy sprite drawn over the user's face in the camera feed) is listed in §13.

---

## 3. Architecture

```
┌──────────────────────────── Lens (Lens Studio 5.24, TypeScript) ────────────────────────────┐
│                                                                                              │
│  DoomLens.ts (BaseScriptComponent)                                                           │
│   ├─ Boot: await createDoom() → base64 → lens_wad_alloc buf → doomgeneric_Create()            │
│   ├─ UpdateEvent: tic accumulator → doomgeneric_Tick() × n                                   │
│   ├─ Video: HEAPU8.subarray(fb) → ProceduralTexture.setPixels(80×200 RGBA)                   │
│   ├─ Palette: on lens_palette_dirty → 256×1 palette texture.setPixels                         │
│   ├─ Input: TouchStart/Move/End → key queue + mouse-dx queue (shared ring in HEAP)           │
│   ├─ Audio: drain SFX ring → AudioOutputProvider.enqueueAudioFrame                           │
│   └─ HUD: read LensState struct from HEAP32 → HudController                                 │
│                                                                                              │
│  HudController.ts        FaceController.ts                  Controls.ts                      │
│   ammo/health/armor       FaceCropTexture (face 0)           virtual stick, turn pad,        │
│   keys, weapons           → FaceDoomify material             FIRE/USE, weapon taps           │
│   STTNUM digit sprites    overlays by st_faceindex                                           │
│                           fallback: STF* sprite atlas                                        │
│                                                                                              │
│  doom.generated.js  ◄── emcc -sWASM=0 output (engine + lens platform layer), CommonJS export │
│  wad.generated.ts   ◄── base64 of stripped freedoom1.wad (E1M1 subset)                       │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.1 Platform layer: `doomgeneric_lens.c`

| Function | Implementation |
|---|---|
| `DG_Init` | No window. Set `lens_state` pointer. |
| `DG_DrawFrame` | Not used for pixels. With `CMAP256`, JS reads `I_VideoBuffer` (320×200 bytes) directly. Increments `lens_frame_counter`. |
| `DG_SleepMs` | No-op. The loop is driven by Lens `UpdateEvent`; never block. |
| `DG_GetTicksMs` | Returns `lens_clock_ms`, a virtual clock that JS advances by exactly `1000/35` ms per tic it requests. Never reads wall time. |

**Why a virtual clock:** Lens `getTime()` probably returns one value for the whole frame (V12), and `DG_SleepMs` can't block. Doom has loops that wait for the clock to move: `TryRunTics` waits for the next tic, and the screen wipe (`wipe_EndScreen`/`D_Display` melt loop) waits on `I_GetTime`. Either one would hang the lens forever. Patches:

1. Run one tic per call: set Chocolate Doom's `singletics = true` so `doomgeneric_Tick()` runs exactly one `G_Ticker` and skips `TryRunTics`. If the pinned engine commit lacks that path, patch `TryRunTics` to return straight away when `counts == 0`.
2. Turn off the melt wipe: force `wipegamestate = gamestate` in `D_Display`, so the screen cuts on level start, death and restart.
3. Make `I_FinishUpdate` (`i_video.c`) return early. Build with `DOOMGENERIC_RESX=320 RESY=200`. Otherwise it copies 320×200 → 640×400 into `DG_ScreenBuffer` every frame, which nothing reads.
4. Add `lens_skip_display`. When it is set, `D_Display` is skipped for that call, so catch-up tics don't render frames that are never shown.
5. Add a memory WAD reader: `w_file_mem.c` implements `wad_file_class_t` over a buffer JS fills via `lens_wad_alloc(size)`. No Emscripten FS, and the WAD is not stored twice in the heap.
| `DG_GetKey` | Pops from a 64-entry ring buffer in linear memory that JS writes to. |
| `DG_SetWindowTitle` | No-op. |

Additional exports (`lens_api.c`):

- `lens_post_mouse(int dx)` → `D_PostEvent(ev_mouse)` for smooth analog turning (keys alone give binary turning).
- `LensState* lens_state` updated at end of each tic from `players[consoleplayer]` and `st_stuff.c`:
  `health, armor, armortype, ammo[4], maxammo[4], readyweapon, weaponowned[9], cards[6], st_faceindex, damagecount, bonuscount, attackerdir (−1 left, 0 none, 1 right), godmode, dead, level_done, kills, items, secrets, leveltime`.
- Hook `I_SetPalette` → copy 768-byte palette to `lens_palette[]`, set `lens_palette_dirty = 1`. This carries Doom's damage (red) and pickup (gold) flashes for free.
- Hook `G_ExitLevel` / `G_SecretExitLevel` → set `level_done = 1`, freeze game, let JS show end card. E1M2 never loads.

`st_faceindex` is exposed by making it non-`static` in `st_stuff.c` (one-line patch). Pain tier math stays the engine's own (`ST_calcPainOffset`: `ST_FACESTRIDE * (((100 - health) * ST_NUMPAINFACES) / 101)`, 5 tiers).

### 3.2 Game loop

```ts
// inside DoomLens.onAwake()
const TIC_MS = 1000 / 35;           // TICRATE = 35
let acc = 0;
this.createEvent("UpdateEvent").bind(() => {
  if (this.paused) return;
  acc = Math.min(acc + getDeltaTime() * 1000, TIC_MS * 4);  // clamp: never spiral
  const n = Math.min(Math.floor(acc / TIC_MS), 3);
  for (let i = 0; i < n; i++) {
    doom.HEAP32[skipDisplayIdx] = i < n - 1 ? 1 : 0;   // render only the last tic
    doom._lens_advance_clock();                          // lens_clock_ms += 1000/35 (fixed-point in C)
    doom._doomgeneric_Tick();                            // exactly one tic (singletics)
  }
  acc -= n * TIC_MS;
  if (n) this.video.upload();        // one texture upload per Lens frame max
  this.hud.update(doom.state());
  this.audio.pump();
});
```

JS decides how many tics run each frame, and the engine never waits. When the Lens frame rate drops below 35 fps, up to 3 tics run per frame and only the last one is rendered. Below ~12 fps the game slows down instead of skipping more tics. Under `singletics`, game logic depends only on the tic count, so input timing stays deterministic.

Engine start args (passed to `doomgeneric_Create`): `-iwad freedoom1.wad -warp 1 1 -skill 3 -nomusic -mb 8`. The memory WAD reader picks up the `freedoom1.wad` name. `-warp` skips title/menu. `-mb 8` sets the zone heap to 8 MB (doomgeneric `i_system.c` default is `DEFAULT_RAM 6 /* MiB */`; 8 gives headroom). Config values (`screenblocks 11`, `detaillevel`, key bindings) are compiled-in defaults in `m_config.c`. With no FS, `M_LoadDefaults` finds no file and keeps them.

### 3.3 Build

```
emcc -O3 -DCMAP256 -DFEATURE_SOUND \
  -sWASM=0 -sENVIRONMENT=shell -sMODULARIZE=1 -sEXPORT_NAME=createDoom \
  -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFILESYSTEM=0 \
  -sINITIAL_MEMORY=16MB -sALLOW_MEMORY_GROWTH=0 -sASSERTIONS=0 \
  -sEXPORTED_FUNCTIONS=_doomgeneric_Create,_doomgeneric_Tick,_lens_advance_clock,_lens_wad_alloc,_lens_post_mouse,_lens_state_ptr,_lens_keyring_ptr,_lens_sfx_ring_ptr,_lens_palette_ptr,_lens_flags_ptr \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32,HEAPF32,stringToNewUTF8 \
  --js-library lens_imports.js \
  $(SRC_DOOM minus i_sdl*, doomgeneric_emscripten.o) doomgeneric_lens.o lens_api.o lens_sound.o w_file_mem.o \
  -o build/doom.generated.js
```

Post-process (`tools/wrap_for_lens.js`):
1. Prepend shims Lens may lack: `globalThis.performance ??= { now: () => getTime() * 1000 }`, `console ??= { log: print, warn: print, error: print }`.
2. Append `module.exports = createDoom;` so TS can `require("./doom.generated")`.
3. Strip `fetch`/`XMLHttpRequest`/`document` code paths (unreachable under `ENVIRONMENT=shell`; verify with a grep gate in CI).

Heap: 16 MB = 8 MB zone + ~1.6 MB WAD buffer + statics/stack. This replaces 24 MB, which included a MEMFS copy of the WAD. `createDoom()` returns a Promise even with `WASM=0`. Boot awaits it before calling `lens_wad_alloc`.

`-sWASM=0` is marked deprecated in Emscripten (S13). Fallback if removed: build normal wasm, then run Binaryen's standalone `wasm2js` tool on the `.wasm` and glue it to the same JS loader. Pin the Emscripten version in `tools/emsdk-version.txt`.

---

## 4. Portrait UI

Reference device: 1170×2532 (19.5:9). All layout uses Screen Transforms under an Orthographic camera, with interactive elements inside a **Safe Render** Screen Region (S9).

```
┌──────────────────────────────┐  Full Frame
│      (Snapchat top UI)       │
├──────────────────────────────┤  Safe Render top
│ ┌──────────────────────────┐ │
│ │                          │ │
│ │   3D VIEW  320×200       │ │  full width, 4:3 display aspect
│ │   (shown at 4:3)         │ │  ≈ 1170 × 878 px
│ │                          │ │
│ └──────────────────────────┘ │
│ ┌─────┬───────────┬────────┐ │
│ │AMMO │           │ HEALTH │ │  HUD strip
│ │ 50  │   FACE    │  100%  │ │  face slot: ~34% width,
│ │     │ (camera)  │ ARMOR  │ │  aspect 24:29 (STF* lump ratio)
│ │ 2 3 │           │   0%   │ │  weapon numbers tappable
│ │ 4 5 │           │ 🟦🟨🟥  │ │  key cards
│ └─────┴───────────┴────────┘ │
│                              │
│  ╭─────╮              ╭────╮ │
│  │ ◉   │   [ USE ]    │FIRE│ │  controls zone
│  ╰─────╯              ╰────╯ │
│  move/strafe   ← drag to turn →│
├──────────────────────────────┤  Safe Render bottom
│   (Snapchat capture button)  │
└──────────────────────────────┘
```

- 3D view: one `Image` with the `DoomScreen` material. Nearest-neighbor sampling. Doom's 320×200 is shown at 4:3 (original 1.2 pixel aspect), so height = width × 0.75.
- Doom's in-engine status bar is off: `screenblocks 11` (full 3D view, no bar), set as a compiled-in default (§3.2). `ST_Ticker` still runs from `G_Ticker`, so `st_faceindex` keeps updating even though `ST_Drawer` draws nothing.
- HUD art: STBAR background, STTNUM digits, STKEYS, STGNUM/STYSNUM extracted from the stripped Freedoom WAD at build time into a sprite atlas PNG, rendered with Lens `Image`s. Same look as Doom, new arrangement.
- Messages ("Picked up a shotgun") are still drawn by the engine inside the 3D view (`hu_stuff.c`), no extra work.
- Background behind UI: black. Camera feed is not shown full-screen; the front camera only feeds the face slot.
- Orientation: portrait only. Landscape is not handled in v1.

### Controls

| Zone | Gesture | Engine input |
|---|---|---|
| Bottom-left stick | Drag from touch-down point; y → forward/back, x → strafe | `KEY_UPARROW`/`KEY_DOWNARROW`, `KEY_STRAFE_L`/`R` key down/up with deadzone 15%; past 75% of stick radius also holds `KEY_RSHIFT` (run) |
| Bottom-right pad (anywhere right of center not on a button) | Horizontal drag | `lens_post_mouse(dx × sensitivity)` per move event |
| FIRE button | Hold | `KEY_FIRE` down/up |
| USE button | Tap | `KEY_USE` down, up next tic |
| HUD weapon numbers | Tap | `'1'`…`'7'` key tap |
| End card | Tap | Restart level (`G_DeferedInitNew(sk_medium, 1, 1)`) |

Multi-touch: each `TouchStartEvent` is assigned to a zone by start position and tracked by `getTouchId()` until `TouchEndEvent` (S8). Set `global.touchSystem.touchBlocking = true` while playing so Snapchat does not consume swipes. Double-tap is Snapchat's camera-flip gesture, so no control uses it. If V8 shows `touchBlocking` doesn't stop the flip, the flip is handled by the rear-camera face fallback (§5.2) and the game is not paused.

---

## 5. Face health indicator

### 5.1 Doom face states (from `st_stuff.c`)

| State | Engine trigger | Lens rendering of user's face |
|---|---|---|
| Straight ×3 | Idle, random look L/C/R | Live face, no offset |
| Pain tier 0–4 | `ST_calcPainOffset()` from health | Overlay `blood_t{0..4}.png`: tier 0 clean → tier 4 heavy (pixel-art, Doom palette) |
| Turn left/right | Damaged by attacker to the side | Crop window offset 12% toward attacker + head-turn arrow flash |
| Ouch | Damage > 20 in one hit | 150 ms: scale 1.08, red tint, shake 2 px |
| Evil grin | New weapon pickup | 1 s gold rim + `STFEVL` mouth overlay anchored to mouth landmarks |
| Rampage | Fire held > 2 s | Eyebrow-down overlay (landmark-anchored) |
| God (`STFGOD0`) | IDDQD / invuln | Glowing gold eyes overlay anchored to eye landmarks |
| Dead (`STFDEAD0`) | health = 0 | Desaturate, red multiply, X-eyes overlay, freeze last frame |

The JS side derives state from `st_faceindex` (decoded as `tier = floor(idx / ST_FACESTRIDE)`, `sub = idx % ST_FACESTRIDE`, plus `ST_GODFACE`/`ST_DEADFACE` constants), so timing and priority are exactly the engine's.

### 5.2 Pipeline

1. **Source**: `FaceCropTexture` asset, face index 0, input = Device Camera Texture (front). Output is a face-centered crop (S6). Set Face Center Mouth Weight ≈ 0.3 so eyes and mouth both sit in frame.
2. **Doomify material** (Material Editor graph, applied to the HUD face `Image`):
   - Pixelate: snap UV to a 24×29 grid (STF* lump size).
   - Quantize to the current Doom palette: sample `PLAYPAL` 256×1 texture via a precomputed 32×32×32 RGB→index LUT baked into a 1024×32 texture at build time; then look up the color in the live palette texture (so damage/pickup flashes tint the face too).
   - Composite overlay layer (blood tier, grin, god eyes) with alpha.
   - Darken border 1 px to match the STBAR face well.
3. **Landmark-anchored overlays**: `Head.getLandmarks()` (screen-space, S5) mapped into crop UV using the crop rect. Eyes/mouth overlays are positioned per frame via a small `ScreenTransform` per overlay. Tier blood overlays use fixed crop-space positions (crop is face-centered).
4. **Fallback**: when `Head.getFacesCount() == 0` for > 0.5 s, or the user flips to the rear camera, swap to the original `STF*` sprite atlas and pick the frame by `st_faceindex`. Fade 200 ms between modes.

Overlay art is stylized 8-bit pixel art in Doom palette. No realistic wounds (S11: "Realistic, disturbing harm" prohibited).

---

## 6. Video path

```ts
// VideoOut.ts
const W = 320, H = 200, PW = W / 4;               // 80 texels × 200, 4 indices per RGBA texel
this.screenTex = ProceduralTextureProvider.createWithFormat(PW, H, TextureFormat.RGBA8Unorm);
this.screenProv = this.screenTex.control as ProceduralTextureProvider;
this.palTex = ProceduralTextureProvider.createWithFormat(256, 1, TextureFormat.RGBA8Unorm);
this.palProv = this.palTex.control as ProceduralTextureProvider;
this.fbView = doom.HEAPU8.subarray(fbPtr, fbPtr + W * H);  // no copy

upload() {
  this.screenProv.setPixels(0, 0, PW, H, this.fbView);
  if (doom.HEAP32[paletteDirtyIdx]) { this.palProv.setPixels(0, 0, 256, 1, this.palRGBA()); doom.HEAP32[paletteDirtyIdx] = 0; }
}
```

`DoomScreen` material (Material Editor):
- `px = floor(uv.x * 320)`, `py = floor((1 - uv.y) * 200)` (flip: `setPixels` region extends upward, S4; Doom rows go downward).
- `texel = sample(screenTex, ((floor(px / 4) + 0.5) / 80, (py + 0.5) / 200))`, nearest.
- `idx = texel[px mod 4] * 255`.
- `color = sample(palTex, ((idx + 0.5) / 256, 0.5))`.

Both procedural textures carry data, not color. They need nearest filtering, no mipmaps, no sRGB decode, and no premultiplied alpha, because the alpha byte holds palette indices (V13). If alpha is premultiplied, any pixel whose 4th index is 0 zeroes the other three.

Fallback if channel select by index is awkward in the graph: JS expands to 320×200 RGBA with a `Uint32Array` palette LUT (64,000 writes/frame) and the material just samples. Pick after Phase 0 profiling.

---

## 7. Audio

| Part | Approach |
|---|---|
| SFX | `lens_sound.c` implements `sound_module_t`: 8 channels, Doom's DS* lumps (8-bit unsigned PCM, 11025 Hz), stereo separation + volume from `S_UpdateSounds`. Mixes into a float ring buffer at the provider's `sampleRate`. JS each frame reads the sample count the `AudioOutputProvider` requests and calls `enqueueAudioFrame` (S7). |
| Music | E1M1 track from Freedoom's MIDI rendered offline (FluidSynth + a GM soundfont with a permissive license) to mono AAC ~64 kbps, loop-trimmed. Imported as `AudioTrackAsset`, looped on an `AudioComponent`. `-nomusic` keeps the engine silent. |
| Mute | Respect Snapchat mute; no UI of our own in v1. |

---

## 8. Asset pipeline and size budget

`tools/wadstrip.py` (Python, reads Freedoom WAD):
1. Keep: `PLAYPAL`, `COLORMAP`, `TEXTURE1`, `PNAMES`, `E1M1` + its 10 map lumps, sky texture, every patch/flat referenced by E1M1 sidedefs/sectors, all sprites for things placed in E1M1 + their states' frames (walk `info.c` state chains from each placed thing type, plus player, weapons, projectiles, blood, puffs, teleport fog), `STBAR`/`ST*` HUD lumps, `STF*` faces, used `DS*` sounds, `M_DOOM`/menu skull only if menu kept (it is not).
2. Drop: `GENMIDI`, `D_*` music, `DMXGUS`, `DEMO*`, `ENDOOM`, other maps.
   **Engine-required lumps (stripping these fails at startup, not at render):**
   - Texture index 0 (the null texture, `AASHITTY`/first entry) must stay first in `TEXTURE1`.
   - Every texture in the episode-1 switch list (`P_InitSwitchList`, `R_TextureNumForName` → `I_Error`), plus their patches. `R_InitTextures` fails on any texture whose patch is missing.
   - Animated flat/texture ranges in `p_spec.c` `animdefs`: if a range's start exists, keep **every lump between start and end in WAD order**, because flats are animated by lump number. Otherwise drop the whole range.
   - `F_SKY1` flat and the `SKY1` texture.
   - `STCFN033`–`STCFN095` HUD font (pickup messages), and every lump `ST_loadGraphics` caches at init.
   - Sprites for items monsters drop (zombieman → clip, shotgun guy → shotgun), not only things placed in the map.
   - All rotations of every sprite kept: a partial rotation set triggers `I_Error` in `R_InitSprites`.
   - `WI*` lumps are not needed, because `G_ExitLevel` is hooked before intermission.
3. Rebuild directory, verify with a headless native doomgeneric build that runs E1M1 for 2,000 tics with a scripted input demo and no `W_GetNumForName` errors.
4. Emit `wad.generated.ts` (base64 string, chunked into 64 KB string literals) and `hud_atlas.png`, `face_fallback_atlas.png`, `quant_lut.png`.

| Item | Estimate | Notes |
|---|---|---|
| `doom.generated.js` (wasm2js, -O3, minified) | 1.5–2.5 MB | Measure in Phase 0 |
| Stripped WAD (raw) | 1.2–1.8 MB | Freedoom 0.13.0 release zip (both IWADs) is 24 MB; E1M1 subset is a small fraction of `freedoom1.wad` |
| Base64 overhead | +33% of WAD | ~0.4–0.6 MB |
| Music (AAC, ~2.5 min mono) | ~1.2 MB | Trim or shorten loop if over budget |
| HUD + face atlases, LUT, overlays | ~0.3 MB | PNG, indexed |
| Lens scene, materials, scripts | ~0.2 MB | |
| **Total** | **~4.8–6.6 MB** | Cap 8 MB (S10). Remote Assets (S10) are excluded: requirement is fully contained |

Over-budget levers, in order: lower music bitrate → drop music → Closure-compile engine JS → drop HUD art the native Lens HUD doesn't use (keep anything `ST_loadGraphics` caches). Sprite rotations are not a lever (§8 step 1).

Runtime memory: 16 MB Emscripten heap (the WAD is decoded straight into a heap buffer from `lens_wad_alloc`) + the base64 string literal. Drop the base64 reference after decoding.

Boot cost: base64 decode of ~1.6 MB in JS runs across several frames (`DelayedCallbackEvent` chunks of 256 KB) behind a loading card ("LOADING E1M1"). Each chunk decodes directly into `HEAPU8`.

---

## 9. Performance plan

Original Doom ran 35 fps on a 486; the unknown is the Lens JS engine's speed on wasm2js output (no documented JIT information, S3). This is the #1 risk and Phase 0 exists to measure it.

| Lever | Effect |
|---|---|
| `-O3`, no assertions, no memory growth | Tight asm.js-style code |
| Doom low detail (`detailLevel 1`) | Halves column rendering cost; native Doom option. First lever to pull |
| Smaller view (`screenblocks 10`, show only the top 168 rows) | ~16% fewer pixels drawn. Under 11, the engine draws its own status bar into the bottom 32 rows, so the material crops them out (`py` range 0–167, 4:3 aspect recomputed). `screenblocks ≤ 9` adds a border and isn't worth it |
| Render decoupled from tics | Up to 3 tics per Lens frame, 1 upload |
| Zero-copy framebuffer view | `HEAPU8.subarray` into `setPixels`; no per-frame allocation |
| Packed 8-bit upload | 16 KB/frame |
| No per-frame object allocation in TS | Reuse arrays/vec objects; avoid GC hitches |

Budget per Lens frame (30 fps target on mid-range 2023 Android, iPhone 12+): engine ≤ 18 ms, upload + HUD + face ≤ 6 ms. Floor: 20 fps game render with 35 Hz tics preserved.

---

## 10. Lens lifecycle

| Event | Behavior |
|---|---|
| Lens start | Loading card → decode WAD → `await createDoom()` → decode WAD into `lens_wad_alloc` buffer → `doomgeneric_Create(args)` → "TAP TO START" over the first rendered frame (needed anyway to unlock audio and show controls overlay) |
| Playing | Loop per §3.2 |
| Player death | Engine handles; dead face; "TAP TO RETRY" after 2 s → reload level |
| `level_done` | End card: time, kills %, items %, secrets % from `LensState`; the user's face from the last frame as a "Doom portrait" — natural Snap capture moment. Tap → restart |
| Lens backgrounded / camera flipped | Pause tics (stop calling `doomgeneric_Tick`, reset accumulator on resume); flip to rear camera → fallback face |
| Persistence | `global.persistentStorageSystem.store` best time and best kills %. No savegames |

---

## 11. Project layout

```
doom-lens/
  engine/                 # fork of ozkl/doomgeneric (GPLv2), pinned commit
    doomgeneric/*.c       # upstream + small patches (st_faceindex export, exit hook, I_SetPalette hook, singletics, wipe off, I_FinishUpdate early return, lens_skip_display)
    lens/doomgeneric_lens.c lens_api.c lens_sound.c w_file_mem.c lens_imports.js
    Makefile.lens
  data/
    freedoom1.wad         # pinned Freedoom release, BSD-3, COPYING + CREDITS alongside
    music/e1m1.mid
  tools/
    wadstrip.py  wrap_for_lens.js  build_atlases.py  render_music.sh  emsdk-version.txt
  lens/                   # Lens Studio 5.24 project
    Assets/Scripts/DoomLens.ts VideoOut.ts HudController.ts FaceController.ts Controls.ts AudioOut.ts
    Assets/Scripts/doom.generated.js wad.generated.ts
    Assets/Materials/DoomScreen.ss_graph FaceDoomify.ss_graph
    Assets/Textures/hud_atlas.png face_fallback_atlas.png quant_lut.png overlays/*.png
    Assets/Audio/e1m1.m4a
  LICENSES/  GPL-2.0.txt  Freedoom-BSD-3.txt  NOTICE.md
```

`make lens` runs: wadstrip → atlases → emcc → wrap → copy generated files into `lens/Assets/Scripts/`. CI checks generated size against the 8 MB budget.

---

## 12. Verification items (must be confirmed in Lens Studio before Phase 1)

| # | Question | Test | Fallback |
|---|---|---|---|
| V1 | Does the Lens JS engine run 2 MB of asm.js-style JS, and how fast? | Load `doom.generated.js`, run 350 tics headless, time on 3 devices | Port hot loops (`R_DrawColumn`, `R_DrawSpan`) to hand-tuned TS; reduce resolution to 160×100 |
| V2 | Max script file / string literal size | Import 2.5 MB JS + 2 MB base64 TS | Split into multiple modules; chunked string array |
| V3 | `setPixels` accepts a `subarray` view of a larger buffer | Pass `HEAPU8.subarray` | `slice()` into a reused 16 KB buffer |
| V4 | `setPixels` per-frame cost at 80×200 | Profile in Lens Studio + device | Halve framerate of uploads |
| V5 | Global shims needed (`performance`, `console`, `TextDecoder`) | Grep Emscripten output; run in Preview | Add shims in wrapper |
| V6 | `FaceCropTexture` works from front camera while the camera feed is not displayed full-screen | Build HUD scene, test on device | Use `FaceInsetVisual` (Face region) in a screen-space object (S6) |
| V7 | `AudioOutputProvider` latency and sample rate on device | Play a 1 kHz test tone via enqueue | SFX as pre-baked `AudioTrackAsset`s triggered by sound id |
| V8 | `touchSystem.touchBlocking` blocks Snapchat swipes | Device test | Keep controls inside Safe Render, avoid edge swipes |
| V9 | Lens Studio TS can `require` a CommonJS `.js` module | Import wrapper | Convert to ES module export; or rename to `.ts` with `// @ts-nocheck` |
| V10 | Lens suspend/resume event names in 5.24 | API reference check | Detect large `getDeltaTime()` gaps and pause |
| V11 | Final package size after Lens compression | Build + check Project Info size | Levers in §8 |
| V12 | Is `getTime()` constant within a frame? Is there a wall clock (`getRealTimeNanos` or similar)? | Log `getTime()` twice around a busy loop | The design doesn't depend on the answer (virtual clock, §3.1). This only affects profiling |
| V13 | Can a `ProceduralTexture` be set to nearest, no mips, no premultiply? | Upload a known index pattern including alpha=0 bytes; read back via the material | Unpack to 320×200 RGBA in JS (§6 fallback) |
| V14 | Lens JS heap limit with a 16 MB `ArrayBuffer` + 2 MB string literal on low-end Android | Device test; watch for lens kill | Shrink the zone to `-mb 6` (upstream default); shorter music |
| V15 | Does `touchBlocking` stop double-tap camera flip and swipe-to-dismiss? | Device test | Covered by face fallback (§4); no game control depends on it |

**Phase 0 kill criterion for V1:** if engine time per tic at `detailLevel 1` is above 40 ms on the mid-range Android reference device, wasm2js isn't viable. Before Phase 1, decide between 160×100 internal resolution and stopping the project. Hand-porting hot loops to TS is unlikely to win back more than 2× if the engine is interpreter-only (no JIT, likely on iOS).

---

## 13. Phases

| Phase | Scope | Exit criteria |
|---|---|---|
| 0 — Spike (1 wk) | Native headless doomgeneric with lens platform layer; wasm2js build; run in Lens Studio Preview + 2 devices; answer V1–V5, V9 | E1M1 renders to a procedural texture; measured ms/tic on devices |
| 1 — Playable (2 wk) | Video path, touch controls, game loop, WAD strip pipeline, boot/end flow | E1M1 completable on device at ≥ 20 fps, package < 8 MB |
| 2 — HUD + Face (2 wk) | Portrait HUD from `LensState`; FaceCropTexture + Doomify shader; overlays by `st_faceindex`; fallback sprites | All 8 face states verified via cheat-driven test script (`IDDQD`, scripted damage) |
| 3 — Audio (1 wk) | SFX mixer + enqueue; pre-rendered music | No audible underruns over a 5 min session |
| 4 — Polish + submit (1 wk) | Perf tuning, end card, persistence, licenses/NOTICE, source repo public, submission review | Passes Lens submission; GPL source link live |

Optional after v1: inverse face mode (Doomguy `STF*` face rendered onto the user's face in the camera view via Face Mesh texture, toggled by tapping the HUD face); head-turn to look (map head yaw to `lens_post_mouse`).

---

## 14. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Lens JS engine too slow for wasm2js Doom | Unplayable | Phase 0 gate; low detail; smaller view; hand-port renderer hot loops |
| Emscripten removes `-sWASM=0` | Build breaks | Pin emsdk; Binaryen `wasm2js` fallback |
| 8 MB cap exceeded | Cannot submit | Budget table levers; CI size gate |
| GPLv2 vs Snap distribution terms | Legal | Publish full corresponding source; legal review before submission |
| Content review (weapons, blood) | Rejection | Freedoom's fictional weapons; stylized pixel overlays; no realistic injury (S11) |
| Trademark: title "minidoom" contains "Doom" (owner accepted); Doomguy likeness | Rejection at review or takedown | Freedoom assets only; no id logos; Freedoom's own status face art for fallback. If rejected, rename without "doom" and resubmit |
| Face tracking lost mid-play | Broken HUD | Sprite fallback with fade |
| Touch conflicts with Snapchat gestures | Bad controls | `touchBlocking`; controls inside Safe Render; no double-tap controls |
| Engine wait loops hang on frame-constant clock | Lens freezes on boot | Virtual clock + `singletics` + wipe disabled (§3.1); headless test runs with a frozen wall clock |

---

## 15. Open questions — resolved

| # | Question | Resolution | By |
|---|---|---|---|
| Q1 | Face requirement: user's face in the HUD slot, or Doomguy over the user's face? | **User's live face in the HUD center slot**, reacting to damage. Doomguy-over-face stays post-v1 (§13) | Owner, 2026-11-10 |
| Q2 | Freedoom vs id shareware `doom1.wad` | **Freedoom Phase 1.** Accepted that its E1M1 is not id's Hangar map | Owner, 2026-11-10 |
| Q3 | How does the engine wait for time with no sleep and a frame-constant clock? | Virtual clock driven by JS; `singletics`; wipe off (§3.1) | Review |
| Q4 | Run toggle on double-tap clashes with Snapchat camera flip | Run while the stick is pushed past 75% (§4) | Review |
| Q5 | Is Emscripten FS needed? | No. Memory WAD reader and compiled-in config; heap 24 → 16 MB (§3.3) | Review |
| Q6 | Status bar vs `screenblocks` perf lever | `screenblocks 11` by default; 10 with crop as a lever (§9) | Review |
| Q7 | Lens title | **"minidoom"**; trademark risk accepted (§14) | Owner, 2026-11-10 |

Still open (need outside input, not design decisions):

- **GPL v2 vs Snap Lens distribution terms:** needs legal review before Phase 4 submission. The design already assumes full source is published.
- **Engine speed (V1):** answered only by Phase 0 on devices. The kill criterion is in §12.
