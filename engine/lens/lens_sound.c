// minidoom: software SFX mixer for the Lens host. JS pulls float PCM.
// GPLv2, part of the doomgeneric fork.

#include <stdint.h>
#include <string.h>

#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"

#define LENS_EXPORT __attribute__((used, visibility("default")))
#define NUM_CHANNELS 16
#define MAX_PULL 4096

typedef struct
{
    const byte *data;     // unsigned 8-bit PCM
    uint32_t len;         // samples
    uint32_t pos;         // 16.16 fixed
    uint32_t step;        // 16.16 fixed, src_rate / out_rate
    float left, right;    // gains incl. volume
    int active;
} channel_t;

// Config variables normally owned by i_sdlsound.c (bound in I_BindSoundVariables).
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static channel_t channels[NUM_CHANNELS];
static int out_rate = 44100;
static boolean use_prefix = true;
static float mixbuf[MAX_PULL * 2];

static void set_params(channel_t *c, int vol, int sep)
{
    // Doom: vol 0..127, sep 0 (left) .. 254 (right), 128 = centre.
    float v = vol / 127.0f;
    float s = sep / 254.0f;
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    c->left = v * (1.0f - s) * 1.414f;
    c->right = v * s * 1.414f;
    if (c->left > v) c->left = v;
    if (c->right > v) c->right = v;
}

static boolean Snd_Init(boolean prefix)
{
    use_prefix = prefix;
    memset(channels, 0, sizeof(channels));
    return true;
}

static void Snd_Shutdown(void) {}

static int Snd_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    if (sfx->link != NULL)
        sfx = sfx->link;
    if (use_prefix)
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void Snd_Update(void) {}

static void Snd_UpdateSoundParams(int ch, int vol, int sep)
{
    if (ch >= 0 && ch < NUM_CHANNELS)
        set_params(&channels[ch], vol, sep);
}

static int Snd_StartSound(sfxinfo_t *sfx, int ch, int vol, int sep)
{
    if (ch < 0 || ch >= NUM_CHANNELS)
        return -1;
    if (sfx->lumpnum < 0)
        sfx->lumpnum = Snd_GetSfxLumpNum(sfx);

    const byte *lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    int lumplen = W_LumpLength(sfx->lumpnum);

    // DMX header: u16 format(3), u16 rate, u32 length; 16 pad bytes front and back.
    if (lumplen < 8 || lump[0] != 0x03 || lump[1] != 0x00)
        return -1;
    int rate = lump[2] | (lump[3] << 8);
    uint32_t length = lump[4] | (lump[5] << 8) | (lump[6] << 16) | ((uint32_t)lump[7] << 24);
    if (length > (uint32_t)lumplen - 8)
        length = lumplen - 8;
    const byte *pcm = lump + 8;
    if (length > 32)
    {
        pcm += 16;
        length -= 32;
    }

    channel_t *c = &channels[ch];
    c->data = pcm;
    c->len = length;
    c->pos = 0;
    c->step = (uint32_t)(((uint64_t)rate << 16) / out_rate);
    set_params(c, vol, sep);
    c->active = 1;
    return ch;
}

static void Snd_StopSound(int ch)
{
    if (ch >= 0 && ch < NUM_CHANNELS)
        channels[ch].active = 0;
}

static boolean Snd_SoundIsPlaying(int ch)
{
    return ch >= 0 && ch < NUM_CHANNELS && channels[ch].active;
}

static void Snd_CacheSounds(sfxinfo_t *sounds, int num) { (void)sounds; (void)num; }

static snddevice_t sound_devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
                                       SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };

sound_module_t DG_sound_module = {
    sound_devices, arrlen(sound_devices),
    Snd_Init, Snd_Shutdown, Snd_GetSfxLumpNum, Snd_Update, Snd_UpdateSoundParams,
    Snd_StartSound, Snd_StopSound, Snd_SoundIsPlaying, Snd_CacheSounds,
};

// Music is a pre-rendered AudioTrackAsset on the Lens side; the engine stays silent.
static boolean Mus_Init(void) { return true; }
static void Mus_Nop(void) {}
static void Mus_SetVolume(int v) { (void)v; }
static void *Mus_Register(void *d, int l) { (void)d; (void)l; return NULL; }
static void Mus_Unregister(void *h) { (void)h; }
static void Mus_Play(void *h, boolean loop) { (void)h; (void)loop; }
static boolean Mus_IsPlaying(void) { return false; }

music_module_t DG_music_module = {
    NULL, 0, Mus_Init, Mus_Nop, Mus_SetVolume, Mus_Nop, Mus_Nop,
    Mus_Register, Mus_Unregister, Mus_Play, Mus_Nop, Mus_IsPlaying, Mus_Nop,
};

// ---------------------------------------------------------------- JS pull API
LENS_EXPORT void lens_audio_set_rate(int rate)
{
    if (rate >= 8000 && rate <= 96000)
        out_rate = rate;
}

// Mix n frames into an interleaved float buffer (channels = 1 or 2). Returns pointer.
LENS_EXPORT float *lens_audio_mix(int n, int nch)
{
    if (n > MAX_PULL)
        n = MAX_PULL;
    if (nch != 2)
        nch = 1;
    memset(mixbuf, 0, sizeof(float) * n * nch);
    int any = 0;
    for (int ci = 0; ci < NUM_CHANNELS; ci++)
        any |= channels[ci].active;
    if (!any)
        return mixbuf;                 // silence: skip the per-sample work entirely
    for (int ci = 0; ci < NUM_CHANNELS; ci++)
    {
        channel_t *c = &channels[ci];
        if (!c->active)
            continue;
        for (int i = 0; i < n; i++)
        {
            uint32_t idx = c->pos >> 16;
            if (idx >= c->len)
            {
                c->active = 0;
                break;
            }
            float smp = (c->data[idx] - 128) / 128.0f;
            if (nch == 2)
            {
                mixbuf[i * 2] += smp * c->left;
                mixbuf[i * 2 + 1] += smp * c->right;
            }
            else
            {
                mixbuf[i] += smp * 0.5f * (c->left + c->right);
            }
            c->pos += c->step;
        }
    }
    for (int i = 0; i < n * nch; i++)
    {
        float v = mixbuf[i] * 0.6f;
        mixbuf[i] = v > 1 ? 1 : (v < -1 ? -1 : v);
    }
    return mixbuf;
}
