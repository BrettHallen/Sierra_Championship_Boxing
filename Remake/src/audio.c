#include "audio.h"
#include "fight.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Timing, for a 4.77 MHz PC:
 *   - the pause routine (OVR 62EA) and the punch-noise loop (FG2 24EA) count
 *     calls of the random routine: about 45 microseconds each;
 *   - the bell (OVR 6472) toggles the speaker by hand around LOOP
 *     instructions: 17 clocks, 3.56 microseconds each;
 *   - tones come from timer channel 2 (1193182 Hz divided by the divisor).
 */
#define RATE      44100
#define OVERSAMPLE 8
#define UNIT_US   45.0
#define LOOP_US   3.56
#define PIT_HZ    1193182.0
#define AMP       0.30f

typedef struct {
    float *out; int n, max;
    double t;               /* time of the next output sample, seconds */
    double acc; int nacc;   /* box filter for the oversampled level */
    double sub;             /* time of the next sub-sample */
    float hp_x, hp_y;       /* DC blocker (a speaker cone has no DC) */
    int rate;
} Synth;

static void emit_level(Synth *s, double level, double dur)
{
    /* hold the speaker at a level (0..1) for dur seconds */
    double end = s->sub + dur;
    double step = 1.0 / (s->rate * OVERSAMPLE);
    while (s->sub < end && s->n < s->max) {
        s->acc += level; s->nacc++;
        s->sub += step;
        if (s->nacc == OVERSAMPLE) {
            float x = (float)(s->acc / OVERSAMPLE) - 0.5f;
            float y = x - s->hp_x + 0.995f * s->hp_y;
            s->hp_x = x; s->hp_y = y;
            s->out[s->n++] = y * 2 * AMP;
            s->acc = 0; s->nacc = 0;
        }
    }
}

/* Timer channel 2 square wave, gated by the speaker data bit. */
static void pit_tone(Synth *s, double div, double dur)
{
    double f = PIT_HZ / div, half = 0.5 / f, t = 0;
    int hi = 1;
    while (t < dur && s->n < s->max) {
        double d = half < dur - t ? half : dur - t;
        emit_level(s, hi, d);
        t += d; hi = !hi;
    }
}

/* FG2 24EA: a tone chopped at random: each pass of the loop toggles the
 * speaker's data bit when the random number is under thr. */
static void punch_noise(Synth *s, double div, int thr, int count)
{
    double f = PIT_HZ / div, half = 0.5 / f, phase = 0, unit = UNIT_US * 1e-6;
    int on = 1, hi = 1;
    for (int i = 0; i < count && s->n < s->max; i++) {
        if ((rand() & 127) < thr) on = !on;
        double t = 0;
        while (t < unit) {
            double left = half - phase, d = left < unit - t ? left : unit - t;
            emit_level(s, on && hi, d);
            t += d; phase += d;
            if (phase >= half - 1e-12) { phase = 0; hi = !hi; }
        }
    }
    emit_level(s, 0, 0.002);
}

/* OVR 6472: the ring. A square wave of 480 LOOP iterations whose "on" part
 * grows by 4 every few cycles until it is all on, so it fades out; table
 * 1,2,4,8,16 (DS 1DFF) gives the cycles per step, 12 steps each. */
static void ring_internal(Synth *s)
{
    static const int table[] = {1, 2, 4, 8, 16};
    int si = 240, di = 240;
    for (int e = 0; e < 5; e++)
        for (int st = 0; st < 12; st++) {
            for (int c = 0; c < table[e]; c++) {
                emit_level(s, 0, si * LOOP_US * 1e-6);
                emit_level(s, 1, di * LOOP_US * 1e-6);
            }
            di += 4; si -= 4;
        }
    emit_level(s, 1, 0.002);
}

/* OVR 385A: the external (PCjr) bell: three voices at 88h, 89h and 8Ah
 * (3579545 / 32N Hz), the attenuation stepped from 0 to 15 (2 dB a step),
 * 1100 calls a step. Mixed directly rather than through the speaker model. */
static void ring_pcjr(Synth *s)
{
    static const int N[3] = {0x88, 0x89, 0x8A};
    double unit = UNIT_US * 1e-6, ph[3] = {0, 0, 0};
    for (int att = 0; att < 16 && s->n < s->max; att++) {
        double vol = att == 15 ? 0 : pow(10.0, -2.0 * att / 20.0);
        int samples = (int)(1100 * unit * s->rate);
        for (int k = 0; k < samples && s->n < s->max; k++) {
            float v = 0;
            for (int c = 0; c < 3; c++) {
                double f = 3579545.0 / (32.0 * N[c]);
                ph[c] += f / s->rate;
                if (ph[c] >= 1) ph[c] -= 1;
                v += ph[c] < 0.5 ? 1.0f : -1.0f;
            }
            s->out[s->n++] = (float)(v / 3 * vol * AMP);
        }
    }
}

int audio_render(int sfx, int sound, int bell, float *out, int max, int rate)
{
    Synth s;
    memset(&s, 0, sizeof s);
    s.out = out; s.max = max; s.rate = rate;
    double unit = UNIT_US * 1e-6;
    if (!sound) return 0;
    switch (sfx) {
    case SFX_BELL:
        if (!bell) pit_tone(&s, 900, 5000 * unit);            /* OVR 383B: beep */
        else if (sound == 2) ring_pcjr(&s);
        else ring_internal(&s);
        break;
    case SFX_COUNT: pit_tone(&s, 400, 50 * unit); break;      /* FG2 2227 */
    case SFX_TAP:   punch_noise(&s, 300, 35, 150); break;     /* 8399 = 1 */
    case SFX_HEAD:  punch_noise(&s, 600, 55, 500); break;     /* 8399 = 3 */
    case SFX_THUD:  punch_noise(&s, 6000, 25, 450); break;    /* 8399 = 10 */
    case SFX_BODY:  punch_noise(&s, 12000, 5, 300); break;    /* any other */
    case SFX_BUZZ:  pit_tone(&s, 15000, 4000 * unit); break;  /* FG2 0C5E */
    case SFX_ERROR: pit_tone(&s, 5000, 5000 * unit); break;   /* OVR 3784 */
    default: break;
    }
    if (sfx != SFX_BELL || sound != 2 || !bell) emit_level(&s, 0, 0.01);   /* speaker off: let the filter settle */
    return s.n;
}

/* FGT 231F: the internal speaker plays the divisor; the PCjr chip plays it
 * on three voices a step apart, at attenuations 0, 4 and 8. */
static int render_tune(const unsigned short (*t)[2], int n, int sound, float *out, int max, int rate)
{
    Synth s;
    memset(&s, 0, sizeof s);
    s.out = out; s.max = max; s.rate = rate;
    double step = 500 * UNIT_US * 1e-6;
    double ph[3] = {0, 0, 0};
    for (int i = 0; i < n && s.n < max; i++) {
        unsigned dv = t[i][0];
        double dur = t[i][1] * step;
        if (dv == 99) break;
        if (sound == 2) {
            int samples = (int)(dur * rate);
            for (int k = 0; k < samples && s.n < max; k++) {
                float v = 0;
                if (dv) for (int c = 0; c < 3; c++) {
                    double f = PIT_HZ / dv * (1.0 - 0.003 * c);
                    ph[c] += f / rate;
                    if (ph[c] >= 1) ph[c] -= 1;
                    v += (ph[c] < 0.5 ? 1.0f : -1.0f) * (float)pow(10.0, -0.4 * c * 2 / 2);
                }
                s.out[s.n++] = v / 3 * AMP;
            }
        } else if (dv) pit_tone(&s, dv, dur);
        else emit_level(&s, 0, dur);
    }
    emit_level(&s, 0, 0.01);
    return s.n;
}

static SDL_AudioStream *stream;

void audio_tune(const unsigned short (*t)[2], int n, int sound)
{
    if (!stream || !sound || n <= 0) return;
    static float buf[RATE * 40];
    int len = render_tune(t, n, sound, buf, RATE * 40, RATE);
    if (len > 0) SDL_PutAudioStreamData(stream, buf, len * (int)sizeof(float));
}

void audio_stop(void)
{
    if (stream) SDL_ClearAudioStream(stream);
}

int audio_init(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, RATE};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) return 0;
    SDL_ResumeAudioStreamDevice(stream);
    return 1;
}

void audio_quit(void)
{
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
}

void audio_play(int sfx, int sound, int bell)
{
    if (!stream || !sound) return;
    /* The original stops while it makes a sound; we queue them one after the
     * other, but drop a punch when we are already well behind. */
    int queued = SDL_GetAudioStreamQueued(stream) / (int)sizeof(float);
    if (queued > RATE / 4 && sfx != SFX_BELL && sfx != SFX_BUZZ) return;
    static float buf[RATE * 2];
    int n = audio_render(sfx, sound, bell, buf, RATE * 2, RATE);
    if (n > 0) SDL_PutAudioStreamData(stream, buf, n * (int)sizeof(float));
}
