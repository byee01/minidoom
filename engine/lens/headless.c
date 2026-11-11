// Native headless driver for the Lens build of the engine.
// Uses the exact same platform layer (lens_platform.c) as the Lens, so behavior
// verified here is the behavior shipped. Drives scripted input and asserts on
// LensState, the same values the Lens HUD reads.
//
// usage: headless <wad> <script>
// script lines:  <tic> <cmd> [args]     (# comments)
//   down KEY | up KEY | tap KEY      KEY = name (UP DOWN LEFT RIGHT STRAFE_L STRAFE_R USE FIRE RUN) or char
//   mouse DX                          turn
//   type TEXT                         e.g. iddqd
//   restart
//   dump FILE.ppm                     write current frame (palette applied)
//   expect FIELD OP VALUE             OP in == != < <= > >= ; FIELD = LensState name[idx]
//   time                              print ms/tic since start
//   end                               stop

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomkeys.h"
#include "lens_state.h"

void lens_create(void);
void lens_tick(int display);
void lens_restart(void);
void lens_post_key(int pressed, int key);
void lens_post_mouse(int dx);
void lens_tap_key(int key);
void lens_test_damage(int amount);
void lens_test_exit(void);
void lens_test_place(int x, int y, int deg);
int lens_test_ceiling(int x, int y);
void lens_test_spawn_enemy(int dist);
void lens_set_analog(int fwd, int side, int turn);
void lens_set_overlay(const char *text, int shade, int visible_w);
uint32_t *lens_overlay_ptr(void);
void lens_type(const char *t);
int32_t *lens_state_ptr(void);
uint8_t *lens_palette_ptr(void);
uint8_t *lens_framebuffer_ptr(void);
uint8_t *lens_wad_alloc(unsigned size);
float *lens_audio_mix(int n, int nch);
uint32_t *lens_present(void);
uint32_t *lens_face_ptr(void);
uint8_t *lens_face_input_ptr(void);
void lens_face_set_live(int live);
int lens_hud_hit(int x, int y);
void lens_test_fake_face(void);
#include "lens_hud.h"

static const struct { const char *name; int off; } fields[] = {
#define F(n) { #n, LS_##n }
    {"health", LS_HEALTH}, {"armor", LS_ARMOR}, {"ammo", LS_AMMO}, {"readyweapon", LS_READYWEAPON},
    {"weaponowned", LS_WEAPONOWNED}, {"cards", LS_CARDS}, {"faceindex", LS_FACEINDEX},
    {"damagecount", LS_DAMAGECOUNT}, {"bonuscount", LS_BONUSCOUNT}, {"dead", LS_DEAD},
    {"godmode", LS_GODMODE}, {"level_done", LS_LEVEL_DONE}, {"kills", LS_KILLS},
    {"totalkills", LS_TOTALKILLS}, {"items", LS_ITEMS}, {"secrets", LS_SECRETS},
    {"leveltime", LS_LEVELTIME}, {"gametic", LS_GAMETIC}, {"frame_counter", LS_FRAME_COUNTER},
    {"palette_dirty", LS_PALETTE_DIRTY}, {"maxammo", LS_MAXAMMO},
    {"px", LS_PX}, {"py", LS_PY}, {"angle", LS_ANGLE}, {"target", LS_TARGET}, {"autofire", LS_AUTOFIRE}, {"autouse_count", LS_AUTOUSE_COUNT}, {"secret_exit", LS_SECRET_EXIT},
};

static int keycode(const char *k)
{
    static const struct { const char *n; int c; } names[] = {
        {"UP", KEY_UPARROW}, {"DOWN", KEY_DOWNARROW}, {"LEFT", KEY_LEFTARROW},
        {"RIGHT", KEY_RIGHTARROW}, {"STRAFE_L", KEY_STRAFE_L}, {"STRAFE_R", KEY_STRAFE_R},
        {"USE", KEY_USE}, {"FIRE", KEY_FIRE}, {"RUN", KEY_RSHIFT}, {"ESC", KEY_ESCAPE},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(k, names[i].n))
            return names[i].c;
    return (unsigned char)k[0];
}

static int field_off(const char *spec)
{
    char name[64];
    int idx = 0;
    const char *br = strchr(spec, '[');
    size_t n = br ? (size_t)(br - spec) : strlen(spec);
    memcpy(name, spec, n);
    name[n] = 0;
    if (br)
        idx = atoi(br + 1);
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        if (!strcmp(name, fields[i].name))
            return fields[i].off + idx;
    fprintf(stderr, "unknown field %s\n", spec);
    exit(2);
}

// Dump the presented composite (3D view + HUD) and, beside it, the face canvas at 3x
// (both exactly what the Lens uploads; both buffers are top-down).
static void dump(const char *path)
{
    const uint32_t *px = lens_present();
    const uint32_t *fc = lens_face_ptr();
    int W = OUT_W + FACE_W * 3, H = OUT_H > FACE_H * 3 ? OUT_H : FACE_H * 3;
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            uint32_t v = 0;
            if (x < OUT_W) { if (y < OUT_H) v = px[y * OUT_W + x]; }
            else if (y < FACE_H * 3) v = fc[(y / 3) * FACE_W + (x - OUT_W) / 3];
            unsigned char rgb[3] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: headless <wad> <script>\n");
        return 2;
    }
    FILE *wf = fopen(argv[1], "rb");
    if (!wf) { perror(argv[1]); return 2; }
    fseek(wf, 0, SEEK_END);
    long wl = ftell(wf);
    fseek(wf, 0, SEEK_SET);
    uint8_t *buf = lens_wad_alloc((unsigned)wl);
    if (fread(buf, 1, wl, wf) != (size_t)wl) { perror("read"); return 2; }
    fclose(wf);

    FILE *sf = fopen(argv[2], "r");
    if (!sf) { perror(argv[2]); return 2; }

    lens_create();
    int32_t *st = lens_state_ptr();
    double t0 = now_ms();
    int tic = 0, failures = 0, checks = 0;
    char line[1024];
    while (fgets(line, sizeof(line), sf))
    {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        int at;
        char cmd[32], a1[256] = "", a2[512] = "", a3[64] = "";
        int n = sscanf(line, "%d %31s %255s %511s %63s", &at, cmd, a1, a2, a3);
        if (n < 2)
            continue;
        while (tic < at)
        {
            lens_tick(1);
            lens_audio_mix(1260, 2);   // exercise the mixer like JS does (35 Hz @ 44.1k)
            tic++;
        }
        if (!strcmp(cmd, "down")) lens_post_key(1, keycode(a1));
        else if (!strcmp(cmd, "up")) lens_post_key(0, keycode(a1));
        else if (!strcmp(cmd, "tap")) lens_tap_key(keycode(a1));
        else if (!strcmp(cmd, "mouse")) lens_post_mouse(atoi(a1));
        else if (!strcmp(cmd, "type")) lens_type(a1);
        else if (!strcmp(cmd, "restart")) lens_restart();
        else if (!strcmp(cmd, "damage")) lens_test_damage(atoi(a1));
        else if (!strcmp(cmd, "exit")) lens_test_exit();
        else if (!strcmp(cmd, "overlay")) {
            // overlay <visible_w> <text with _ for spaces and | for newlines>
            char txt[256]; int k = 0;
            for (const char *q = a2; *q && k < 255; q++) txt[k++] = *q == '_' ? ' ' : (*q == '|' ? '\n' : *q);
            txt[k] = 0;
            lens_set_overlay(txt, 150, atoi(a1));
            const uint32_t *o = lens_overlay_ptr();
            FILE *f = fopen("build/overlay.ppm", "wb");
            fprintf(f, "P6\n320 200\n255\n");
            for (int i = 0; i < 64000; i++) { unsigned char rgb[3] = { o[i] & 255, (o[i] >> 8) & 255, (o[i] >> 16) & 255 };
                if ((o[i] >> 24) < 255) { rgb[0] = rgb[1] = rgb[2] = 40; } fwrite(rgb, 1, 3, f); }
            fclose(f);
        }
        else if (!strcmp(cmd, "analog")) lens_set_analog(atoi(a1), atoi(a2), atoi(a3));
        else if (!strcmp(cmd, "spawn")) lens_test_spawn_enemy(atoi(a1));
        else if (!strcmp(cmd, "place")) lens_test_place(atoi(a1), atoi(a2), atoi(a3));
        else if (!strcmp(cmd, "ceiling")) printf("tic %d: ceiling(%s,%s) = %d\n", tic, a1, a2, lens_test_ceiling(atoi(a1), atoi(a2)));
        else if (!strcmp(cmd, "expect_ceiling")) { int c = lens_test_ceiling(atoi(a1), atoi(a2)); checks++; if (!(c > atoi(a3))) { failures++; printf("FAIL tic %d: ceiling(%s,%s)=%d not > %s\n", tic, a1, a2, c, a3); } }
        else if (!strcmp(cmd, "face")) { if (atoi(a1)) lens_test_fake_face(); lens_face_set_live(atoi(a1)); }
        else if (!strcmp(cmd, "hit")) printf("tic %d: hit(%s,%s) = %d\n", tic, a1, a2, lens_hud_hit(atoi(a1), atoi(a2)));
        else if (!strcmp(cmd, "dump")) dump(a1);
        else if (!strcmp(cmd, "time"))
            printf("tic %d: %.3f ms/tic\n", tic, (now_ms() - t0) / (tic ? tic : 1));
        else if (!strcmp(cmd, "print"))
            printf("tic %d: %s = %d\n", tic, a1, st[field_off(a1)]);
        else if (!strcmp(cmd, "expect"))
        {
            int v = st[field_off(a1)], want = atoi(a3), ok;
            if (!strcmp(a2, "==")) ok = v == want;
            else if (!strcmp(a2, "!=")) ok = v != want;
            else if (!strcmp(a2, "<")) ok = v < want;
            else if (!strcmp(a2, "<=")) ok = v <= want;
            else if (!strcmp(a2, ">")) ok = v > want;
            else ok = v >= want;
            checks++;
            if (!ok)
            {
                failures++;
                printf("FAIL tic %d: %s (=%d) %s %d\n", tic, a1, v, a2, want);
            }
        }
        else if (!strcmp(cmd, "end")) break;
        else { fprintf(stderr, "bad cmd %s\n", cmd); return 2; }
    }
    printf("%d tics, %d/%d checks passed, %.3f ms/tic\n", tic, checks - failures, checks,
           (now_ms() - t0) / (tic ? tic : 1));
    return failures ? 1 : 0;
}
