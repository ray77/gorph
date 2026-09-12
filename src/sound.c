#include "sound.h"
#include <SDL.h>
#include <string.h>

#define RATE 44100
#define VOICES 3

enum { WAVE_PULSE, WAVE_NOISE, WAVE_TRI };

typedef struct {
    int    active;
    int    wave;
    double freq, sweep;
    double phase;
    double vol, decay;
    double lp, lpk;            /* Lowpass state/coefficient (0 = off) */
    unsigned long noise;
} Voice;

static Voice voice[VOICES];
static SDL_AudioDeviceID dev;

/* Embedded speech samples (8-bit unsigned, 22050 Hz -> held 2x) */
#include "wondata.h"
#include "presdata.h"
#include "gorphvdata.h"
#include "againdata.h"
#include "takeoverdata.h"
#include "boomdata.h"
#include "glassdata.h"
#include "hahadata.h"
#include "iehhdata.h"
#include "ouhdata.h"
#include "wipedata.h"
#include "moddata.h"
#include "modplay.h"
/* Soft steady wind (title orbit): lowpass-filtered noise with
 * slow gust modulation, gently faded in/out */
static double wind_lvl = 0.0, wind_tgt = 0.0, wind_lp = 0.0, wind_ph = 0.0;
static unsigned long wind_rng = 0xBADCAFEUL;

typedef struct {
    const unsigned char *d;
    long len2;                             /* Length in 44100 units */
    long pos;                              /* -1 = off */
    double gain;
} Smp;
#define NSMP 10
static Smp smp[NSMP] = {
    { won_pcm,   WON_LEN   * 2L, -1, 0.9 },
    { pres_pcm,  PRES_LEN  * 2L, -1, 0.9 },
    { gorphv_pcm, GORPHV_LEN * 2L, -1, 1.6 },
    { again_pcm, AGAIN_LEN * 2L, -1, 0.9 },
    { take_pcm,  TAKE_LEN  * 2L, -1, 0.85 },
    { boom_pcm,  BOOM_LEN  * 2L, -1, 2.5 },
    { glass_pcm, GLASS_LEN * 2L, -1, 2.2 },  /* Mirror break (credits finale) */
    { haha_pcm,  HAHA_LEN  * 2L, -1, 2.0 },  /* Plus the demon's laughter */
    { iehh_pcm,  IEHH_LEN  * 2L, -1, 2.0 },  /* "Iehh": demon is wiped away */
    { ouh_pcm,   OUH_LEN   * 2L, -1, 2.0 }   /* "Ouh": demon fully gone */
};

/* Credits chiptune: own MOD player (modplay.c), level below the rest */
/* Wipe noise (credits end): VERY quiet, even loop, level follows
 * the sponge tempo sluggishly (sound_wipe 0..100, tracked in the mixer
 * over ~250 ms), pitch rises minimally with the tempo */
#define WIPE_GAIN 0.06
static double wipe_fp = 0.0, wipe_tgt = 0.0, wipe_cur = 0.0;
#define MOD_GAIN 2.4
static double modbuf[4096];
static int    mod_on = 0;
static double mod_lvl = 1.0, mod_step = 0.0;   /* Fade-out ramp (sound_mod_fade) */

/* TV glitch: chopped-up white noise (user request instead of MP3
 * sample): noise is switched hard on/off in randomly long chunks
 * (9-60ms), each chunk with its own volume. */
static long          gn_left = 0;          /* Remainder */
static int           gn_gatecnt = 0, gn_gate = 0;
static double        gn_vol = 0.8;
static unsigned long gn_rng = 0x9E3779B9UL;

static double frand(unsigned long *st)
{
    *st = *st * 1103515245UL + 12345UL;
    return (double)((*st >> 16) & 0x7FFF) / 16384.0 - 1.0;
}

static void mix(void *ud, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int n = len / (int)sizeof(Sint16), i, v;
    (void)ud;
    if (mod_on && n <= 4096) mod_render(modbuf, n);
    for (i = 0; i < n; ++i) {
        double s = (mod_on && n <= 4096) ? modbuf[i] * MOD_GAIN * mod_lvl : 0.0;
        if (mod_step > 0.0) {             /* MOD fades out, then stop */
            mod_lvl -= mod_step;
            if (mod_lvl <= 0.0) { mod_lvl = 0.0; mod_step = 0.0; mod_stop(); mod_on = 0; }
        }
        for (v = 0; v < VOICES; ++v) {
            Voice *o = &voice[v];
            double val = 0.0;
            if (!o->active) continue;
            switch (o->wave) {
            case WAVE_NOISE:
                val = frand(&o->noise);
                if (o->lpk > 0.0) {          /* dull rumble instead of hiss */
                    o->lp += o->lpk * (val - o->lp);
                    val = o->lp * 3.0;
                }
                break;
            case WAVE_TRI:
                val = 4.0 * (o->phase < 0.5 ? o->phase : 1.0 - o->phase) - 1.0;
                break;
            default:
                val = (o->phase < 0.5) ? 1.0 : -1.0;
                break;
            }
            s += val * o->vol;
            o->phase += o->freq / RATE;
            if (o->phase >= 1.0) o->phase -= 1.0;
            o->freq += o->sweep / RATE;
            if (o->freq < 20.0) o->freq = 20.0;
            o->vol -= o->decay / RATE;
            if (o->vol <= 0.0) { o->vol = 0.0; o->active = 0; }
        }
        for (v = 0; v < NSMP; ++v) {
            Smp *sp = &smp[v];
            if (sp->pos < 0) continue;
            s += ((double)sp->d[sp->pos >> 1] - 128.0) / 128.0 * sp->gain;
            if (++sp->pos >= sp->len2) sp->pos = -1;
        }
        if (wipe_cur > 0.0005 || wipe_tgt > 0.0) {
            s += ((double)wipe_pcm[(long)wipe_fp] - 128.0) / 128.0 * WIPE_GAIN * wipe_cur;
            wipe_fp += 0.5 * (0.92 + 0.2 * wipe_cur);
            if (wipe_fp >= (double)WIPE_LEN) wipe_fp -= (double)WIPE_LEN;
            wipe_cur += (wipe_tgt - wipe_cur) * 0.00009;
        }
        if (wind_lvl > 0.0001 || wind_tgt > 0.0) {
            double w = frand(&wind_rng), g2;
            wind_lp += 0.08 * (w - wind_lp);
            wind_ph += 0.35 / RATE;
            if (wind_ph >= 1.0) wind_ph -= 1.0;
            g2 = 0.75 + (wind_ph < 0.5 ? wind_ph : 1.0 - wind_ph);
            wind_lvl += (wind_tgt - wind_lvl) * 0.00006;
            s += wind_lp * 17.0 * wind_lvl * g2;
        }
        if (gn_left > 0) {
            if (--gn_gatecnt <= 0) {
                gn_gatecnt = 400 + (int)((frand(&gn_rng) + 1.0) * 1100.0);
                gn_gate = !gn_gate;
                gn_vol = 0.45 + (frand(&gn_rng) + 1.0) * 0.25;
            }
            if (gn_gate) s += frand(&gn_rng) * gn_vol;
            --gn_left;
        }
        if (s >  1.0) s =  1.0;
        if (s < -1.0) s = -1.0;
        out[i] = (Sint16)(s * 7000.0);
    }
}

static void start(int v, int wave, double f, double sweep, double vol, double decay)
{
    voice[v].active = 1;
    voice[v].wave   = wave;
    voice[v].freq   = f;
    voice[v].sweep  = sweep;
    voice[v].vol    = vol;
    voice[v].decay  = decay;
    voice[v].phase  = 0.0;
    voice[v].lp     = 0.0;
    voice[v].lpk    = 0.0;
    if (!voice[v].noise) voice[v].noise = 0x1234567UL + (unsigned long)v * 7919UL;
}

void sound_wind(int strength)
{
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    wind_tgt = strength / 1000.0;
    SDL_UnlockAudioDevice(dev);
}

void sound_mod_start(void)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    if (mod_load(mod_data, MOD_LEN, RATE) == 0) { mod_start(); mod_on = 1; }
    mod_lvl = 1.0; mod_step = 0.0;
    SDL_UnlockAudioDevice(dev);
}

void sound_wipe(int level)
{
    if (!dev) return;
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    SDL_LockAudioDevice(dev);
    wipe_tgt = level / 100.0;
    SDL_UnlockAudioDevice(dev);
}

void sound_mod_seek(double seconds)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    if (!mod_on && mod_load(mod_data, MOD_LEN, RATE) == 0) mod_on = 1;
    if (mod_on) { mod_seek(seconds); mod_lvl = 1.0; mod_step = 0.0; }
    SDL_UnlockAudioDevice(dev);
}

void sound_mod_stop(void)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    mod_stop(); mod_on = 0; mod_step = 0.0;
    SDL_UnlockAudioDevice(dev);
}

void sound_mod_fade(int frames)
{
    if (!dev || frames < 1) return;
    SDL_LockAudioDevice(dev);
    if (mod_on) mod_step = 1.0 / ((double)frames * RATE / 50.0);
    SDL_UnlockAudioDevice(dev);
}

int sound_init(void)
{
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = mix;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return -1;
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}

void sound_shutdown(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}

void sound_play(int id)
{
    int i;
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    switch (id) {
    /* Recipes from PRG effect engine ($A0C1, records $A27A-$A3C2):
     * frequencies/sweeps/durations from decoded original data sets. */
    case SND_SHOOT:      /* FA1->2: noise whoosh + falling triangle "Pew" */
                         start(1, WAVE_NOISE, 1.0,       0.0, 0.5, 3.3);
                         voice[1].lpk = 0.12;
                         start(0, WAVE_TRI, 1156.0, -2630.0, 0.5, 1.25); break;
    case SND_HIT:        /* FA8(+$0C): deep falling noise, LP filter */
                         start(1, WAVE_NOISE, 1.0,       0.0, 1.0, 0.75);
                         voice[1].lpk = 0.07;
                         start(2, WAVE_PULSE,  76.0,   -80.0, 0.6, 0.85); break;
    case SND_EXPLODE:    start(1, WAVE_NOISE, 1.0,       0.0, 0.9, 1.8);
                         start(2, WAVE_PULSE, 180.0,  -260.0, 0.5, 1.8);  break;
    case SND_STEP:       /* March tick: DEEP and very short (~86Hz/70ms,
                          * video measurement; original sync effect) */
                         start(2, WAVE_PULSE,  86.0,   -60.0, 0.8, 11.0); break;
    case SND_BEAM:       /* fx10: pulse riser 442->929Hz over ~1s */
                         start(0, WAVE_PULSE, 442.0,   490.0, 0.5, 0.5);  break;
    case SND_SIREN:      /* fx11: siren warble, ~975Hz slowly falling */
                         start(0, WAVE_TRI,  975.0,  -350.0, 0.5, 0.33);  break;
    case SND_EJECT:      /* fx14: triangle zip 91->2037Hz, ~100ms */
                         start(0, WAVE_TRI,   91.0, 19000.0, 0.55, 4.5);  break;
    case SND_BIGBOOM:    /* fx0D/0E + FA9/0A: ring riser + double rumble */
                         start(0, WAVE_TRI,   46.0,  2000.0, 0.6, 0.9);
                         start(1, WAVE_NOISE, 1.0,       0.0, 1.0, 0.25);
                         voice[1].lpk = 0.05;
                         start(2, WAVE_PULSE, 973.0,  -320.0, 0.6, 0.3);  break;
    case SND_PLAYER_DIE: /* FA8 + $18/$19: explosion + deep two-tone sputter */
                         start(0, WAVE_PULSE,  33.0,    -5.0, 0.9, 0.9);
                         start(1, WAVE_NOISE, 1.0,       0.0, 1.1, 1.0);
                         voice[1].lpk = 0.06;
                         break;
    case SND_MISSION:    /* FA6 whoosh: pulse 91->61Hz ~3s */
                         start(0, WAVE_PULSE,  91.0,   -10.0, 0.5, 0.2);  break;
    case SND_WON:        smp[0].pos = 0;                                  break;
    case SND_PRESENTS:   smp[1].pos = 0;                                  break;
    case SND_WON2:       smp[3].pos = 0;                                  break;
    case SND_TAKEOVER:   smp[4].pos = 0;                                  break;
    case SND_CREDBOOM:   smp[5].pos = 0;                                  break;
    case SND_GLASS:      smp[6].pos = 0;                                  break;
    case SND_HAHA:       smp[7].pos = 0;                                  break;
    case SND_IEHH:       smp[8].pos = 0;                                  break;
    case SND_OUH:        smp[9].pos = 0;                                  break;
    case SND_GORPHVOICE:  smp[2].pos = 0;   /* Voice free: duck synth */
                         for (i = 0; i < VOICES; ++i) voice[i].vol *= 0.25;
                         break;
    case SND_GLITCH:     /* chopped white noise, length = transition 2.5s */
                         gn_left = (long)(RATE * 2.5);
                         gn_gatecnt = 0; gn_gate = 0;                     break;
    case SND_TICK:       /* quiet typewriter click (~10ms) */
                         start(2, WAVE_PULSE, 1400.0,   0.0, 0.12, 18.0); break;
    case SND_KEY:        /* mechanical keyboard stroke (~25ms) */
                         start(1, WAVE_NOISE, 1.0,       0.0, 0.35, 14.0);
                         voice[1].lpk = 0.25;
                         start(2, WAVE_PULSE, 620.0,  -300.0, 0.22, 16.0); break;
    case SND_EXTRA:      start(0, WAVE_PULSE, 660.0,  1800.0, 0.5, 3.0);  break;
    default: break;
    }
    SDL_UnlockAudioDevice(dev);
}
