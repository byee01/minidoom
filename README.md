# minidoom

Freedoom E1M1 as a Snapchat Lens: Doom-engine port (doomgeneric, C → pure JS), portrait
touch controls, and a status-bar face that is *your* face from the front camera, bloodied by
in-game health. Design and as-built notes: `DESIGN.md`. Licenses: `LICENSES/NOTICE.md`.

## Build

Requires macOS, Lens Studio 5.24+, Homebrew `emscripten`, `ffmpeg`, Python 3, Node, git, curl
(`fluid-synth` only to re-render the music).

Third-party inputs are not stored in this repo. `tools/fetch_deps.sh` (run by `build_all.sh`)
downloads them pinned and checksummed: doomgeneric at commit 63e2d26 (mirrored unmodified on this
repo's `doomgeneric-upstream` branch) with
`engine/patches/minidoom-lens.patch` applied, Freedoom 0.13.0, and (only for
`tools/render_music.sh`) the FluidR3Mono soundfont. The rendered `data/music/music_e1m1.mp3` is
committed.

```
tools/build_all.sh                    # release: engine, tests, WAD strip, Lens project, export
MINIDOOM_DEBUG=1 tools/build_all.sh   # debug: adds the in-Lens autotest + render captures
tools/run_tests.sh                    # native vs shipped-JS behaviour tests (frames must match)
tools/ls_preview.sh 45                # open in Lens Studio, collect [minidoom] log lines
```

Outputs: `lens/minidoom.esproj` (generated project, open in Lens Studio to submit) and
`build/export/minidoom.zip` (exported lens).

Testing on a phone and submitting the lens: see `PUBLISHING.md`.

## Source layout

- `engine/doomgeneric/` – upstream doomgeneric (GPLv2), fetched by `tools/fetch_deps.sh` and patched with `#ifdef LENS` changes
  (`engine/patches/minidoom-lens.patch` against commit 63e2d26).
- `engine/lens/` – Lens platform layer, HUD/face renderer, SFX mixer, headless test driver.
- `lens_src/` – Lens scripts (Main.js, Ui.js, DebugCapture.js).
- `tools/` – WAD stripper, wasm2js wrapper, Lens Studio project builder, test/bench scripts.
- `tests/` – scripted behaviour tests (run on native and JS builds).
- `data/` – Freedoom 0.13.0 (BSD-3), rendered music, icon.
