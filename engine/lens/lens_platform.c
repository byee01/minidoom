// minidoom: doomgeneric platform layer for Snapchat Lens (JS host, no FS, no clock).
// GPLv2, part of the doomgeneric fork.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_loop.h"
#include "d_player.h"
#include "g_game.h"
#include "p_local.h"
#include "r_main.h"
#include "i_video.h"
#include "w_file.h"
#include "z_zone.h"
#include "lens_state.h"

#define LENS_EXPORT __attribute__((used, visibility("default")))

// ---------------------------------------------------------------- shared state
int lens_skip_display = 0;
static int32_t lens_state[LS_COUNT];
static uint8_t lens_palette[256 * 4];          // RGBA, A=255
static uint32_t lens_tics = 0;                 // virtual clock, in tics

extern int st_faceindex;
extern byte *I_VideoBuffer;
extern int screenblocks, detailLevel, showMessages, mouseSensitivity;
extern int sfxVolume, musicVolume, snd_channels, usegamma, show_endoom;

// ---------------------------------------------------------------- key ring
#define KEYRING 64
static uint16_t keyring[KEYRING];
static unsigned key_w = 0, key_r = 0;

LENS_EXPORT void lens_post_key(int pressed, int key)
{
    unsigned next = (key_w + 1) % KEYRING;
    if (next == key_r)
        return;  // full: drop (64 pending transitions means JS stopped ticking)
    keyring[key_w] = (uint16_t)((pressed ? 0x100 : 0) | (key & 0xff));
    key_w = next;
}

// Tap = press now, release after the next tic. Doom polls gamekeydown[] when it builds the
// tic command (weapon select, use), so a press and release inside one tic is never seen.
#define MAX_TAPS 8
static int pending_up[MAX_TAPS];
static int n_pending_up = 0;
LENS_EXPORT void lens_tap_key(int key)
{
    lens_post_key(1, key);
    if (n_pending_up < MAX_TAPS)
        pending_up[n_pending_up++] = key;
    else
        lens_post_key(0, key);
}

LENS_EXPORT void lens_post_mouse(int dx)
{
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ev_mouse;
    ev.data1 = 0;      // no buttons
    ev.data2 = dx;     // turn
    ev.data3 = 0;      // no forward/back from mouse
    D_PostEvent(&ev);
}

// ---------------------------------------------------------------- DG_* surface
void DG_Init(void) {}

void DG_DrawFrame(void)
{
    lens_state[LS_FRAME_COUNTER]++;
}

void DG_SleepMs(uint32_t ms) { (void)ms; }

uint32_t DG_GetTicksMs(void)
{
    // Exact: tic n starts at floor(n*1000/35) ms, so I_GetTime() == n.
    return (uint32_t)(((uint64_t)lens_tics * 1000 + 34) / TICRATE);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (key_r == key_w)
        return 0;
    uint16_t v = keyring[key_r];
    key_r = (key_r + 1) % KEYRING;
    *pressed = (v >> 8) & 1;
    *key = (unsigned char)(v & 0xff);
    return 1;
}

void DG_SetWindowTitle(const char *title) { (void)title; }

// ---------------------------------------------------------------- hooks
extern const byte gammatable[5][256];   // i_video.c

void lens_on_palette(const byte *raw)
{
    const byte *g = gammatable[usegamma];
    for (int i = 0; i < 256; i++)
    {
        lens_palette[i * 4 + 0] = g[raw[i * 3 + 0]];
        lens_palette[i * 4 + 1] = g[raw[i * 3 + 1]];
        lens_palette[i * 4 + 2] = g[raw[i * 3 + 2]];
        lens_palette[i * 4 + 3] = 255;
    }
    lens_state[LS_PALETTE_DIRTY] = 1;
    extern void lens_truecolor_palette(const uint8_t *rgba_pal);
    lens_truecolor_palette(lens_palette);
}

void lens_on_level_done(int secret)
{
    lens_state[LS_LEVEL_DONE] = 1;
    lens_state[LS_SECRET_EXIT] = secret;
}

// ---------------------------------------------------------------- memory WAD
#define LENS_WAD_NAME "minidoom.wad"
static byte *wad_buf = NULL;
static unsigned wad_len = 0;

int lens_is_memory_wad(const char *path)
{
    if (!path || !wad_buf)
        return 0;
    size_t n = strlen(path), m = strlen(LENS_WAD_NAME);
    return n >= m && strcmp(path + n - m, LENS_WAD_NAME) == 0;
}

static wad_file_t *Mem_OpenFile(char *path);
static void Mem_CloseFile(wad_file_t *wad) { (void)wad; }

static size_t Mem_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t len)
{
    (void)wad;
    if (offset >= wad_len)
        return 0;
    if (len > wad_len - offset)
        len = wad_len - offset;
    memcpy(buffer, wad_buf + offset, len);
    return len;
}

wad_file_class_t lens_memory_wad_file = { Mem_OpenFile, Mem_CloseFile, Mem_Read };
static wad_file_t mem_wad;

static wad_file_t *Mem_OpenFile(char *path)
{
    if (!lens_is_memory_wad(path))
        return NULL;
    mem_wad.file_class = &lens_memory_wad_file;
    mem_wad.mapped = wad_buf;     // lumps are served in place, no zone copies
    mem_wad.length = wad_len;
    return &mem_wad;
}

LENS_EXPORT byte *lens_wad_alloc(unsigned size)
{
    wad_buf = malloc(size);
    wad_len = wad_buf ? size : 0;
    return wad_buf;
}

// ---------------------------------------------------------------- lifecycle
LENS_EXPORT void lens_create(void)
{
    static char *argv[] = {
        "minidoom", "-iwad", LENS_WAD_NAME, "-warp", "1", "1",
        "-skill", "3", "-nomusic", "-mb", "8", NULL
    };
    // Compiled-in config; M_LoadDefaults finds no file and keeps these.
    screenblocks = 11;
    detailLevel = 0;
    showMessages = 1;
    mouseSensitivity = 5;
    sfxVolume = 15;
    musicVolume = 0;
    snd_channels = 8;
    usegamma = 0;
    show_endoom = 0;
    singletics = true;   // one doomgeneric_Tick == one game tic; JS owns time

    doomgeneric_Create(11, argv);
}

static void update_state(void)
{
    player_t *p = &players[consoleplayer];
    int32_t *s = lens_state;
    s[LS_HEALTH] = p->health;
    s[LS_ARMOR] = p->armorpoints;
    s[LS_ARMORTYPE] = p->armortype;
    for (int i = 0; i < 4; i++)
    {
        s[LS_AMMO + i] = p->ammo[i];
        s[LS_MAXAMMO + i] = p->maxammo[i];
    }
    s[LS_READYWEAPON] = p->readyweapon;
    s[LS_PENDINGWEAPON] = p->pendingweapon;
    for (int i = 0; i < 9; i++)
        s[LS_WEAPONOWNED + i] = p->weaponowned[i];
    for (int i = 0; i < 6; i++)
        s[LS_CARDS + i] = p->cards[i];
    s[LS_FACEINDEX] = st_faceindex;
    s[LS_DAMAGECOUNT] = p->damagecount;
    s[LS_BONUSCOUNT] = p->bonuscount;
    s[LS_DEAD] = p->playerstate == PST_DEAD;
    s[LS_GODMODE] = (p->cheats & CF_GODMODE) || p->powers[pw_invulnerability];
    s[LS_KILLS] = p->killcount;
    s[LS_TOTALKILLS] = totalkills;
    s[LS_ITEMS] = p->itemcount;
    s[LS_TOTALITEMS] = totalitems;
    s[LS_SECRETS] = p->secretcount;
    s[LS_TOTALSECRET] = totalsecret;
    s[LS_LEVELTIME] = leveltime;
    s[LS_GAMETIC] = gametic;
    s[LS_BACKPACK] = p->backpack;
    if (p->mo)
    {
        s[LS_PX] = p->mo->x >> FRACBITS;
        s[LS_PY] = p->mo->y >> FRACBITS;
        s[LS_ANGLE] = (int32_t)(((uint64_t)p->mo->angle * 360) >> 32);   // degrees
    }
}

// Run exactly one game tic. display=0 skips rendering (catch-up tics).
LENS_EXPORT void lens_tick(int display)
{
    if (lens_state[LS_LEVEL_DONE])
        return;
    lens_tics++;
    lens_skip_display = !display;
    doomgeneric_Tick();
    lens_skip_display = 0;
    for (int i = 0; i < n_pending_up; i++)
        lens_post_key(0, pending_up[i]);
    n_pending_up = 0;
    update_state();
    extern void lens_assist_update(int32_t *s);
    lens_assist_update(lens_state);
}

LENS_EXPORT void lens_restart(void)
{
    lens_state[LS_LEVEL_DONE] = 0;
    lens_state[LS_SECRET_EXIT] = 0;
    extern void lens_assist_reset(void);
    lens_assist_reset();
    G_DeferedInitNew(sk_medium, 1, 1);
}

// Cheat entry for test scripts and the god-mode face (types the keys).
LENS_EXPORT void lens_type(const char *text)
{
    for (; *text; text++)
    {
        lens_post_key(1, (unsigned char)*text);
        lens_post_key(0, (unsigned char)*text);
    }
}

LENS_EXPORT int32_t *lens_state_ptr(void) { return lens_state; }
LENS_EXPORT uint8_t *lens_palette_ptr(void) { return lens_palette; }
LENS_EXPORT byte *lens_framebuffer_ptr(void) { return I_VideoBuffer; }

// Runtime quality control: blocks 3..11 (11 = full 320x200), detail 0 high / 1 low.
LENS_EXPORT void lens_set_view(int blocks, int detail)
{
    extern void R_SetViewSize(int blocks, int detail);
    if (blocks < 3) blocks = 3;
    if (blocks > 11) blocks = 11;
    screenblocks = blocks;
    detailLevel = detail ? 1 : 0;
    R_SetViewSize(screenblocks, detailLevel);
}

// ---------------------------------------------------------------- test hooks
// Used by tests/*.txt and the debug-build autotest to reach states that are slow to reach by
// play (pain tiers, death, level exit). Release builds keep them; they do nothing unless called.
LENS_EXPORT void lens_test_damage(int amount)
{
    extern void P_DamageMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, int damage);
    player_t *p = &players[consoleplayer];
    if (p->mo)
        P_DamageMobj(p->mo, NULL, NULL, amount);
}

LENS_EXPORT void lens_test_exit(void)
{
    G_ExitLevel();
}

// Place the player (tests): map units, angle in degrees.
LENS_EXPORT void lens_test_place(int x, int y, int deg)
{
    player_t *p = &players[consoleplayer];
    if (!p->mo) return;
    P_UnsetThingPosition(p->mo);
    p->mo->x = x << FRACBITS;
    p->mo->y = y << FRACBITS;
    P_SetThingPosition(p->mo);
    p->mo->z = p->mo->floorz = p->mo->subsector->sector->floorheight;
    p->mo->angle = (angle_t)(((uint64_t)deg << 32) / 360);
}

// Ceiling height of the sector at (x, y), in map units (tests: did a door open?).
LENS_EXPORT int lens_test_ceiling(int x, int y)
{
    return R_PointInSubsector(x << FRACBITS, y << FRACBITS)->sector->ceilingheight >> FRACBITS;
}

// Spawn a zombieman `dist` units straight ahead (tests: deterministic auto-fire target).
LENS_EXPORT void lens_test_spawn_enemy(int dist)
{
    player_t *p = &players[consoleplayer];
    if (!p->mo) return;
    int a = p->mo->angle >> ANGLETOFINESHIFT;
    mobj_t *m = P_SpawnMobj(p->mo->x + dist * finecosine[a], p->mo->y + dist * finesine[a], ONFLOORZ, MT_POSSESSED);
    m->angle = p->mo->angle + ANG180;
    totalkills++;
}

// ---------------------------------------------------------------- analog twin sticks
// fwd/side/turn in -1000..1000 (stick deflection). Movement tops out at ~75% of Doom's walk speed
// (forwardmove 25 / sidemove 24): full walk speed felt too fast on a phone. Turning mixes a
// linear term (fine aim) with a quadratic one (fast sweeps), up to ~150 degrees/s.
#define LENS_MAX_FORWARD 19
#define LENS_MAX_SIDE 17
#define LENS_MAX_TURN 780
int lens_analog_forward = 0, lens_analog_side = 0, lens_analog_turn = 0;

static int clamp1000(int v) { return v > 1000 ? 1000 : (v < -1000 ? -1000 : v); }

LENS_EXPORT void lens_set_analog(int fwd, int side, int turn)
{
    fwd = clamp1000(fwd); side = clamp1000(side); turn = clamp1000(turn);
    lens_analog_forward = fwd * LENS_MAX_FORWARD / 1000;
    lens_analog_side = side * LENS_MAX_SIDE / 1000;
    int a = turn < 0 ? -turn : turn;
    int mag = (int)((long long)LENS_MAX_TURN * (350LL * a + 650LL * a * a / 1000) / 1000000);
    lens_analog_turn = turn < 0 ? -mag : mag;
}
