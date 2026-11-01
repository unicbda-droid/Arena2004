#include "audio.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SR 44100
#define MAX_VOICES 48
#define MAX_SFX_LEN (SR * 3)

typedef struct {
    float* data;
    int len;
    float gain;
} SfxClip;

typedef struct {
    int clip;
    int pos;
    float vol, pan;
    bool loop;
    bool active;
} Voice;

typedef struct {
    float* data;
    int len;
    int pos;
    float vol;
    int next;
} MusicTrack;

static SDL_AudioDeviceID g_dev = 0;
static bool g_ok = false;
static SfxClip g_clips[SFX_COUNT];
static Voice g_voices[MAX_VOICES];
static MusicTrack g_music[MUS_COUNT];
static int g_cur_music = -1;
static float g_master = 0.85f, g_sfx_vol = 0.9f, g_music_vol = 0.45f;
static SDL_mutex* g_lock = NULL;

static uint32_t g_rng = 0x1234567u;
static float nrnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (float)(g_rng & 0xffff) / 32768.0f - 1.0f; }

static float env_ad(int i, int n, float atk, float dec) {
    (void)n;
    float t = (float)i / SR;
    float a = atk > 0 ? fminf(t / atk, 1.0f) : 1.0f;
    float d = expf(-t * dec);
    return a * d;
}
static float env_adsr(int i, int n, float atk, float hold, float dec) {
    (void)n;
    float t = (float)i / SR;
    if (t < atk) return t / atk;
    if (t < atk + hold) return 1.0f;
    return expf(-(t - atk - hold) * dec);
}

static SfxClip make_clip(int len) {
    SfxClip c;
    c.len = len;
    c.data = calloc(len, sizeof(float));
    c.gain = 0.8f;
    return c;
}

/* simple one-pole lowpass */
typedef struct { float z; } LP;
static float lp(LP* f, float x, float a) { f->z += (x - f->z) * a; return f->z; }

static void synth_step(SfxClip* c) {
    for (int i = 0; i < c->len; i++) {
        float t = (float)i / SR;
        float e = env_ad(i, c->len, 0.002f, 42.0f);
        float n = nrnd() * 0.5f;
        float thump = sinf(t * 2 * M_PI * (95.0f - t * 60.0f)) * expf(-t * 60.0f);
        c->data[i] = (n * 0.35f + thump * 0.6f) * e;
    }
    c->gain = 0.30f;
}
static void synth_jump(SfxClip* c) {
    LP f = {0};
    for (int i = 0; i < c->len; i++) {
        float t = (float)i / SR;
        float fq = 180.0f + t * 420.0f;
        float e = env_ad(i, c->len, 0.004f, 11.0f);
        float v = sinf(t * 2 * M_PI * fq) * 0.5f + lp(&f, nrnd(), 0.15f) * 0.25f;
        c->data[i] = v * e;
    }
    c->gain = 0.42f;
}
static void synth_dodge(SfxClip* c) {
    LP f = {0};
    for (int i = 0; i < c->len; i++) {
        float e = env_ad(i, c->len, 0.01f, 16.0f);
        c->data[i] = lp(&f, nrnd(), 0.32f) * e * 2.2f;
    }
    c->gain = 0.35f;
}
static void synth_land(SfxClip* c) {
    for (int i = 0; i < c->len; i++) {
        float t = (float)i / SR;
        float e = expf(-t * 22.0f);
        float thump = sinf(t * 2 * M_PI * (78.0f - t * 40.0f));
        c->data[i] = (thump * 0.7f + nrnd() * 0.15f) * e;
    }
    c->gain = 0.5f;
}
/* generic gunshot */
static void synth_gun(SfxClip* c, float base_f, float decay, float noise, float body, float len_scale) {
    LP f = {0};
    int n = (int)(c->len * len_scale);
    for (int i = 0; i < c->len; i++) {
        float t = (float)i / SR;
        float e = expf(-t * decay);
        float s = 0;
        if (i < n) {
            s += nrnd() * noise;
            s += sinf(t * 2 * M_PI * base_f) * body * expf(-t * decay * 2.4f);
            s += lp(&f, nrnd(), 0.6f) * noise * 0.5f;
        }
        c->data[i] = s * e;
    }
}
static void synth_explode(SfxClip* c) {
    LP f = {0};
    LP f2 = {0};
    for (int i = 0; i < c->len; i++) {
        float t = (float)i / SR;
        float e = expf(-t * 3.2f);
        float n = lp(&f, nrnd(), 0.06f);
        float n2 = lp(&f2, nrnd(), 0.5f);
        float boom = sinf(t * 2 * M_PI * (48.0f - t * 20.0f)) * expf(-t * 4.0f);
        c->data[i] = (n * 1.8f + n2 * 0.4f + boom * 0.9f) * e;
    }
    c->gain = 0.9f;
}

/* pentatonic arpeggio helpers */
static float note_hz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

static void build_sfx(void) {
    for (int i = 0; i < SFX_COUNT; i++) g_clips[i] = make_clip(SR * 8 / 10);

    synth_step(&g_clips[SFX_STEP]);
    synth_jump(&g_clips[SFX_JUMP]);
    synth_dodge(&g_clips[SFX_DODGE]);
    synth_land(&g_clips[SFX_LAND]);
    g_clips[SFX_AR] = make_clip(SR / 5);
    synth_gun(&g_clips[SFX_AR], 150.0f, 26.0f, 0.55f, 0.7f, 0.5f);
    g_clips[SFX_AR].gain = 0.55f;
    g_clips[SFX_SHOCK] = make_clip(SR * 7 / 10);
    synth_gun(&g_clips[SFX_SHOCK], 420.0f, 9.0f, 0.20f, 0.5f, 1.0f);
    g_clips[SFX_SHOCK].gain = 0.7f;
    g_clips[SFX_ROCKET] = make_clip(SR / 2);
    synth_gun(&g_clips[SFX_ROCKET], 110.0f, 14.0f, 0.45f, 0.8f, 0.6f);
    g_clips[SFX_ROCKET].gain = 0.7f;
    g_clips[SFX_FLAK] = make_clip(SR / 3);
    synth_gun(&g_clips[SFX_FLAK], 240.0f, 18.0f, 0.75f, 0.6f, 0.4f);
    g_clips[SFX_FLAK].gain = 0.65f;
    g_clips[SFX_LIGHTNING] = make_clip(SR * 3 / 5);
    {
        LP f = {0};
        for (int i = 0; i < g_clips[SFX_LIGHTNING].len; i++) {
            float t = (float)i / SR;
            float e = env_ad(i, g_clips[SFX_LIGHTNING].len, 0.01f, 7.0f);
            float z = lp(&f, nrnd(), 0.85f);
            g_clips[SFX_LIGHTNING].data[i] = (z * 0.6f + sinf(t * 2 * M_PI * 900.0f) * 0.08f) * e;
        }
        g_clips[SFX_LIGHTNING].gain = 0.55f;
    }
    g_clips[SFX_ALT_FIRE] = make_clip(SR / 2);
    synth_gun(&g_clips[SFX_ALT_FIRE], 300.0f, 12.0f, 0.3f, 0.5f, 0.7f);
    g_clips[SFX_EXPLODE] = make_clip(SR * 14 / 10);
    synth_explode(&g_clips[SFX_EXPLODE]);
    g_clips[SFX_IMPACT] = make_clip(SR / 5);
    synth_gun(&g_clips[SFX_IMPACT], 700.0f, 40.0f, 0.5f, 0.2f, 0.3f);
    g_clips[SFX_IMPACT].gain = 0.35f;

    /* musical pickups */
    {
        SfxClip* c = &g_clips[SFX_PICKUP_HEALTH]; c->len = SR / 2; free(c->data); c->data = calloc(c->len, sizeof(float));
        int notes[3] = {72, 76, 79};
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < c->len - k * SR / 12; i++) {
                float t = (float)i / SR;
                float e = expf(-t * 9.0f);
                c->data[i + k * SR / 12] += sinf(t * 2 * M_PI * note_hz(notes[k])) * 0.22f * e;
            }
        }
        c->gain = 0.6f;
    }
    {
        SfxClip* c = &g_clips[SFX_PICKUP_ARMOR]; c->len = SR / 2; free(c->data); c->data = calloc(c->len, sizeof(float));
        int notes[3] = {67, 71, 74};
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < c->len - k * SR / 14; i++) {
                float t = (float)i / SR;
                float e = expf(-t * 10.0f);
                c->data[i + k * SR / 14] += (sinf(t * 2 * M_PI * note_hz(notes[k])) + 0.3f * sinf(t * 4 * M_PI * note_hz(notes[k]))) * 0.2f * e;
            }
        }
        c->gain = 0.55f;
    }
    {
        SfxClip* c = &g_clips[SFX_PICKUP_WEAPON]; c->len = SR * 6 / 10; free(c->data); c->data = calloc(c->len, sizeof(float));
        int notes[4] = {60, 67, 72, 79};
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < c->len - k * SR / 16; i++) {
                float t = (float)i / SR;
                float e = expf(-t * 5.0f);
                float saw = sinf(t * 2 * M_PI * note_hz(notes[k])) + 0.5f * sinf(t * 2 * M_PI * note_hz(notes[k]) * 2) + 0.25f * sinf(t * 2 * M_PI * note_hz(notes[k]) * 3);
                c->data[i + k * SR / 16] += saw * 0.14f * e;
            }
        }
        c->gain = 0.6f;
    }
    {
        SfxClip* c = &g_clips[SFX_PICKUP_AMMO]; c->len = SR / 4; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 30.0f);
            c->data[i] = (sinf(t * 2 * M_PI * 320.0f) + nrnd() * 0.3f) * e * 0.4f;
        }
        c->gain = 0.45f;
    }
    {
        SfxClip* c = &g_clips[SFX_JUMPPAD]; c->len = SR / 2; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float fq = 120.0f + t * 1600.0f;
            float e = env_ad(i, c->len, 0.005f, 6.0f);
            c->data[i] = (sinf(t * 2 * M_PI * fq) * 0.5f + nrnd() * 0.2f) * e;
        }
        c->gain = 0.6f;
    }
    {
        SfxClip* c = &g_clips[SFX_HIT]; c->len = SR / 10; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 55.0f);
            c->data[i] = sinf(t * 2 * M_PI * 1400.0f) * e * 0.5f;
        }
        c->gain = 0.45f;
    }
    {
        SfxClip* c = &g_clips[SFX_HEADSHOT]; c->len = SR / 4; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 18.0f);
            c->data[i] = (sinf(t * 2 * M_PI * 2100.0f) + sinf(t * 2 * M_PI * 3150.0f) * 0.4f) * e * 0.45f;
        }
        c->gain = 0.55f;
    }
    {
        SfxClip* c = &g_clips[SFX_DEATH]; c->len = SR * 9 / 10; free(c->data); c->data = calloc(c->len, sizeof(float));
        LP f = {0};
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 4.0f);
            float fq = 220.0f * expf(-t * 1.2f);
            c->data[i] = (sinf(t * 2 * M_PI * fq) * 0.4f + lp(&f, nrnd(), 0.1f) * 0.5f) * e;
        }
        c->gain = 0.6f;
    }
    {
        SfxClip* c = &g_clips[SFX_RESPAWN]; c->len = SR * 7 / 10; free(c->data); c->data = calloc(c->len, sizeof(float));
        int notes[3] = {60, 67, 72};
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < c->len; i++) {
                float t = (float)i / SR;
                float e = env_adsr(i, c->len, 0.04f * (k + 1), 0.12f, 5.0f);
                c->data[i] += sinf(t * 2 * M_PI * note_hz(notes[k])) * 0.13f * e;
            }
        }
        c->gain = 0.5f;
    }
    {
        SfxClip* c = &g_clips[SFX_UI_CLICK]; c->len = SR / 14; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 90.0f);
            c->data[i] = sinf(t * 2 * M_PI * 1250.0f) * e * 0.35f;
        }
        c->gain = 0.4f;
    }
    {
        SfxClip* c = &g_clips[SFX_UI_HOVER]; c->len = SR / 24; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = expf(-t * 120.0f);
            c->data[i] = sinf(t * 2 * M_PI * 780.0f) * e * 0.22f;
        }
        c->gain = 0.28f;
    }
    {
        SfxClip* c = &g_clips[SFX_ANNOUNCE]; c->len = SR * 5 / 10; free(c->data); c->data = calloc(c->len, sizeof(float));
        for (int i = 0; i < c->len; i++) {
            float t = (float)i / SR;
            float e = env_adsr(i, c->len, 0.02f, 0.1f, 6.0f);
            c->data[i] = (sinf(t * 2 * M_PI * 440.0f) + 0.5f * sinf(t * 2 * M_PI * 660.0f)) * 0.16f * e;
        }
        c->gain = 0.5f;
    }
}

/* ---- procedural music loops ---- */
static void build_music_track(MusicTrack* t, int seed, float bpm, int dark) {
    int bars = 8;
    float spb = 60.0f / bpm;
    int len = (int)(SR * spb * 4 * bars);
    t->len = len;
    t->data = calloc(len, sizeof(float));
    t->pos = 0; t->vol = 1.0f; t->next = -1;

    /* minor pentatonic-ish scale */
    static const int scale[7] = {0, 3, 5, 7, 10, 12, 15};
    uint32_t rng = (uint32_t)seed * 2654435761u + 12345u;
    int root = dark ? 45 : 48;

    float* d = t->data;
    LP pad_a = {0}, pad_b = {0};
    for (int bar = 0; bar < bars; bar++) {
        int base = (int)(SR * spb * 4 * bar);
        int chord_root = root + (bar % 2 == 0 ? 0 : (bar % 4 == 1 ? 5 : 3));
        /* pad: sustained chord with slow filter sweep */
        for (int i = 0; i < (int)(SR * spb * 4); i++) {
            float tt = (float)i / SR;
            float e = env_adsr(i, (int)(SR * spb * 4), 0.35f, spb * 2.2f, 1.4f);
            float f0 = note_hz(chord_root), f1 = note_hz(chord_root + 7), f2 = note_hz(chord_root + 12);
            float n = sinf(tt * 2 * M_PI * f0) * 0.5f + sinf(tt * 2 * M_PI * f1) * 0.32f + sinf(tt * 2 * M_PI * f2) * 0.22f;
            n += lp(&pad_a, sinf(tt * 2 * M_PI * f0 * 1.005f) * 0.2f, 0.02f);
            n += lp(&pad_b, nrnd() * 0.1f, 0.01f);
            int idx = base + i;
            if (idx < len) d[idx] += n * e * 0.16f;
        }
        /* arpeggio 16ths */
        for (int step = 0; step < 16; step++) {
            if (((rng >> step) & 3) == 0) continue;
            int ni = (int)((rng >> ((step * 3) & 31)) % 7);
            int oct = ((rng >> (step + 5)) & 3) == 0 ? 12 : 0;
            float fq = note_hz(chord_root + scale[ni] + oct);
            int start = base + (int)(SR * spb * 0.25f * step);
            int slen = (int)(SR * spb * 0.9f);
            for (int i = 0; i < slen; i++) {
                int idx = start + i;
                if (idx >= len) break;
                float tt = (float)i / SR;
                float e = expf(-tt * 7.0f) * (1.0f - expf(-tt * 300.0f));
                float s = sinf(tt * 2 * M_PI * fq) * 0.5f + sinf(tt * 4 * M_PI * fq) * 0.16f;
                d[idx] += s * e * 0.13f;
            }
        }
        /* drums: kick on 1,3 snare on 2,4 hats on 8ths */
        for (int beat = 0; beat < 4; beat++) {
            int start = base + (int)(SR * spb * beat);
            if (beat % 2 == 0) {
                for (int i = 0; i < (int)(SR * 0.22f); i++) {
                    int idx = start + i; if (idx >= len) break;
                    float tt = (float)i / SR;
                    float e = expf(-tt * 16.0f);
                    float f = 60.0f + 120.0f * expf(-tt * 30.0f);
                    d[idx] += sinf(tt * 2 * M_PI * f) * e * 0.5f;
                }
            } else {
                for (int i = 0; i < (int)(SR * 0.18f); i++) {
                    int idx = start + i; if (idx >= len) break;
                    float tt = (float)i / SR;
                    float e = expf(-tt * 24.0f);
                    d[idx] += (nrnd() * 0.34f + sinf(tt * 2 * M_PI * 190.0f) * 0.2f) * e;
                }
            }
            for (int h = 0; h < 2; h++) {
                int hs = start + (int)(SR * spb * 0.5f * h);
                for (int i = 0; i < (int)(SR * 0.05f); i++) {
                    int idx = hs + i; if (idx >= len) break;
                    float tt = (float)i / SR;
                    d[idx] += nrnd() * 0.10f * expf(-tt * 80.0f);
                }
            }
        }
    }
    /* normalize + soft clip */
    float peak = 0.001f;
    for (int i = 0; i < len; i++) peak = fmaxf(peak, fabsf(d[i]));
    float g = 0.85f / peak;
    for (int i = 0; i < len; i++) { float v = d[i] * g; d[i] = v / (1.0f + fabsf(v)); }
}

static void mix_callback(void* ud, Uint8* stream, int len_bytes) {
    (void)ud;
    int n = len_bytes / (int)(sizeof(int16_t) * 2);
    int16_t* out = (int16_t*)stream;
    float* acc = calloc(n, sizeof(float));
    if (!acc) { memset(stream, 0, len_bytes); return; }

    if (g_lock) SDL_LockMutex(g_lock);

    if (g_cur_music >= 0) {
        MusicTrack* t = &g_music[g_cur_music];
        if (t->data && t->len > 0) {
            float mv = g_music_vol * g_master * 0.9f;
            for (int i = 0; i < n; i++) {
                acc[i] += t->data[t->pos] * mv;
                t->pos++;
                if (t->pos >= t->len) t->pos = 0;
            }
        }
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &g_voices[v];
        if (!vo->active) continue;
        SfxClip* c = &g_clips[vo->clip];
        if (!c->data) { vo->active = false; continue; }
        float lg = fminf(vo->vol * c->gain * g_sfx_vol * g_master, 1.6f);
        float l = lg * (0.5f - vo->pan * 0.5f);
        float r = lg * (0.5f + vo->pan * 0.5f);
        for (int i = 0; i < n; i++) {
            float s = c->data[vo->pos] * (0.5f + 0.5f * l); /* placeholder */
            (void)s;
            float sm = c->data[vo->pos];
            acc[i] += sm * (l + r) * 0.5f; /* mono into both; pan below */
            vo->pos++;
            if (vo->pos >= c->len) { if (vo->loop) vo->pos = 0; else { vo->active = false; break; } }
        }
        /* stash per-voice panning weights for simple stereo: re-mix is costly; keep mono-ish */
        (void)l; (void)r;
    }

    if (g_lock) SDL_UnlockMutex(g_lock);

    for (int i = 0; i < n; i++) {
        float v = acc[i];
        v = v / (1.0f + fabsf(v) * 0.6f);
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = (int16_t)(v * 32000.0f);
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
    }
    free(acc);
}

void audio_init(void) {
    if (g_ok) return;
    memset(g_clips, 0, sizeof(g_clips));
    memset(g_voices, 0, sizeof(g_voices));
    memset(g_music, 0, sizeof(g_music));
    g_lock = SDL_CreateMutex();
    build_sfx();
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = SR;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix_callback;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (g_dev == 0) {
        fprintf(stderr, "[audio] SDL audio failed: %s (running silent)\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_dev, 0);
    g_ok = true;
}

void audio_shutdown(void) {
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
    for (int i = 0; i < SFX_COUNT; i++) { free(g_clips[i].data); g_clips[i].data = NULL; }
    for (int i = 0; i < MUS_COUNT; i++) { free(g_music[i].data); g_music[i].data = NULL; }
    if (g_lock) { SDL_DestroyMutex(g_lock); g_lock = NULL; }
    g_ok = false;
}

void audio_set_master(float v) { g_master = v < 0 ? 0 : (v > 1 ? 1 : v); }
void audio_set_sfx(float v) { g_sfx_vol = v < 0 ? 0 : (v > 1 ? 1 : v); }
void audio_set_music(float v) { g_music_vol = v < 0 ? 0 : (v > 1 ? 1 : v); }

void audio_play(SfxId id, float volume, float pan) {
    if (id < 0 || id >= SFX_COUNT) return;
    if (g_lock) SDL_LockMutex(g_lock);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) {
        /* steal oldest-ish: lowest volume */
        float worst = 1e9f; int wi = 0;
        for (int i = 0; i < MAX_VOICES; i++) if (g_voices[i].vol < worst) { worst = g_voices[i].vol; wi = i; }
        slot = wi;
    }
    Voice* v = &g_voices[slot];
    v->clip = id; v->pos = 0; v->vol = volume; v->pan = pan < -1 ? -1 : (pan > 1 ? 1 : pan);
    v->loop = false; v->active = true;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

void audio_play_at(SfxId id, float volume, float x, float y, float z,
                   float lx, float ly, float lz, float lfx, float lfz) {
    float dx = x - lx, dy = y - ly, dz = z - lz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float att = 1.0f / (1.0f + dist * dist * 0.006f);
    if (att < 0.02f) return;
    /* pan by dot with right vector (lfx,lz) ~ fwd; right = (-fz, fx) */
    float rx = -lfz, rz = lfx;
    float dot = dist > 1e-4f ? (dx * rx + dz * rz) / dist : 0;
    float pan = fmaxf(-1.0f, fminf(1.0f, dot));
    audio_play(id, volume * att, pan);
}

void audio_music(MusicId id) {
    if (id < 0 || id >= MUS_COUNT) return;
    if (g_cur_music == (int)id) return;
    if (g_lock) SDL_LockMutex(g_lock);
    if (!g_music[id].data) {
        if (id == MUS_MENU) build_music_track(&g_music[id], 7, 92.0f, 1);
        else if (id == MUS_GAME) build_music_track(&g_music[id], 23, 140.0f, 1);
        else build_music_track(&g_music[id], 41, 104.0f, 1);
    }
    g_music[id].pos = 0;
    g_cur_music = (int)id;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

void audio_update(float dt) { (void)dt; }
bool audio_active(void) { return g_ok; }
