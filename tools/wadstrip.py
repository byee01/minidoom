"""Strip freedoom1.wad down to what E1M1 can reach.

Reachability is computed from the engine's own tables (info.c, d_items.c, sounds.c,
p_spec.c, p_switch.c) and action-function bodies, so the subset tracks the engine.
Lumps the engine requires at init but that the walk can't see are listed in
ALWAYS_KEEP_* below, each with the reason.

usage: python3 tools/wadstrip.py <in.wad> <out.wad> [--report]
"""
import os
import re
import struct
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(__file__))
from wadlib import Wad  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENG = os.path.join(ROOT, "engine/doomgeneric/doomgeneric")
MAP = "E1M1"
MAP_LUMPS = ["THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS",
             "NODES", "SECTORS", "REJECT", "BLOCKMAP"]

# Init-time loads the reachability walk can't infer (verified by tests/strip_boot.txt).
ALWAYS_KEEP_EXACT = {
    "PLAYPAL", "COLORMAP", "FREEDOOM",   # palette, light tables, Freedoom detection
    "STBAR", "STARMS",                   # ST_loadGraphics (bar is hidden but still loaded)
    "D_E1M1",                            # S_ChangeMusic resolves the lump even with -nomusic
}
ALWAYS_KEEP_PREFIX = (
    "STTNUM", "STTPRCNT", "STTMINUS", "STYSNUM", "STGNUM", "STKEYS", "STFB", "STF",  # ST_loadGraphics
    "STCFN",                                                                          # HU font
)


def src(name):
    return open(os.path.join(ENG, name), encoding="latin-1").read()


def enum_list(text, close):
    """Return enum member names in order, from 'typedef enum {' to '} close;'."""
    end = text.index("} " + close)
    start = text.rindex("typedef enum", 0, end)
    body = re.sub(r"//.*", "", text[start:end])
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [m.split("=")[0].strip() for m in body[body.index("{") + 1:].split(",") if m.strip()]


def parse_engine():
    info_h, info_c = src("info.h"), src("info.c")
    sprites = enum_list(info_h, "spritenum_t")
    states_enum = enum_list(info_h, "statenum_t")
    mobjtypes = enum_list(info_h, "mobjtype_t")

    states = {}
    for m in re.finditer(r"\{(SPR_\w+),[^,]+,[^,]+,\{(\w+)\},(S_\w+),[^}]*\},?\s*// (S_\w+)", info_c):
        spr, action, nxt, name = m.groups()
        states[name] = (spr, None if action == "NULL" else action, nxt)
    assert len(states) == len([s for s in states_enum if s != "NUMSTATES"]), "state parse"

    mobjinfo = {}
    for m in re.finditer(r"\{\s*// (MT_\w+)(.*?)\n    \}", info_c, re.S):
        fields = dict((f, v.strip()) for v, f in re.findall(r"\s*([^\n]+?),?\s*// (\w+)", m.group(2)))
        mobjinfo[m.group(1)] = fields
    assert len(mobjinfo) == len([t for t in mobjtypes if t != "NUMMOBJTYPES"]), "mobjinfo parse"

    weapon_states = set(re.findall(r"\bS_\w+", src("d_items.c")))

    # sfx name table: sfx_xxx -> lump name, following SOUND_LINK
    sounds_c = src("sounds.c")
    sfx_enum = enum_list(src("sounds.h"), "sfxenum_t")
    entries = re.findall(r"SOUND(?:_LINK)?\(\"(\w+)\"[^)]*?(?:&S_sfx\[(sfx_\w+)\])?[^)]*\)", sounds_c)
    # S_sfx[] is index-aligned with sfxenum_t, including the leading "none" entry for sfx_None.
    assert len(entries) == len(sfx_enum) - 1, "S_sfx/sfxenum_t length mismatch"   # minus NUMSFX
    sfx = {}
    for i, (name, link) in enumerate(entries):
        sfx[sfx_enum[i]] = (name, link or None)
    assert sfx["sfx_pistol"][0] == "pistol" and sfx["sfx_popain"][0] == "popain", "sfx table misaligned"
    return sprites, states, mobjinfo, weapon_states, sfx


def functions(text):
    """Map function name -> body for top-level C functions."""
    out = {}
    for m in re.finditer(r"^(?:void|boolean|int|static void|static boolean|static int|fixed_t)\s+\*?(\w+)\s*\([^;{]*\)\s*\{", text, re.M):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out[m.group(1)] = text[m.end():i]
    return out


def spawned(body, prefix):
    """Identifiers used as values (spawn/state args), not in type comparisons like `x->type == MT_CYBORG`."""
    out = []
    for m in re.finditer(r"\b%s\w+" % prefix, body):
        before = body[max(0, m.start() - 12):m.start()]
        if re.search(r"(==|!=|case)\s*$", before):
            continue
        out.append(m.group(0))
    return out


def reachable(wad, report):
    sprites, states, mobjinfo, weapon_states, sfx = parse_engine()
    action_bodies = {}
    for f in ("p_enemy.c", "p_pspr.c"):
        action_bodies.update(functions(src(f)))
    by_ednum = {int(v["doomednum"]): k for k, v in mobjinfo.items() if v["doomednum"].lstrip("-").isdigit()}

    # Things placed in E1M1 for skill 3 (medium), single player.
    mi = wad.index(MAP)
    things = wad.lumps[mi + 1][1]
    types = set()
    for i in range(0, len(things), 10):
        _, _, _, t, flags = struct.unpack_from("<hhhhh", things, i)
        if t in (1, 2, 3, 4, 11):
            continue  # player starts
        if not (flags & 2) or (flags & 16):
            continue
        if t in by_ednum:
            types.add(by_ednum[t])
    placed = sorted(types)

    # Always-present actors: player, fx, teleport, weapon projectiles (any weapon via pickups or cheats).
    types |= {"MT_PLAYER", "MT_PUFF", "MT_BLOOD", "MT_TFOG", "MT_IFOG", "MT_TELEPORTMAN"}
    drops = {"MT_POSSESSED": "MT_CLIP", "MT_SHOTGUY": "MT_SHOTGUN", "MT_CHAINGUY": "MT_CHAINGUN",
             "MT_WOLFSS": "MT_CLIP"}
    state_queue = list(weapon_states)
    kept_states, kept_sfx = set(), set()
    type_queue = list(types)
    seen_types = set()
    state_fields = ("spawnstate", "seestate", "painstate", "meleestate", "missilestate",
                    "deathstate", "xdeathstate", "raisestate")
    sound_fields = ("seesound", "attacksound", "painsound", "deathsound", "activesound")
    while type_queue or state_queue:
        while type_queue:
            t = type_queue.pop()
            if t in seen_types:
                continue
            seen_types.add(t)
            info = mobjinfo[t]
            state_queue += [info[f] for f in state_fields]
            kept_sfx |= {info[f] for f in sound_fields}
            if t in drops:
                type_queue.append(drops[t])
        while state_queue:
            s = state_queue.pop()
            if s in kept_states or s not in states:
                continue
            kept_states.add(s)
            spr, action, nxt = states[s]
            state_queue.append(nxt)
            if action and action in action_bodies:
                body = action_bodies[action]
                type_queue += spawned(body, "MT_")
                state_queue += spawned(body, "S_")
                kept_sfx |= set(re.findall(r"\bsfx_\w+", body))

    # Sounds referenced by always-running code (pickups, doors, switches, player, menu-free HUD).
    for f in ("p_inter.c", "p_mobj.c", "p_map.c", "p_telept.c", "p_spec.c", "p_switch.c", "p_doors.c",
              "p_plats.c", "p_floor.c", "p_ceilng.c", "p_lights.c", "p_user.c", "g_game.c", "s_sound.c",
              "st_stuff.c", "hu_stuff.c", "p_pspr.c"):
        kept_sfx |= set(re.findall(r"\bsfx_\w+", src(f)))
    kept_sfx.discard("sfx_None")
    lumps_sfx = set()
    for s in kept_sfx:
        if s not in sfx:
            continue
        name, link = sfx[s]
        while link:
            name, link = sfx[link]
        lumps_sfx.add(("DS" + name).upper())

    kept_sprites = {states[s][0][4:] for s in kept_states if s != "S_NULL"}
    if report:
        print("placed types:", placed)
        print("sprites (%d):" % len(kept_sprites), " ".join(sorted(kept_sprites)))
        print("sounds (%d):" % len(lumps_sfx), " ".join(sorted(lumps_sfx)))
    return kept_sprites, lumps_sfx


def texture_subset(wad, report):
    """Return (texture1_bytes, pnames_bytes, patch_names, flats_needed_names)."""
    pn = wad.get("PNAMES")
    npn = struct.unpack_from("<i", pn, 0)[0]
    pnames = [pn[4 + 8 * i:12 + 8 * i].rstrip(b"\0").decode().upper() for i in range(npn)]
    # Engine numbers textures TEXTURE1 then TEXTURE2; merging keeps that numbering.
    texdefs = OrderedDict()
    for lump in ("TEXTURE1", "TEXTURE2"):
        t1 = wad.get(lump)
        if t1 is None:
            continue
        nt = struct.unpack_from("<i", t1, 0)[0]
        for o in struct.unpack_from("<%di" % nt, t1, 4):
            name = t1[o:o + 8].rstrip(b"\0").decode().upper()
            npatch = struct.unpack_from("<h", t1, o + 20)[0]
            texdefs.setdefault(name, t1[o:o + 22 + 10 * npatch])
    order = list(texdefs)

    mi = wad.index(MAP)
    sides = wad.lumps[mi + 3][1]
    secs = wad.lumps[mi + 8][1]
    used = set()
    for i in range(0, len(sides), 30):
        for o in (4, 12, 20):
            used.add(sides[i + o:i + o + 8].rstrip(b"\0").decode().upper())
    used.discard("-")
    flats = set()
    for i in range(0, len(secs), 26):
        for o in (4, 12):
            flats.add(secs[i + o:i + o + 8].rstrip(b"\0").decode().upper())
    flats.add("F_SKY1")
    used.add("SKY1")
    used.add(order[0])  # texture 0 is the null texture; must stay first

    # Episode-1 switch textures: P_InitSwitchList resolves every one with R_TextureNumForName.
    for m in re.finditer(r'\{"(\w+)",\s*"(\w+)",\s*(\d)\}', src("p_switch.c")):
        if m.group(3) == "1":
            used |= {m.group(1).upper(), m.group(2).upper()}

    # Animated ranges: if any member is used, keep the whole contiguous range.
    anims = re.findall(r'\{(-?\d|false|true),\s*"(\w+)",\s*"(\w+)",\s*\d+\}', src("p_spec.c"))
    tex_ranges, flat_ranges = [], []
    for istex, end, start in anims:
        (tex_ranges if istex in ("1", "true") else flat_ranges).append((start.upper(), end.upper()))
    for start, end in tex_ranges:
        if start in texdefs and end in texdefs:
            a, b = order.index(start), order.index(end)
            rng = order[min(a, b):max(a, b) + 1]
            if used & set(rng):
                used |= set(rng)

    missing = sorted(u for u in used if u not in texdefs)
    assert not missing, "textures not in TEXTURE1: %s" % missing

    kept = [n for n in order if n in used]
    patch_idx = OrderedDict()
    new_defs = []
    for n in kept:
        d = bytearray(texdefs[n])
        npatch = struct.unpack_from("<h", d, 20)[0]
        for k in range(npatch):
            po = 22 + 10 * k
            old = struct.unpack_from("<h", d, po + 4)[0]
            pname = pnames[old]
            if pname not in patch_idx:
                patch_idx[pname] = len(patch_idx)
            struct.pack_into("<h", d, po + 4, patch_idx[pname])
        new_defs.append(bytes(d))
    hdr = struct.pack("<i", len(new_defs))
    off = 4 + 4 * len(new_defs)
    table, body = b"", b""
    for d in new_defs:
        table += struct.pack("<i", off + len(body))
        body += d
    tex1 = hdr + table + body
    pnames_b = struct.pack("<i", len(patch_idx)) + b"".join(p.encode().ljust(8, b"\0") for p in patch_idx)
    if report:
        print("textures: %d of %d, patches: %d of %d" % (len(kept), len(order), len(patch_idx), npn))
    return tex1, pnames_b, set(patch_idx), flats, flat_ranges


def palette_lut(playpal):
    """RGB555 -> nearest index in palette 0. Must match nearest() in engine/lens/lens_hud.c."""
    pal = [tuple(playpal[i * 3:i * 3 + 3]) for i in range(256)]
    out = bytearray(32768)
    for r in range(32):
        for g in range(32):
            for b in range(32):
                R, G, B = r * 8 + 4, g * 8 + 4, b * 8 + 4
                best, bd = 0, 1 << 30
                for i, (pr, pg, pb) in enumerate(pal):
                    d = 3 * (pr - R) ** 2 + 4 * (pg - G) ** 2 + 2 * (pb - B) ** 2
                    if d < bd:
                        bd, best = d, i
                out[(r << 10) | (g << 5) | b] = best
    return bytes(out)


def strip(inp, outp, report=False):
    wad = Wad.load(inp)
    sprites, sounds = reachable(wad, report)
    tex1, pnames_b, patches, flats, flat_ranges = texture_subset(wad, report)

    names = [n for n, _ in wad.lumps]
    fs, fe = names.index("F_START"), names.index("F_END")
    flat_order = names[fs + 1:fe]
    for start, end in flat_ranges:
        if start in flat_order and end in flat_order:
            a, b = flat_order.index(start), flat_order.index(end)
            rng = flat_order[a:b + 1]
            if flats & set(rng):
                flats |= set(rng)

    out = []
    section = None
    mi = wad.index(MAP)
    map_slice = set(range(mi, mi + 1 + len(MAP_LUMPS)))
    for i, (n, d) in enumerate(wad.lumps):
        if n in ("S_START", "SS_START", "F_START", "FF_START"):
            section = n[0]
            if n in ("S_START", "F_START"):
                out.append((n, b""))
            continue
        if n in ("S_END", "SS_END", "F_END", "FF_END"):
            if n in ("S_END", "F_END"):
                out.append((n, b""))
            section = None
            continue
        if section == "S":
            if n[:4] in sprites:
                out.append((n, d))
        elif section == "F":
            if n in flats:
                out.append((n, d))
        elif i in map_slice:
            out.append((n, d))
        elif n == "TEXTURE1":
            out.append((n, tex1))
        elif n == "TEXTURE2":
            continue  # merged into TEXTURE1
        elif n == "PNAMES":
            out.append((n, pnames_b))
        elif n in patches or n in sounds or n in ALWAYS_KEEP_EXACT or n.startswith(ALWAYS_KEEP_PREFIX):
            out.append((n, d))
    present = {n for n, _ in out}
    missing = sorted(snd for snd in sounds if snd not in present and wad.index(snd) >= 0)
    absent = sorted(snd for snd in sounds if wad.index(snd) < 0)
    assert not missing, "sound lumps dropped: %s" % missing
    if report and absent:
        print("note: referenced sounds not in source WAD (engine never plays them):", absent)
    out.append(("LENSLUT", palette_lut(wad.get("PLAYPAL"))))
    Wad(b"IWAD", out).save(outp)
    size = os.path.getsize(outp)
    if report:
        print("lumps: %d of %d, size: %d bytes (%.2f MB)" % (len(out), len(wad.lumps), size, size / 1e6))
    return size


if __name__ == "__main__":
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    strip(a[0], a[1], report="--report" in sys.argv)
