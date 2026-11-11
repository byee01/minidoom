# minidoom — third-party notices

minidoom is a Snapchat Lens that plays the first level (E1M1) of Freedoom on a
Doom-engine port. It is not affiliated with or endorsed by id Software,
ZeniMax/Bethesda or Microsoft. "DOOM" is a trademark of its owner.

## Engine — GNU GPL v2 (or later)

- doomgeneric by ozkl (https://github.com/ozkl/doomgeneric), pinned at commit
  `63e2d26a8c6b67fa922aff7782e315e548bf28e3`, derived from Chocolate Doom
  (Simon Howard and contributors) and the DOOM source release by id Software.
- minidoom changes: `engine/doomgeneric` patches (all marked `#ifdef LENS`) and
  the Lens platform layer in `engine/lens/`. Full corresponding source,
  including the build scripts that produce `doomEngine.js`, is published with
  this project. See `GPL-2.0.txt`.

## Game data — BSD 3-Clause

- Freedoom Phase 1, version 0.13.0 (https://freedoom.github.io), stripped to the
  lumps E1M1 needs by `tools/wadstrip.py`. Copyright © 2001-2024 Contributors to
  the Freedoom project. See `Freedoom-BSD-3.txt` and `Freedoom-CREDITS.txt`.
- The E1M1 music is Freedoom's `D_E1M1` MIDI, rendered offline.

## Music rendering — MIT

- FluidR3Mono General MIDI SoundFont. FluidR3 (original version) by Frank Wen,
  Copyright © 2000-2002, 2008. Mono conversion (FluidR3Mono) by Michael Cowgill,
  Copyright © 2014-17. Released under the MIT license. See `FluidR3Mono-MIT.md`.
