// minidoom: portrait HUD + live face, drawn in Doom's 8-bit space and presented
// together with the 3D view as one RGBA image (palette flashes tint everything).
// GPLv2, part of the doomgeneric fork.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "i_swap.h"
#include "v_patch.h"
#include "w_wad.h"
#include "z_zone.h"
#include "lens_state.h"
#include "lens_hud.h"

#define LENS_EXPORT __attribute__((used, visibility("default")))

extern byte *I_VideoBuffer;
int32_t *lens_state_ptr(void);
uint8_t *lens_palette_ptr(void);

static byte hud[HUD_W * HUD_H] __attribute__((aligned(4)));
// Presented image, top-down: rows 0..199 are the 3D view (written directly by the truecolor
// drawers in r_draw.c through lens_view32), rows 200..295 the HUD. The Lens Image flips Y.
static uint32_t rgba[OUT_W * OUT_H];
uint32_t *lens_view32 = rgba;
uint32_t lens_pal32[256];
uint32_t lens_colormaps32[34 * 256];
static int hud_pal_dirty = 1;
#define pal32 lens_pal32
static byte face_in[FACE_W * FACE_H * 4];     // RGBA from the camera, written by JS
static int face_live = 0;                      // 1: camera face present this frame
static int face_dirty = 1;                     // new camera frame or live toggle since last HUD draw
static byte face_frozen[FACE_W * FACE_H];      // last live face (for the dead state)
static byte facec[FACE_W * FACE_H];            // face canvas, palette indices
static uint32_t face_rgba[FACE_W * FACE_H];     // face canvas, presented (top-down)
static int face_pal_dirty = 1;
static byte lut[32 * 32 * 32];                 // RGB555 -> palette index
static int lut_ready = 0;
static const byte *colormap;                   // COLORMAP lump (34 maps x 256)
static byte c_bg, c_panel, c_light, c_shadow, c_black, c_red, c_dkred, c_gold, c_white, c_teeth;

// ---------------------------------------------------------------- palette helpers
static int nearest(const byte *pal, int r, int g, int b)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 256; i++)
    {
        int dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
        // perceptual-ish weights
        int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void build_lut(void)
{
    const byte *pal = W_CacheLumpName("PLAYPAL", PU_STATIC);   // base palette 0
    // RGB555 -> palette index table, precomputed by tools/wadstrip.py (a 32K-entry nearest-colour
    // search costs ~6 s in the Lens interpreter).
    int l = W_CheckNumForName("LENSLUT");
    if (l >= 0 && W_LumpLength(l) == 32768)
        memcpy(lut, W_CacheLumpNum(l, PU_STATIC), 32768);
    else
        for (int r = 0; r < 32; r++)
            for (int g = 0; g < 32; g++)
                for (int b = 0; b < 32; b++)
                    lut[(r << 10) | (g << 5) | b] = nearest(pal, r * 8 + 4, g * 8 + 4, b * 8 + 4);
    c_black = nearest(pal, 0, 0, 0);
    c_bg = nearest(pal, 28, 28, 28);
    c_panel = nearest(pal, 52, 48, 44);
    c_light = nearest(pal, 104, 96, 88);
    c_shadow = nearest(pal, 16, 14, 12);
    c_red = nearest(pal, 200, 0, 0);
    c_dkred = nearest(pal, 110, 0, 0);
    c_gold = nearest(pal, 255, 200, 40);
    c_white = nearest(pal, 240, 240, 240);
    c_teeth = nearest(pal, 230, 220, 190);
    colormap = W_CacheLumpName("COLORMAP", PU_STATIC);
    lut_ready = 1;
}

static inline byte quant(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return lut[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
}

// ---------------------------------------------------------------- drawing
static void fill(int x, int y, int w, int h, byte c)
{
    for (int j = y; j < y + h; j++)
        if (j >= 0 && j < HUD_H)
            for (int i = x; i < x + w; i++)
                if (i >= 0 && i < HUD_W)
                    hud[j * HUD_W + i] = c;
}

// Panel texture: a text-free patch from inside the ammo panel of Freedoom's STBAR
// (the mottled recess), tiled with a half-tile offset per row to hide seams.
#define TEX_X 4
#define TEX_Y 3
#define TEX_W 36
#define TEX_H 16
static byte stbar[32][320];
static int stbar_ready = 0;
static void load_stbar(void)
{
    int lump = W_CheckNumForName("STBAR");
    memset(stbar, c_panel, sizeof(stbar));
    if (lump >= 0)
    {
        const patch_t *p = W_CacheLumpNum(lump, PU_CACHE);
        int w = SHORT(p->width);
        for (int col = 0; col < w && col < 320; col++)
        {
            const column_t *c = (const column_t *)((const byte *)p + LONG(p->columnofs[col]));
            while (c->topdelta != 0xff)
            {
                const byte *src = (const byte *)c + 3;
                for (int k = 0; k < c->length && c->topdelta + k < 32; k++)
                    stbar[c->topdelta + k][col] = src[k];
                c = (const column_t *)((const byte *)c + c->length + 4);
            }
        }
    }
    stbar_ready = 1;
}
static inline byte panel_tex(int x, int y)
{
    int row = y / TEX_H;
    int tx = (x + (row & 1) * (TEX_W / 2)) % TEX_W, ty = y % TEX_H;
    return stbar[TEX_Y + ty][TEX_X + tx];
}
static void texfill(int x, int y, int w, int h)
{
    if (!stbar_ready) load_stbar();
    for (int j = y; j < y + h; j++)
        if (j >= 0 && j < HUD_H)
            for (int i = x; i < x + w; i++)
                if (i >= 0 && i < HUD_W)
                    hud[j * HUD_W + i] = panel_tex(i, j);
}

static void bevel(int x, int y, int w, int h, int inset)
{
    texfill(x, y, w, h);
    fill(x, y, w, 1, inset ? c_shadow : c_light);
    fill(x, y, 1, h, inset ? c_shadow : c_light);
    fill(x, y + h - 1, w, 1, inset ? c_light : c_shadow);
    fill(x + w - 1, y, 1, h, inset ? c_light : c_shadow);
}

// Draw a Doom patch with integer scale, ignoring its offsets. remap may be NULL.
static int draw_patch_to(byte *buf, int bw, int bh, int x, int y, const char *name, int scale, const byte *remap)
{
    int lump = W_CheckNumForName((char *)name);
    if (lump < 0)
        return 0;
    const patch_t *p = W_CacheLumpNum(lump, PU_CACHE);
    int w = SHORT(p->width);
    for (int col = 0; col < w; col++)
    {
        const column_t *c = (const column_t *)((const byte *)p + LONG(p->columnofs[col]));
        while (c->topdelta != 0xff)
        {
            const byte *src = (const byte *)c + 3;
            for (int k = 0; k < c->length; k++)
            {
                byte v = remap ? remap[src[k]] : src[k];
                int py = y + (c->topdelta + k) * scale, px = x + col * scale;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                    {
                        int xx = px + sx, yy = py + sy;
                        if (xx >= 0 && xx < bw && yy >= 0 && yy < bh)
                            buf[yy * bw + xx] = v;
                    }
            }
            c = (const column_t *)((const byte *)c + c->length + 4);
        }
    }
    return w * scale;
}

static int draw_patch(int x, int y, const char *name, int scale, const byte *remap)
{
    return draw_patch_to(hud, HUD_W, HUD_H, x, y, name, scale, remap);
}

static int patch_w(const char *name)
{
    int lump = W_CheckNumForName((char *)name);
    return lump < 0 ? 0 : SHORT(((const patch_t *)W_CacheLumpNum(lump, PU_CACHE))->width);
}

static void text(int x, int y, const char *s, int scale, const byte *remap)
{
    char name[9];
    for (; *s; s++)
    {
        int ch = *s;
        if (ch == ' ') { x += 4 * scale; continue; }
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        snprintf(name, sizeof(name), "STCFN%.3d", ch);
        int w = draw_patch(x, y, name, scale, remap);
        x += (w ? w : 4 * scale);
    }
}

// Right-aligned big number (STTNUM), with optional percent sign.
static void bignum(int right, int y, int v, int percent, const byte *remap)
{
    char buf[8], name[9];
    int x = right;
    if (percent)
    {
        x -= patch_w("STTPRCNT");
        draw_patch(x, y, "STTPRCNT", 1, remap);
    }
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    snprintf(buf, sizeof(buf), "%d", v);
    for (int i = (int)strlen(buf) - 1; i >= 0; i--)
    {
        snprintf(name, sizeof(name), "STTNUM%c", buf[i]);
        x -= patch_w(name);
        draw_patch(x, y, name, 1, remap);
    }
}

// ---------------------------------------------------------------- face
// Face state from st_faceindex (st_stuff.c layout: 5 pain tiers x 8, then god, dead).
#define FS_STRIDE 8
#define FS_GOD 40
#define FS_DEAD 41

// 24x29 overlays: '.' = none, 'r' = red, 'd' = dark red, 'w' = white, 'k' = black, 'g' = gold, 't' = teeth
static const char *blood_masks[5] = {
    NULL,
    // tier 1: small forehead cut
    "........................"
    "........................"
    "........................"
    "........................"
    "..........d............."
    "..........r............."
    "..........r.............",
    // tier 2: + cheek scrape
    "........................"
    "........................"
    "........................"
    "........................"
    "..........d............."
    ".........rr............."
    "..........r............."
    "..........r............."
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "..................d....."
    ".................rr....."
    "..................r.....",
    // tier 3: + nose bleed, more forehead
    "........................"
    "........................"
    "......d................."
    "......r...d............."
    ".....rr..rr............."
    "......r..rr............."
    "..........r............."
    "..........r............."
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "..........d.d..........."
    "..........r.r......d...."
    "..........r.r.....rr...."
    "...........r......rr...."
    "...................r....",
    // tier 4: heavy
    "........................"
    "........d..............."
    ".....d..r..d............"
    ".....rr.rr.rr..........."
    "....rrr.rr.rr...d......."
    ".....rr..r.rr..rr......."
    "......r..r..r...r......."
    "......r.....r..........."
    "......d................."
    "........................"
    "........................"
    ".d......................"
    ".r......................"
    ".rr....................."
    ".rr......d.d............"
    "..r......r.r......d....."
    "..r......r.r.....rr....."
    "..d.......r.r....rrr...."
    "..........r.r.....rr...."
    "...........r.......r...."
    "...........r.......d....",
};

static const char *grin_mask =
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "......kkkkkkkkkkkk......"
    ".....ktttttttttttttk...."
    "......kktktktktktkk....."
    "........kkkkkkkkk.......";

static const char *rampage_mask =
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "....kkk..........kkk...."
    ".....kkkk......kkkk....."
    ".......kk......kk.......";

static const char *god_eyes =
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "......ggg......ggg......"
    ".....gwwwg....gwwwg....."
    "......ggg......ggg......";

static const char *dead_eyes =
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    "........................"
    ".....k...k....k...k....."
    "......k.k......k.k......"
    ".......k........k......."
    "......k.k......k.k......"
    ".....k...k....k...k.....";

static void apply_mask(byte *face, const char *m, int dx)
{
    if (!m)
        return;
    int len = (int)strlen(m);
    for (int y = 0; y < FACE_H; y++)
        for (int x = 0; x < FACE_W; x++)
        {
            int mx = (x - dx) * MASK_W / FACE_W, my = y * MASK_H / FACE_H;
            if (x - dx < 0 || mx >= MASK_W)
                continue;
            int k = my * MASK_W + mx;
            if (k >= len)
                continue;
            byte c;
            switch (m[k])
            {
                case 'r': c = c_red; break;
                case 'd': c = c_dkred; break;
                case 'w': c = c_white; break;
                case 'k': c = c_black; break;
                case 'g': c = c_gold; break;
                case 't': c = c_teeth; break;
                default: continue;
            }
            face[y * FACE_W + x] = c;
        }
}

// Build the face canvas (palette indices) from the camera frame for the given face index.
static void build_live_face(byte *out, int fi)
{
    int tier = fi < FS_GOD ? fi / FS_STRIDE : 0;
    int sub = fi < FS_GOD ? fi % FS_STRIDE : -1;
    int dx = 0, red = 0, gold = 0, gray = 0;
    // Look direction: straight faces 0/1/2 glance, turn faces 3 (right) / 4 (left) turn harder.
    if (sub >= 0 && sub <= 2) dx = (sub - 1) * 2;
    if (sub == 3) dx = 7;
    if (sub == 4) dx = -7;
    if (sub == 5) red = 90;          // ouch
    if (sub == 6) gold = 30;          // evil grin: gold rim; god mode only gets gold eyes
    if (fi == FS_DEAD) { gray = 1; red = 60; }
    // pain tiers get paler/darker
    int shade = tier * 10;

    if (fi == FS_DEAD)
    {
        memcpy(out, face_frozen, sizeof(face_frozen));
    }
    else
    {
        // Auto-levels: a face close to the camera fills the crop with bright, smooth skin. The old
        // fixed contrast boost clipped it to white, and the few skin ramps in PLAYPAL turned the
        // gradients into hard bands. Map the frame's 3rd..97th luma percentiles into a range
        // that keeps the highlights, smoothed over frames so exposure doesn't flicker.
        int hist[64] = { 0 };
        for (int i = 0; i < FACE_W * FACE_H; i++)
        {
            const byte *p = face_in + i * 4;
            hist[(p[0] * 3 + p[1] * 5 + p[2] * 2) / 10 >> 2]++;
        }
        int n = FACE_W * FACE_H, acc = 0, lo = -1, hi = 63;
        for (int k = 0; k < 64; k++)
        {
            acc += hist[k];
            if (lo < 0 && acc >= n * 3 / 100) lo = k;
            if (acc >= n * 97 / 100) { hi = k; break; }
        }
        if (lo < 0) lo = 0;
        lo = lo * 4; hi = hi * 4 + 3;
        if (hi - lo < 64) { int m = (hi + lo) / 2; lo = m - 32; hi = m + 32; }   // flat frame: cap the gain
        static int lv_lo = -1, lv_hi = -1;                                      // 8.8 fixed, smoothed
        if (lv_lo < 0) { lv_lo = lo << 8; lv_hi = hi << 8; }
        lv_lo += ((lo << 8) - lv_lo) / 4;
        lv_hi += ((hi << 8) - lv_hi) / 4;
        const int out_lo = 20, out_hi = 215;
        int gain = ((out_hi - out_lo) << 16) / ((lv_hi - lv_lo) >> 8 > 0 ? (lv_hi - lv_lo) >> 8 : 1);  // 16.16
        if (gain > (2 << 16)) gain = 2 << 16;
        int base = lv_lo >> 8;
        // 4x4 ordered dither (+-9 per channel): breaks the remaining palette steps into
        // Doom-style speckle instead of bands.
        static const signed char bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
        for (int y = 0; y < FACE_H; y++)
            for (int x = 0; x < FACE_W; x++)
            {
                int sx = x - dx;
                if (sx < 0) sx = 0;
                if (sx >= FACE_W) sx = FACE_W - 1;
                // texture rows from JS are bottom-up (getPixels region extends upward)
                const byte *p = face_in + ((FACE_H - 1 - y) * FACE_W + sx) * 4;
                int d = ((bayer[(y & 3) * 4 + (x & 3)] * 2 - 15) * 5) / 8 - shade;
                int r = out_lo + (((p[0] - base) * gain) >> 16) + d;
                int g = out_lo + (((p[1] - base) * gain) >> 16) + d;
                int b = out_lo + (((p[2] - base) * gain) >> 16) + d;
                out[y * FACE_W + x] = quant(r, g, b);
            }
        memcpy(face_frozen, out, sizeof(face_frozen));
    }
    if (red || gold || gray)
    {
        const byte *pal = W_CacheLumpName("PLAYPAL", PU_CACHE);
        for (int i = 0; i < FACE_W * FACE_H; i++)
        {
            int r = pal[out[i] * 3], g = pal[out[i] * 3 + 1], b = pal[out[i] * 3 + 2];
            if (gray) { int l = (r * 3 + g * 5 + b * 2) / 10; r = g = b = l; }
            r += red + gold; g += gold / 2 - red / 2; b -= red / 2 + gold;
            out[i] = quant(r, g, b);
        }
    }
    apply_mask(out, blood_masks[tier > 4 ? 4 : tier], dx);
    if (fi == FS_DEAD) apply_mask(out, blood_masks[4], 0);
    if (sub == 6) apply_mask(out, grin_mask, 0);
    if (sub == 7) apply_mask(out, rampage_mask, 0);
    if (fi == FS_GOD) apply_mask(out, god_eyes, 0);
    if (fi == FS_DEAD) apply_mask(out, dead_eyes, 0);
}

// Name of the Doom face sprite for st_faceindex (fallback when no camera face).
static void face_lump(int fi, char *name)
{
    if (fi == FS_GOD) { strcpy(name, "STFGOD0"); return; }
    if (fi == FS_DEAD) { strcpy(name, "STFDEAD0"); return; }
    int tier = fi / FS_STRIDE, sub = fi % FS_STRIDE;
    if (sub <= 2) snprintf(name, 9, "STFST%d%d", tier, sub);
    else if (sub == 3) snprintf(name, 9, "STFTR%d0", tier);
    else if (sub == 4) snprintf(name, 9, "STFTL%d0", tier);
    else if (sub == 5) snprintf(name, 9, "STFOUCH%d", tier);
    else if (sub == 6) snprintf(name, 9, "STFEVL%d", tier);
    else snprintf(name, 9, "STFKILL%d", tier);
}

// ---------------------------------------------------------------- HUD layout
// 320x40 strip: AMMO | arms 2-7 (tappable) | HEALTH | ARMOR + keys.
static const int arms_x0 = 72, arms_y0 = 3, arms_cw = 32, arms_ch = 17;

static void draw_hud(const int32_t *s)
{
    const byte *dim = colormap + 256 * 20;     // darker light level for unowned weapons
    texfill(0, 0, HUD_W, HUD_H);
    for (int i = 0; i < HUD_W * HUD_H; i++)
        hud[i] = colormap[256 * 12 + hud[i]];   // gaps between panels: darker stone

    bevel(2, 2, 66, HUD_H - 4, 0);
    text(7, 4, "AMMO", 1, NULL);
    int w = s[LS_READYWEAPON];
    // weapontype -> ammotype: fist, pistol, shotgun, chaingun, missile, plasma, bfg, chainsaw, ssg
    static const int ammo_type_of_weapon[9] = { -1, 0, 1, 0, 3, 2, 2, -1, 1 };
    int at = (w >= 0 && w < 9) ? ammo_type_of_weapon[w] : -1;
    if (at >= 0)
        bignum(64, 15, s[LS_AMMO + at], 0, NULL);
    else
        text(36, 18, "--", 1, NULL);

    for (int i = 0; i < 6; i++)
    {
        int num = i + 2;        // slots 2..7: pistol, shotgun, chaingun, rocket, plasma, bfg
        int cx = arms_x0 + (i % 3) * arms_cw, cy = arms_y0 + (i / 3) * arms_ch;
        int owned = s[LS_WEAPONOWNED + num - 1];
        bevel(cx, cy, arms_cw - 2, arms_ch - 1, 1);
        if (w == num - 1)
            fill(cx + 1, cy + 1, arms_cw - 4, arms_ch - 3, c_shadow);
        char name[9];
        snprintf(name, sizeof(name), "STYSNUM%d", num);
        // big digits don't fit 17 px; draw the yellow small digits at 2x (8x12)
        draw_patch(cx + (arms_cw - 2 - patch_w(name) * 2) / 2, cy + 2, name, 2, owned ? NULL : dim);
    }

    bevel(172, 2, 72, HUD_H - 4, 0);
    text(177, 4, "HEALTH", 1, NULL);
    bignum(240, 15, s[LS_HEALTH], 1, NULL);

    bevel(246, 2, 72, HUD_H - 4, 0);
    text(251, 4, "ARMOR", 1, NULL);
    bignum(314, 13, s[LS_ARMOR], 1, NULL);
    for (int k = 0; k < 3; k++)
    {
        int have = s[LS_CARDS + k] || s[LS_CARDS + k + 3];
        int skull = !s[LS_CARDS + k] && s[LS_CARDS + k + 3];
        if (have)
        {
            char name[9];
            snprintf(name, sizeof(name), "STKEYS%d", k + (skull ? 3 : 0));
            draw_patch(251 + k * 10, 30, name, 1, NULL);
        }
    }
}

static void draw_face(const int32_t *s)
{
    int fi = s[LS_FACEINDEX];
    memset(facec, c_black, sizeof(facec));
    if (face_live)
    {
        build_live_face(facec, fi);
    }
    else
    {
        char name[9];
        face_lump(fi, name);
        int lump = W_CheckNumForName(name);
        if (lump >= 0)
        {
            const patch_t *p = W_CacheLumpNum(lump, PU_CACHE);
            int pw = SHORT(p->width) * 2, ph = SHORT(p->height) * 2;
            draw_patch_to(facec, FACE_W, FACE_H, (FACE_W - pw) / 2, (FACE_H - ph) / 2, name, 2, NULL);
        }
    }
}

// ---------------------------------------------------------------- exports
LENS_EXPORT byte *lens_face_input_ptr(void) { return face_in; }
LENS_EXPORT void lens_face_set_live(int live) { face_live = live; face_dirty = 1; }
// JS calls this after writing a new camera frame into face_in.
LENS_EXPORT void lens_face_frame(void) { face_dirty = 1; }

// HUD content signature: redraw + re-expand the HUD only when something it shows changed.
static uint32_t hud_sig = 0;
static uint32_t hud_signature(const int32_t *s)
{
    uint32_t h = 2166136261u;
    const int fields[] = { LS_HEALTH, LS_ARMOR, LS_READYWEAPON };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) h = (h ^ (uint32_t)s[fields[i]]) * 16777619u;
    for (int i = 0; i < 4; i++) h = (h ^ (uint32_t)s[LS_AMMO + i]) * 16777619u;
    for (int i = 0; i < 9; i++) h = (h ^ (uint32_t)s[LS_WEAPONOWNED + i]) * 16777619u;
    for (int i = 0; i < 6; i++) h = (h ^ (uint32_t)s[LS_CARDS + i]) * 16777619u;
    return h | 1;
}

static void expand_hud(void)
{
    for (int y = 0; y < HUD_H; y++)
    {
        uint32_t *dst = rgba + (200 + y) * OUT_W;
        const uint32_t *s4 = (const uint32_t *)(hud + y * HUD_W);
        for (int x = 0; x < HUD_W; x += 4)
        {
            uint32_t q = *s4++;                                   // 4 indices per load
            dst[x] = pal32[q & 0xff];
            dst[x + 1] = pal32[(q >> 8) & 0xff];
            dst[x + 2] = pal32[(q >> 16) & 0xff];
            dst[x + 3] = pal32[q >> 24];
        }
    }
}

// Called from lens_on_palette (I_SetPalette): rebuild the truecolor tables once per palette change.
void lens_truecolor_palette(const uint8_t *rgba_pal)
{
    extern byte *colormaps;
    for (int i = 0; i < 256; i++)
        pal32[i] = (uint32_t)rgba_pal[i * 4] | ((uint32_t)rgba_pal[i * 4 + 1] << 8) |
                   ((uint32_t)rgba_pal[i * 4 + 2] << 16) | 0xff000000u;
    if (colormaps)
        for (int i = 0; i < 34 * 256; i++)
            lens_colormaps32[i] = pal32[colormaps[i]];
    hud_pal_dirty = 1;
}

static void expand_face(void)
{
    for (int i = 0; i < FACE_W * FACE_H; i++)
        face_rgba[i] = pal32[facec[i]];
}

// Compose the presented image: the 3D view is already in place; refresh the HUD strip and the
// face canvas only when what they show changed. LS_FACE_VERSION tells JS to re-upload the face.
static int last_faceindex = -1;
LENS_EXPORT uint32_t *lens_present(void)
{
    int32_t *s = lens_state_ptr();
    if (!lut_ready)
        build_lut();
    uint32_t sig = hud_signature(s);
    int static_changed = sig != hud_sig;
    int face_changed = face_dirty || s[LS_FACEINDEX] != last_faceindex;
    if (static_changed)
    {
        draw_hud(s);
        hud_sig = sig;
    }
    if (static_changed || hud_pal_dirty)
        expand_hud();
    if (face_changed)
    {
        draw_face(s);
        face_dirty = 0;
        last_faceindex = s[LS_FACEINDEX];
    }
    if (face_changed || hud_pal_dirty)
    {
        expand_face();
        s[LS_FACE_VERSION]++;
    }
    hud_pal_dirty = 0;
    return rgba;
}

LENS_EXPORT uint32_t *lens_face_ptr(void) { return face_rgba; }

// ---------------------------------------------------------------- text overlay
// Title / end card / "TAP TO RETRY", drawn in Doom's HUD font (STCFN) into a transparent
// 320x200 layer the Lens stacks exactly over the 3D view (so it crops the same way).
// Lines are centred and word-wrapped to the visible width. A line starting with '^' is drawn
// at 2x, '^^' at 3x. Colours use the base palette (no damage tint).
static uint32_t overlay[320 * 200];

static int glyph_w(int ch)
{
    char name[9];
    if (ch == ' ') return 4;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 33 || ch > 95) return 4;
    snprintf(name, sizeof(name), "STCFN%.3d", ch);
    int w = patch_w(name);
    return w ? w : 4;
}

static int text_w(const char *s, int n, int scale)
{
    int w = 0;
    for (int i = 0; i < n; i++) w += glyph_w((unsigned char)s[i]);
    return w * scale;
}

static void overlay_glyph(int x, int y, int ch, int scale, const byte *pal)
{
    char name[9];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 33 || ch > 95) return;
    snprintf(name, sizeof(name), "STCFN%.3d", ch);
    int lump = W_CheckNumForName(name);
    if (lump < 0) return;
    const patch_t *p = W_CacheLumpNum(lump, PU_CACHE);
    for (int col = 0; col < SHORT(p->width); col++)
    {
        const column_t *c = (const column_t *)((const byte *)p + LONG(p->columnofs[col]));
        while (c->topdelta != 0xff)
        {
            const byte *src = (const byte *)c + 3;
            for (int k = 0; k < c->length; k++)
            {
                byte v = src[k];
                uint32_t rgba = pal[v * 3] | (pal[v * 3 + 1] << 8) | (pal[v * 3 + 2] << 16) | 0xff000000u;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                    {
                        int xx = x + col * scale + sx, yy = y + (c->topdelta + k) * scale + sy;
                        if (xx >= 0 && xx < 320 && yy >= 0 && yy < 200)
                            overlay[yy * 320 + xx] = rgba;
                    }
            }
            c = (const column_t *)((const byte *)c + c->length + 4);
        }
    }
}

// Lays out (draw=0) or draws (draw=1) the text; returns total height in pixels.
static int overlay_layout(const char *text, int visible_w, int y0, int draw, const byte *pal)
{
    int y = y0;
    const char *line = text;
    while (*line)
    {
        const char *end = strchr(line, '\n');
        int n = end ? (int)(end - line) : (int)strlen(line);
        int scale = 1;
        while (n > 0 && *line == '^') { scale++; line++; n--; }
        int lh = 9 * scale;
        if (n == 0) { y += lh; }
        // greedy word wrap within visible_w
        int start = 0;
        while (start < n)
        {
            int best = n;
            if (text_w(line + start, n - start, scale) > visible_w)
            {
                best = start;
                for (int i = start; i < n; i++)
                    if (line[i] == ' ' && text_w(line + start, i - start, scale) <= visible_w) best = i;
                if (best == start) best = n;            // single long word: let it overflow
            }
            int w = text_w(line + start, best - start, scale);
            if (draw)
            {
                int x = (320 - w) / 2;
                for (int i = start; i < best; i++)
                {
                    overlay_glyph(x, y, (unsigned char)line[i], scale, pal);
                    x += glyph_w((unsigned char)line[i]) * scale;
                }
            }
            y += lh;
            start = best;
            while (start < n && line[start] == ' ') start++;
        }
        line = end ? end + 1 : line + n;
    }
    return y - y0;
}

// text: lines separated by '\n' (NULL or "" clears). shade: 0..255 background darkness.
// visible_w: how many of the 320 columns are on screen (the Lens crops the sides).
LENS_EXPORT void lens_set_overlay(const char *text, int shade, int visible_w)
{
    if (!lut_ready) build_lut();
    int32_t *s = lens_state_ptr();
    uint32_t bg = text && *text ? ((uint32_t)shade << 24) : 0;     // premultiplied black
    for (int i = 0; i < 320 * 200; i++) overlay[i] = bg;
    if (text && *text)
    {
        const byte *pal = W_CacheLumpName("PLAYPAL", PU_CACHE);
        int vw = visible_w < 64 ? 64 : (visible_w > 320 ? 320 : visible_w);
        vw -= 12;                                                    // margin
        int h = overlay_layout(text, vw, 0, 0, pal);
        overlay_layout(text, vw, (200 - h) / 2, 1, pal);
    }
    s[LS_OVERLAY_VERSION]++;
}

LENS_EXPORT uint32_t *lens_overlay_ptr(void) { return overlay; }

// Screen background: the panel texture tiled over w x h Doom-scale pixels, base palette
// (no damage tint). Rendered once at boot; JS shows it with nearest filtering.
#define BG_MAX (256 * 560)
static uint32_t bg_rgba[BG_MAX];
LENS_EXPORT uint32_t *lens_background(int w, int h)
{
    if (w * h > BG_MAX) return NULL;
    if (!lut_ready) build_lut();
    if (!stbar_ready) load_stbar();
    const byte *pal = W_CacheLumpName("PLAYPAL", PU_CACHE);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            byte i = colormap[256 * 4 + panel_tex(x, y)];      // slightly darker than the HUD panels
            bg_rgba[y * w + x] = pal[i * 3] | (pal[i * 3 + 1] << 8) | (pal[i * 3 + 2] << 16) | 0xff000000u;
        }
    return bg_rgba;
}

// Bezel around the camera view, in the style of the HUD's AMMO/HEALTH panels: a raised panel
// (light top-left, shadow bottom-right, STBAR stone texture) with the window cut in b px from
// the edge and an inset edge (shadow top-left, light bottom-right). w x h Doom-scale pixels,
// base palette. The window itself is black (the face image covers it).
#define BEZEL_MAX (256 * 320)
static uint32_t bezel_rgba[BEZEL_MAX];
LENS_EXPORT uint32_t *lens_bezel(int w, int h, int b)
{
    if (w * h > BEZEL_MAX || w <= 2 * b + 2 || h <= 2 * b + 2) return NULL;
    if (!lut_ready) build_lut();
    if (!stbar_ready) load_stbar();
    const byte *pal = W_CacheLumpName("PLAYPAL", PU_CACHE);
    int ix0 = b, iy0 = b, ix1 = w - 1 - b, iy1 = h - 1 - b;     // window, inclusive
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            byte i;
            if (x > ix0 && x < ix1 && y > iy0 && y < iy1) i = c_black;
            else if (x == 0 || y == 0) i = c_light;                           // raised outer edge
            else if (x == w - 1 || y == h - 1) i = c_shadow;
            else if ((x == ix0 || y == iy0) && x >= ix0 && x <= ix1 && y >= iy0 && y <= iy1) i = c_shadow;   // inset
            else if ((x == ix1 || y == iy1) && x >= ix0 && x <= ix1 && y >= iy0 && y <= iy1) i = c_light;
            else i = panel_tex(x, y);
            bezel_rgba[y * w + x] = pal[i * 3] | (pal[i * 3 + 1] << 8) | (pal[i * 3 + 2] << 16) | 0xff000000u;
        }
    return bezel_rgba;
}

// Synthetic camera face: skin-toned oval with darker eyes/mouth, for face-path tests.
LENS_EXPORT void lens_test_fake_face(void)
{
    uint8_t *f = face_in;
    for (int y = 0; y < FACE_H; y++)
        for (int x = 0; x < FACE_W; x++)
        {
            float u = (x + 0.5f) / FACE_W, v = 1.0f - (y + 0.5f) / FACE_H;   // stored bottom-up
            float dx = (u - 0.5f) / 0.46f, dy = (v - 0.5f) / 0.48f;
            int in = dx * dx + dy * dy < 1.0f;
            int eye = v > 0.38f && v < 0.46f && ((u > 0.25f && u < 0.37f) || (u > 0.63f && u < 0.75f));
            int mouth = v > 0.72f && v < 0.77f && u > 0.33f && u < 0.67f;
            uint8_t *p = f + (y * FACE_W + x) * 4;
            if (!in) { p[0] = 30; p[1] = 40; p[2] = 60; }
            else if (eye || mouth) { p[0] = 50; p[1] = 30; p[2] = 25; }
            else { p[0] = 225; p[1] = 170; p[2] = 140; }
            p[3] = 255;
        }
}

// HUD hit test: returns weapon key ('2'..'7') or 0. Coordinates in HUD pixels.
LENS_EXPORT int lens_hud_hit(int x, int y)
{
    for (int i = 0; i < 6; i++)
    {
        int cx = arms_x0 + (i % 3) * arms_cw, cy = arms_y0 + (i / 3) * arms_ch;
        if (x >= cx && x < cx + arms_cw && y >= cy && y < cy + arms_ch)
            return '2' + i;
    }
    return 0;
}
