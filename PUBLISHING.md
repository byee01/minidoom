# Deploy and publish checklist

From source change to a public Lens. Work top to bottom; each section assumes the one before it
passed. Background and sources for the platform limits are in `DESIGN.md` (§8, sources S10–S12,
risks table).

## 1. Build

- [ ] Close Lens Studio first. `tools/build_all.sh` deletes and regenerates `lens/`, and the
      preview script refuses to run while Lens Studio is open.
- [ ] Debug build + in-Lens autotest:
      `MINIDOOM_DEBUG=1 tools/build_all.sh && tools/ls_preview.sh 45 lens_debug`
  - [ ] Log ends with `AUTOTEST DONE pass N fail 0`
  - [ ] No `BOOT FAILED`, `TypeError`, `ReferenceError` or `Script ... error` lines
- [ ] Release build: `tools/build_all.sh`
  - [ ] All `tests/*.txt` print `ok` (native and JS builds must agree)
  - [ ] Build log ends with `BUILD saved OK`
  - [ ] Only the two known Lens Studio warnings appear in `build/ls_build.log` /
        `build/ls_export.log`: `with-event-loop option is deprecated` (printed by the Lens Studio
        CLI itself) and the `SpectaclesCompatibility` network error (not relevant to a phone lens).
        Anything else is new: investigate.
- [ ] `build/export/minidoom.zip` is under the **8 MB** lens size limit (currently ~3.5 MB)
- [ ] `MINIDOOM_TRY_RECORD` is unset or `0` (the default). `1` makes the build call
      `scene.forceRecordFrame()`, an **undocumented** API; use it only for device experiments,
      never for a submitted build. Check: `build/stage/BuildConfig.js` has `TRY_FORCE_RECORD = 0`.

## 2. Open in Lens Studio

- [ ] Open `lens/minidoom.esproj` (release project, not `lens_debug/`)
- [ ] Logger shows no warnings at start, in particular no deprecated-API warnings
- [ ] Logger shows `[minidoom] face: render target ok, head tracker ok, source image ok` and
      `[minidoom] game camera ok`
- [ ] Project Info: lens name, icon set, front camera on activation

## 3. Test on a phone

Send to the phone: log in to Lens Studio with your Snapchat account → **Send to Snapchat** →
open the notification in Snapchat. Keep the Logger open; device logs stream to it.

Test on at least one iPhone and one mid-range Android (performance of the JS engine is the main
device risk, see `DESIGN.md` V14).

- [ ] Title card shows; tap starts; music plays
- [ ] Left stick moves, right stick turns, drag on the face turns; sticks sit at the screen edges
- [ ] Auto-fire and auto-open doors work; HUD taps 2–7 switch weapons
- [ ] Frame rate is playable; the log's `quality ->` lines show adaptive quality settling
- [ ] **Face slot**
  - [ ] Face appears in the bevelled frame below the HUD; Doomguy shows when no face is tracked
  - [ ] `[minidoom] face window:` log line appears; face is centred, upright and not mirrored
        compared with the screen
  - [ ] Leaning in close and turning the head: slot stays filled with camera image (no streaks,
        no black or white areas), no hard colour bands on skin
  - [ ] Pain/ouch/grin/dead tints and blood overlays show; dead face freezes until retry
- [ ] Bottom of the face vs Snapchat's capture button and carousel: acceptable overlap
      (tune `FACE_PUSH` in `lens_src/Ui.js` if not)
- [ ] Enemies at the left/right edges of the cropped 3D view are visible in time
- [ ] Death → `TAP TO RETRY` → retry works
- [ ] Finish E1M1 (or debug-exit): end card shows time/kills/best, and the replay plays
      **gameplay only** (no face, HUD or sticks)
- [ ] Record a Snap from the title card: the run starts with it and the Snap captures the game
- [ ] Rear camera: lens still works (Doomguy face)
- [ ] Background/foreground the app mid-run: game pauses, controls don't stick
- [ ] Run for several minutes: no lens kill, no memory warnings

## 4. Before submitting

- [ ] **GPL v2 (engine):** the full corresponding source (patched doomgeneric, `engine/lens/`,
      `lens_src/`, `tools/`, `tests/`, build scripts) is pushed to
      https://github.com/byee01/minidoom (public, GPL-2.0 `LICENSE`), at the revision being
      submitted. Put the URL in the lens description.
- [ ] `LICENSES/` (NOTICE, GPL-2.0, Freedoom BSD-3 and credits, FluidR3Mono MIT) is included in
      the published source
- [ ] Legal review of GPL v2 against Snap's lens distribution terms (open item in `DESIGN.md`)
- [ ] **Content review risk:** weapons are fictional/cartoonish Freedoom sprites; blood is
      stylized pixel overlays; nothing realistic (Snap submission guidelines prohibit realistic,
      disturbing harm)
- [ ] **Trademark risk:** no id Software / DOOM logos or art; Freedoom assets only. The name
      "minidoom" contains "Doom". If rejected for that, rename and resubmit.
- [ ] Icon: `data/icon/icon.png` (512×512) looks right at small size in the Lens carousel
- [ ] Optional preview video/image recorded from a phone run

## 5. Submit

- [ ] Lens Studio → **Publish Lens**: log in, fill in name, description (with the source URL),
      category and visibility. Fix anything the publish dialog flags.
- [ ] Submit for review; watch status at my-lenses.snapchat.com
- [ ] If rejected: read the reason, fix, rebuild (section 1), re-test (section 3), resubmit

## 6. After it goes live

- [ ] Open the public lens from its Snapcode/link on a phone that never had the test build
- [ ] Spot-check section 3 on the live version
- [ ] Record the published version and date (the build is regenerated each time, so note which
      source revision was submitted)
