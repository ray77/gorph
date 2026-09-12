/* main.c - SDL2 frontend of the native Gorph version.
 *
 *   ./gorph                 play
 *   ./gorph -cheat          plus debug keys: 1-4 mission 1-4, 5 replay title
 *                           intro, 6 flagship kill, 8 level 8 outro,
 *                           9 replay boot screen, N next mission incl. rank,
 *                           0 in the credits: jump to just before the end
 *   ./gorph -f N -shot b.bmp   run N logic steps, write single frame
 *   make wiperec && ./wiperec tools/wipe_bg.bmp   record wipe path for the
 *                           credits ending with the mouse (S = save to
 *                           src/wipepath.h, then make)
 *   Headless hooks (only with -shot): GORPH_JUMP=m, GORPH_AUTOFIRE=1,
 *   GORPH_CREDITS=N, GORPH_BOOT=1, GORPH_FRAC=x, GORPH_WDBG=mask.pgm
 *
 * Controls: arrow keys/WASD, space = fire, F11 fullscreen, Esc,
 * C on the title screen = credits.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gorph.h"
#include "vic.h"
#include "sound.h"
#include "robotimg.h"
#include "bootimg.h"
#include "amigafont.h"
#include "creditslogo.h"
#include "lensfx.h"
#include "monsterimg.h"
#include "rockimg.h"
#include "armimg.h"
#include "wipepath.h"

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static SDL_Texture  *btex;
static SDL_GameController *pad;
static unsigned char frame[VIC_W * VIC_H];
static Uint32        pixels[VIC_W * VIC_H];

static void to_pixels(void)
{
    int i;
    if (g.fade <= 0) {
        for (i = 0; i < VIC_W * VIC_H; ++i) pixels[i] = 0xFF000000UL;
    } else if (g.fade >= 256) {
        for (i = 0; i < VIC_W * VIC_H; ++i)
            pixels[i] = 0xFF000000UL | vic_palette[frame[i] & 15];
    } else {
        for (i = 0; i < VIC_W * VIC_H; ++i) {
            unsigned long c = vic_palette[frame[i] & 15];
            unsigned long r = (((c >> 16) & 0xFF) * (unsigned long)g.fade) >> 8;
            unsigned long gc = (((c >> 8) & 0xFF) * (unsigned long)g.fade) >> 8;
            unsigned long b = ((c & 0xFF) * (unsigned long)g.fade) >> 8;
            pixels[i] = 0xFF000000UL | (r << 16) | (gc << 8) | b;
        }
    }
}

/* Boot screen on its OWN 640x400 texture (higher resolution than
 * the game - "real image"): 1701 monitor, typewriter "PRESS SPACE
 * ON TAPE" starting at the READY. cursor position (16px glyphs to
 * match the BASIC font), zoom into the screen + fade. */
static Uint32 bpix[BOOT_TW * BOOT_TH];

static int bglyph(char c)
{
    if (c >= 'A' && c <= 'Z') return 0x41 + c - 'A';
    if (c >= '0' && c <= '9') return 0x70 + c - '0';
    if (c == '*') return 0x6A;
    return -1;
}

static void boot_render(void)
{
    static const char *bt1 = "PRESS SPACE ON";
    static const char *bt2 = "TAPE";
    static const char *h1 = "**** COMMODORE 64 BASIC V3 ****";
    static const char *h2 = "64K RAM SYSTEM 3891 BASIC BYTES FREE";
    int x, y, t = g.statetimer, shown, f2 = g.fade;
    int hgl = (g.state == ST_BOOT && t >= 30 && t < 70);
    int roll = 0;
    int cf = 256;
    int cur = (t >= 50 && !((t >> 4) & 1));   /* blinks forever */
    if (hgl) {                            /* V-Hold slip: whole screen
                                           * jumps vertically, wraps */
        unsigned long sd = (unsigned long)(t / 3) * 2654435761UL;
        int amp = (70 - t);
        if (amp > 0) roll = (int)((sd >> 8) % (unsigned long)(amp * 2 + 1)) - amp;
    }
    if (f2 > 256) f2 = 256;
    if (f2 < 0) f2 = 0;
    shown = (t < 50) ? 0 : (t - 50) / 4;
    if (shown > 18) shown = 18;
    if (g.state != ST_BOOT) {             /* Crossfade phase: text holds */
        f2 = 256; shown = 18; cur = 0;
    }
    for (y = 0; y < BOOT_TH; ++y)
        for (x = 0; x < BOOT_TW; ++x) {
            int sx = x, sy = y;
            unsigned long r = 0, gg = 0, b = 0;
            if (g.bootzoom > 0) {
                int z = 64 + g.bootzoom * 4;
                sx = 318 + (x - 318) * 64 / z;
                sy = 164 + (y - 164) * 64 / z;
            }
            if (roll && sx >= 159 && sx <= 479 && sy >= 44 && sy <= 290)
                sy = 44 + (sy - 44 + roll + 247 * 4) % 247;
            if (sx >= 112 && sx < 112 + BOOTI_W && sy >= 0 && sy < BOOTI_H) {
                const unsigned char *p =
                    boot_rgb + (sy * BOOTI_W + (sx - 112)) * 3;
                r = p[0]; gg = p[1]; b = p[2];
            }
            /* BASIC header: flashes in at t=30 with image glitch */
            if (t >= 30 && sy >= 74 && sy < 94) {
                int gsx = sx, on = 0, code = -1, row = 0, ci;
                if (hgl) gsx += ((sy * 31 + t * 17) % 9) - 4;
                if (sy < 82 && gsx >= 195 && gsx < 195 + 31 * 8) {
                    ci = (gsx - 195) / 8; row = sy - 74;
                    if (h1[ci] != ' ') code = bglyph(h1[ci]);
                    if (code >= 0)
                        on = (vic.charset[code * 8 + row]
                              >> (7 - ((gsx - 195) & 7))) & 1;
                } else if (sy >= 86 && gsx >= 175 && gsx < 175 + 36 * 8) {
                    ci = (gsx - 175) / 8; row = sy - 86;
                    if (h2[ci] != ' ') code = bglyph(h2[ci]);
                    if (code >= 0)
                        on = (vic.charset[code * 8 + row]
                              >> (7 - ((gsx - 175) & 7))) & 1;
                }
                if (on) {
                    unsigned long hb = (hgl && ((t + sy) & 1)) ? 128 : 256;
                    r  = (0x6CUL * hb) >> 8;
                    gg = (0x5EUL * hb) >> 8;
                    b  = (0xB5UL * hb) >> 8;
                }
            }
            {
                int on = 0, iscur = 0;
                if (sy >= 147 && sy < 163 &&
                    sx >= 207 && sx < 207 + 15 * 16) {
                    int ci = (sx - 207) / 16;
                    if (ci < shown && ci < 14 && bt1[ci] != ' ') {
                        unsigned char gb = vic.charset[
                            (0x41 + bt1[ci] - 'A') * 8 + ((sy - 147) >> 1)];
                        on = (gb >> (7 - (((sx - 207) >> 1) & 7))) & 1;
                    } else if (cur && shown < 14 && ci == shown) {
                        iscur = 1;
                    }
                } else if (sy >= 171 && sy < 187 &&
                           sx >= 287 && sx < 287 + 5 * 16) {
                    int ci = (sx - 287) / 16;
                    if (ci < shown - 14 && ci < 4) {
                        unsigned char gb = vic.charset[
                            (0x41 + bt2[ci] - 'A') * 8 + ((sy - 171) >> 1)];
                        on = (gb >> (7 - (((sx - 287) >> 1) & 7))) & 1;
                    } else if (cur && shown >= 14 && ci == shown - 14) {
                        iscur = 1;
                    }
                }
                if (on) { r = 0x6C; gg = 0x5E; b = 0xB5; }
                else if (iscur) {
                    r  = (r  * (256UL - cf) + 0x6CUL * cf) >> 8;
                    gg = (gg * (256UL - cf) + 0x5EUL * cf) >> 8;
                    b  = (b  * (256UL - cf) + 0xB5UL * cf) >> 8;
                }
            }
            r = r * (unsigned long)f2 >> 8;
            gg = gg * (unsigned long)f2 >> 8;
            b = b * (unsigned long)f2 >> 8;
            bpix[y * BOOT_TW + x] = 0xFF000000UL | (r << 16) | (gg << 8) | b;
        }
}
/* Typewriter alpha blending: text cells reported by game.c are
 * dimmed continuously (background there is black -> pure fade) */
static void tw_overlay(void)
{
    int i, x, y;
    for (i = 0; i < g.tw_n; ++i) {
        unsigned long a = g.tw_a[i];
        int px = g.tw_cx[i] * 8, py = g.tw_cy[i] * 8;
        for (y = 0; y < 8; ++y) {
            /* slight darkening downward (Photoshop reference) */
            unsigned long f = (a * (256UL - (unsigned long)y * 15UL)) >> 8;
            for (x = 0; x < 8; ++x) {
                Uint32 *p = &pixels[(py + y) * VIC_W + px + x];
                unsigned long r, g2, b;
                if ((*p & 0xFFFFFFUL) != 0xFFFFFFUL)
                    continue;             /* dim only text pixels (white) -
                                           * sprites in front stay untouched */
                r = (0xFFUL * f) >> 8;
                g2 = r; b = r;
                *p = 0xFF000000UL | (r << 16) | (g2 << 8) | b;
            }
        }
    }
}

/* Outro watermark: robot as a FREEZE FRAME with real transparency
 * (alpha g.robot_a/64) blended over the finished RGB image, top center */
static void robot_overlay(void)
{
    int x, y, a = g.robot_a;
    if (a <= 0) return;
    for (y = 0; y < ROBOT_H; ++y)
        for (x = 0; x < ROBOT_W; ++x) {
            unsigned char v = robot_img[y * ROBOT_W + x];
            Uint32 *p;
            unsigned long c, r0, g0, b0, r1, g1, b1;
            if (v == 0xFF) continue;
            p = &pixels[(28 + y) * VIC_W + (105 + x)];
            c = vic_palette[v];
            r0 = (*p >> 16) & 0xFF; g0 = (*p >> 8) & 0xFF; b0 = *p & 0xFF;
            r1 = (c >> 16) & 0xFF;  g1 = (c >> 8) & 0xFF;  b1 = c & 0xFF;
            r0 = (r0 * (64 - a) + r1 * a) >> 6;
            g0 = (g0 * (64 - a) + g1 * a) >> 6;
            b0 = (b0 * (64 - a) + b1 * a) >> 6;
            *p = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
        }
}

/* ====================================================================== */
/*  Credits (key C on title screen) - Amiga demo style, 640x400 texture:  */
/*  3D starfield flies toward the viewer and rotates, with real           */
/*  Motion blur (decaying trail buffer); logo fades in slowly at          */
/*  the top; sine scroller "CREDITS:" swings in the middle.               */
/* ====================================================================== */

#define CR_STARS 130

static double cr_sx[CR_STARS], cr_sy[CR_STARS], cr_sz[CR_STARS];
static Uint32 cr_trail[BOOT_TW * BOOT_TH];
static Uint32 cr_last[VIC_W * VIC_H];     /* title snapshot (fade out) */
static int    cr_ready = 0;
static int    cr_mirror = 0;                  /* glyphs mirrored below CR_ML */
#define CR_TY0 240                            /* scrolltext layer from row 240 */
static Uint32 cr_tlayer[BOOT_TW * BOOT_TH];   /* scrolltext layer (AARRGGBB):
                                               * glyphs land here, the layer
                                               * is faded in with lens offset
                                               * and demon shadow */
#define CR_ML 358                             /* mirror edge (floor line) */
/* Credits FINALE (after the text sequence): the demon fetches a rock
 * from behind the logo, comes around the right side to the front, holds
 * before the logo center, drops it; the rock smashes the mirror floor
 * (shards, glass sound, MOD fades out), image fades out -> title. */
#define CR_ROCK_G 0.20                        /* rock fall acceleration px/f^2 */
#define CR_FIN_B  40.0                        /* behind the logo: fetch rock */
#define CR_FIN_C 200.0                        /* half a lap to the front */
#define CR_FIN_D 100.0                        /* hold at front, then release */
#define CR_NSH   56                           /* shards */
static int    cr_fin_ready = 0, cr_dir0 = 1, cr_broken = 0, cr_laughed = 0;
/* WIPER (end of credits): a robot arm with a sponge wipes the image
 * away - humanly: the sponge circles constantly (radius breathes),
 * moves in zigzag paths from top to bottom, then does a targeted
 * touch-up pass until NO pixel is left, and exits downward out of
 * the picture. The wipe footprint is the sponge shape itself; wiped
 * pixels stay black (mask). State is advanced per render with dt. */
#define CR_WIPE_START 100.0                   /* after the impact */
#define CR_WIPE_BANDS 8                       /* paths of 50 px */
static int    cr_wp = -1;                     /* -1 off, 0 entry, 1 stroke,
                                               * 2 transition to the next
                                               * stroke, 3 touch-up, 4 done
                                               * (hand stays put, crossfade
                                               * to the title screen) */
static int    cr_wband = 0, cr_frozen = 0, cr_wtarget = -1,
              cr_wdown = 0,                   /* path mode: sponge bottom */
              cr_iehh = 0, cr_ouh = 0;        /* "Iehh"/"Ouh" already played */
static double cr_wt = 0.0, cr_wx = 40.0, cr_wy = 460.0, cr_wcirc = 0.0,
              cr_wpx = 0.0, cr_wpy = 0.0, cr_wdur = 45.0, cr_wspd = 0.0,
              cr_wax = 18.0, cr_way = 14.0;   /* current scrub amplitudes */
static unsigned char cr_wmask[BOOT_TW * BOOT_TH];
static Uint32 cr_freeze[BOOT_TW * BOOT_TH];   /* freeze frame while wiping: stars
                                               * and animations stand still */
static unsigned char cr_mmask[BOOT_TW * BOOT_TH]; /* monster pixels in the freeze frame
                                                   * (for the "Iehh") */
static int    cr_titlefade = 0;               /* title screen fades in after the
                                               * wipe, hand out (crossfade) */
#define CR_LAUGH_AT 15.0                      /* laughter 0.3 s after impact */
static double cr_tend, cr_turns0, cr_tA, cr_tdrop, cr_timp, cr_rx0, cr_ry0;
static Uint32        cr_snap[BOOT_TW * (BOOT_TH - CR_ML)];  /* mirror on break */
static unsigned char cr_shid[BOOT_TW * (BOOT_TH - CR_ML)];  /* shard cells */
static struct { double mx, my, vx, vy, w; int x0, y0, x1, y1; } cr_sh[CR_NSH];
static double cr_tf = 0.0, cr_tprev = -1.0;  /* credits time in 50Hz frames,
                                              * with sub-frame part (VSync) */
static double cr_ox = 0.0, cr_oy = 0.0;   /* camera sideways offset (world) */
static double cr_cs = 1.0, cr_sn = 0.0;   /* field rotation now */
/* letter flight: if the demon steps in front of the scroller, the
 * glyphs flee sideways out of the image (the edge vignette fades the
 * exit softly) and dare to come back while he still stands there.
 * 0 = home, 1 = fully outside; clocked once per frame. */
static double cr_fluchtF = 0.0;
static double cr_flucht_voll = -1.0, cr_flucht_prev = 0.0;

static unsigned long cr_rng = 0x51AB77E1UL;

static double cr_frand(void)
{
    cr_rng = cr_rng * 1103515245UL + 12345UL;
    return (double)((cr_rng >> 16) & 0x7FFF) / 16384.0 - 1.0;
}

static void cr_star_reset(int i, int deep)
{
    /* spawn around the (panned) camera center, namely IN
     * the view window of the rolled depth (it shrinks with z -
     * otherwise near stars spawn invisible and only cycle);
     * rotate the offset back out of the rotated space */
    double z = deep ? 1.0 : 0.08 + (cr_frand() + 1.0) * 0.46;
    cr_sz[i] = z;
    cr_sx[i] = cr_frand() * 1.35 * z + (-cr_ox) * cr_cs + (-cr_oy) * cr_sn;
    cr_sy[i] = cr_frand() * 1.35 * z - (-cr_ox) * cr_sn + (-cr_oy) * cr_cs;
}

/* Seamless transition title -> credits: the 40 intro stars are taken
 * into the 3D path at their current screen position (morphing), the
 * rest of the field trickles in staggered; the last title image
 * fades out additively. Call BEFORE the state change (title time!). */
static void cr_finale_reset(void)
{
    cr_broken = 0;                        /* mirror intact again */
    cr_laughed = 0;
    cr_wp = -1; cr_wband = 0; cr_wt = 0.0;
    cr_wx = 40.0; cr_wy = 460.0; cr_wcirc = 0.0; cr_frozen = 0; cr_wtarget = -1;
    cr_wax = 18.0; cr_way = 14.0; cr_wdown = 0;
    cr_wspd = 0.0; cr_iehh = 0; cr_ouh = 0;
    memset(cr_wmask, 0, sizeof(cr_wmask));
    cr_titlefade = 0;
}

static void cr_morph_init(void)
{
    int i, tx, ty;
    cr_finale_reset();
    for (i = 0; i < CR_STARS; ++i) {
        if (i < 40) {
            double z = 0.35 + (double)(i % 5) * 0.1;
            game_title_star(i, &tx, &ty);
            cr_sz[i] = z;
            cr_sx[i] = ((double)tx * 2.0 - 320.0) * z / 230.0;
            cr_sy[i] = ((double)ty * 2.0 - 200.0) * z / 230.0;
        } else {
            cr_sz[i] = -1.0 - (double)(i - 40);   /* waits (i-40) frames */
        }
    }
    memset(cr_trail, 0, sizeof(cr_trail));
    memcpy(cr_last, pixels, sizeof(cr_last));
    cr_fluchtF = 0.0; cr_flucht_voll = -1.0; cr_flucht_prev = 0.0;
    cr_ready = 1;
}

static void cr_plot(int x, int y, unsigned long r, unsigned long gg,
                    unsigned long b)
{
    Uint32 *p;
    unsigned long r0, g0, b0;
    if (x < 0 || x >= BOOT_TW || y < 0 || y >= BOOT_TH) return;
    p = &cr_trail[y * BOOT_TW + x];
    r0 = ((*p >> 16) & 0xFF) + r; if (r0 > 255) r0 = 255;
    g0 = ((*p >> 8) & 0xFF) + gg; if (g0 > 255) g0 = 255;
    b0 = (*p & 0xFF) + b;         if (b0 > 255) b0 = 255;
    *p = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
}

static void cr_text(const char *s, int x, int y,
                    unsigned long r, unsigned long gg, unsigned long b)
{
    int gx = x, gy, k;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        if (c >= AF_FIRST && c < AF_FIRST + AF_COUNT) {
            const unsigned long *bits = af_bits[c - AF_FIRST];
            int w = af_w[c - AF_FIRST];
            for (gy = 0; gy < AF_H; ++gy) {
                /* slight vertical gradient: white on top, bluish below */
                unsigned long f = 256UL - (unsigned long)gy * 3UL;
                int py = y + gy;
                if (py < 0 || py >= BOOT_TH) continue;
                for (k = 0; k < w; ++k) {
                    int px = gx + k;
                    if (px < 0 || px >= BOOT_TW) continue;
                    if ((bits[gy] >> (31 - k)) & 1)
                        bpix[py * BOOT_TW + px] = 0xFF000000UL
                            | (((r * f) >> 8) << 16)
                            | (((gg * f) >> 8) << 8) | b;
                }
            }
            gx += w + 3;
        } else
            gx += 15;
    }
}

static int cr_gw(int c);                 /* '*' = own star glyph (below) */

static int cr_text_w(const char *s)
{
    int w = 0;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        w += (c >= AF_FIRST && c < AF_FIRST + AF_COUNT)
             ? cr_gw(c) + 3 : 15;
    }
    return w - 3;
}

/* HSV rainbow (6 segments of 256) - shared palette for the copper
 * line and the scroller fill */
static void cr_rainbow(int ph, int *r, int *gg, int *b)
{
    int seg = (ph >> 8) % 6, f = ph & 255;
    switch (seg) {
    case 0:  *r = 255;     *gg = f;       *b = 0;       break;
    case 1:  *r = 255 - f; *gg = 255;     *b = 0;       break;
    case 2:  *r = 0;       *gg = 255;     *b = f;       break;
    case 3:  *r = 0;       *gg = 255 - f; *b = 255;     break;
    case 4:  *r = f;       *gg = 0;       *b = 255;     break;
    default: *r = 255;     *gg = 0;       *b = 255 - f; break;
    }
}

/* Mini text line (6px high, OR downsampling of 32 font);
 * gradient: dark gray at top -> white at bottom */
#define SIG_H 6

static int cr_sig_gw(int c)
{
    int w = (af_w[c - AF_FIRST] * SIG_H + 31) / 32;
    return (w < 2) ? 2 : w;
}

static int cr_text_small_w(const char *s)
{
    int w = 0;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        w += (c >= AF_FIRST && c < AF_FIRST + AF_COUNT)
             ? cr_sig_gw(c) + 1 : 3;
    }
    return w - 1;
}

static void cr_text_small(const char *s, int x, int y, int a)
{
    int gx = x, gy, k;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        if (c >= AF_FIRST && c < AF_FIRST + AF_COUNT) {
            const unsigned long *bits = af_bits[c - AF_FIRST];
            int w32 = af_w[c - AF_FIRST];
            int w = cr_sig_gw(c);
            for (gy = 0; gy < SIG_H; ++gy) {
                int py = y + gy;
                int sy0 = gy * AF_H / SIG_H, sy1 = (gy + 1) * AF_H / SIG_H;
                unsigned long v = ((110UL + 145UL * (unsigned long)gy
                                    / (SIG_H - 1)) * (unsigned long)a) >> 8;
                Uint32 col = 0xFF000000UL | (v << 16) | (v << 8) | v;
                if (py < 0 || py >= BOOT_TH) continue;
                for (k = 0; k < w; ++k) {
                    int px = gx + k, on = 0, sx, sy;
                    int sx0 = k * w32 / w, sx1 = (k + 1) * w32 / w;
                    if (px < 0 || px >= BOOT_TW) continue;
                    for (sy = sy0; sy < sy1 && !on; ++sy)
                        for (sx = sx0; sx < sx1; ++sx)
                            if ((bits[sy] >> (31 - sx)) & 1) { on = 1; break; }
                    if (on) bpix[py * BOOT_TW + px] = col;
                }
            }
            gx += w + 1;
        } else
            gx += 3;
    }
}

/* Five-pointed star as special glyph for '*' in credits text (32x32,
 * bit 31 = left column, like af_bits) */
static const unsigned long star_bits[32] = {
    0x00000000UL, 0x00010000UL, 0x00010000UL, 0x00038000UL,
    0x00038000UL, 0x00038000UL, 0x0007C000UL, 0x0007C000UL,
    0x0007C000UL, 0x000FE000UL, 0x000FE000UL, 0x7FFFFFFCUL,
    0x3FFFFFF8UL, 0x0FFFFFE0UL, 0x07FFFFC0UL, 0x03FFFF80UL,
    0x00FFFE00UL, 0x007FFC00UL, 0x007FFC00UL, 0x007FFC00UL,
    0x007FFC00UL, 0x00FFFE00UL, 0x00FEFE00UL, 0x00FC7E00UL,
    0x00F01E00UL, 0x01E00F00UL, 0x01800300UL, 0x01000100UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL };
#define STAR_W 32

static const unsigned long *cr_gbits(int c)
{ return (c == '*') ? star_bits : af_bits[c - AF_FIRST]; }
static int cr_gw(int c)
{ return (c == '*') ? STAR_W : af_w[c - AF_FIRST]; }

/* Blit a glyph ROTATED about its center (target iteration with
 * inverse rotation) - the letters follow the wave tangent; copper
 * fill stays on the RASTER LINE (like the stripe) */
static void cr_glyph_rot(int c, int cx, int cy, double ang, int cph, double sc)
{
    const unsigned long *bits;
    int w, dx, dy, hw, rx, ry;
    double cs = cos(ang) / sc, sn = sin(ang) / sc;   /* /sc = shrink */
    if (c < AF_FIRST || c >= AF_FIRST + AF_COUNT) return;
    bits = cr_gbits(c);
    w = cr_gw(c);
    hw = w / 2;
    ry = (int)(24 * sc) + 1; rx = (int)((hw + 15) * sc) + 1;
    for (dy = -ry; dy <= ry; ++dy)
        for (dx = -rx; dx <= rx; ++dx) {
            int sx = (int)floor(dx * cs + dy * sn + 0.5) + hw;
            int sy = (int)floor(-dx * sn + dy * cs + 0.5) + AF_H / 2;
            int px = cx + dx, py = cy + dy, r, gg, b;
            if (sx < 0 || sx >= w || sy < 0 || sy >= AF_H) continue;
            if (!((bits[sy] >> (31 - sx)) & 1)) continue;
            if (px < 0 || px >= BOOT_TW || py < 0 || py >= BOOT_TH) continue;
            /* warm palette: dark red -> yellow -> dark red (cyclic,
             * no blue component) */
            {
                int ph = ((py * 18 + cph) % 1536 + 1536) % 1536;
                int tri = (ph < 768) ? ph / 3 : (1535 - ph) / 3;
                if (tri > 255) tri = 255;
                r = 170 + (85 * tri >> 8);
                gg = tri;
                b = 0;
            }
            /* edge vignette: 120px before the left/right screen edge
             * blend smoothly into the background - entry/exit
             * "emerges" instead of popping up at the edge */
            {
                int e = (px < BOOT_TW - 1 - px) ? px : BOOT_TW - 1 - px;
                int xd = px, yd = py;
                unsigned long fa = (e >= 120) ? 256UL
                                 : (unsigned long)e * 256UL / 120UL;
                if (cr_mirror) {          /* floor reflection: flipped below
                                           * the edge, dimmed, fading with
                                           * depth, slight ripple */
                    int depth;
                    yd = 2 * CR_ML - py;
                    if (py >= CR_ML || yd >= BOOT_TH) continue;
                    depth = yd - CR_ML;   /* 1.. */
                    xd = px + (int)(sin(yd * 0.35 + cph * 0.03) * 2.0);
                    if (xd < 0 || xd >= BOOT_TW) continue;
                    fa = fa * (unsigned long)(depth < 42 ? (42 - depth) : 0) / 42UL;
                    fa = fa * 120UL >> 8;
                    if (!fa) continue;
                }
                /* into the text layer (alpha = coverage); the fade-in happens
                 * later in cr_text_composite (lens + shadow) */
                if (fa > 255) fa = 255;
                cr_tlayer[yd * BOOT_TW + xd] = (fa << 24)
                    | ((unsigned long)r << 16) | ((unsigned long)gg << 8)
                    | (unsigned long)b;
            }
        }
}

/* Lens flares: the spritesheet animation flashes at THREE changing
 * bright spots of the logo (2x scaled, additive, GRAY instead of
 * blue: max channel as brightness) */
static void cr_flares(int t)
{
    /* highlights on CLAWS and METAL in the upper claw band
     * (y<115, reddish, surroundings >=75% opaque - never in
     * space or on the lettering) */
    static const short fp[9][2] = {
        {126, 49},{226,100},{210,  6},
        {294, 57},{104, 99},{191, 71},
        {195, 46},{293, 88},{168,101} };
    /* GLOBAL beat: exactly ONE flash per ~5s (= 3 per 15s), each
     * animation runs fully through - never two overlapping
     * (overlapping slots used to look "too fast") */
    {
        /* nonlinear playback curve: build up briskly, hold the big,
         * clearly visible frames much longer - otherwise the
         * flash looks "too fast" depending on the background */
        static const unsigned char lf_dur[13] =
            { 6, 6, 6, 6, 6, 14, 14, 14, 14, 14, 14, 14, 10 };
        int cyc = (t - 260 + 200) / 250;
        int ph  = (t - 260 + 200) % 250;
        int fr, pi, ax, ay, w, x, y, c, p, dly;
        const unsigned char *src;
        if (t < 260) return;              /* only when the logo is fully up */
        dly = (cyc * 89 + ((cyc * 37) >> 1)) % 60;   /* slight scatter */
        ph -= dly;
        if (ph < 0) return;
        for (fr = 0; fr < LF_N && ph >= (int)lf_dur[fr]; ++fr)
            ph -= lf_dur[fr];
        if (fr >= LF_N) return;           /* pause until the next flash */
        /* anchor: jump width 1..8 mod 9 -> never the same spot
         * twice in a row */
        p = 0;
        for (c = 1; c <= cyc; ++c)
            p += 1 + ((c * 7 + ((c * 11) >> 2)) % 8);
        pi = p % 9;
        ax = 110 + fp[pi][0];
        ay = 4 + fp[pi][1];
        w = lf_w[fr];
        src = lf_rgba + (long)lf_off[fr] * LF_H * 4;
        for (y = 0; y < LF_H; ++y)
            for (x = 0; x < w; ++x) {
                const unsigned char *sp = src + (y * w + x) * 4;
                unsigned long a2;
                int dx, dy;
                if (!sp[3]) continue;
                /* keep the sheet's original color nuances */
                a2 = ((unsigned long)sp[3] * 3) >> 2;   /* ~75% opacity */
                for (dy = 0; dy < 2; ++dy)
                    for (dx = 0; dx < 2; ++dx) {
                        int px = ax - w + x * 2 + dx;
                        int py = ay - LF_H + y * 2 + dy;
                        Uint32 *d;
                        unsigned long r0, g0, b0;
                        if (px < 0 || px >= BOOT_TW ||
                            py < 0 || py >= BOOT_TH) continue;
                        d = &bpix[py * BOOT_TW + px];
                        r0 = (*d >> 16) & 0xFF;
                        g0 = (*d >> 8) & 0xFF;
                        b0 = *d & 0xFF;
                        r0 = (r0 * (256 - a2) + sp[0] * a2) >> 8;
                        g0 = (g0 * (256 - a2) + sp[1] * a2) >> 8;
                        b0 = (b0 * (256 - a2) + sp[2] * a2) >> 8;
                        *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
                    }
            }
    }
}

/* Draw centered wave text with x offset; the wave phase depends on
 * the TARGET position, so entry/exit swings just like the resting state */
static void cr_wave_text(const char *s, double off, double tf)
{
    int wt = cr_text_w(s), i, cph = (int)(tf * 10.0);
    double sc = (wt > 580) ? 580.0 / wt : 1.0;   /* shrink wide lines */
    double bx = (BOOT_TW - wt * sc) / 2.0 + off;
    for (i = 0; s[i]; ++i) {
        double gw = cr_gw((unsigned char)s[i]) * sc;
        double phs = tf * 0.14 + (bx - off + gw / 2) * 0.021;
        int gy = 300 + (int)(sin(phs) * 20.0);
        int fx = 0, fy = 0;
        double fdreh = 0.0;
        if (cr_fluchtF > 0.0) {
            /* every letter knows its path: alternately out left/right,
             * slightly fanned, with tumbling; distance^2 = hesitate
             * first, then plunge - back home the same path */
            double f2 = cr_fluchtF * cr_fluchtF;
            double seite = (i & 1) ? 1.0 : -1.0;
            double faecher = 0.65 + 0.45 * (double)((i * 37) % 13) / 13.0;
            fx = (int)(seite * f2 * 760.0 * faecher);
            fy = (int)(sin((double)i * 1.7 + cr_fluchtF * 4.0) * f2 * 30.0);
            fdreh = seite * f2 * (1.2 + (double)(i % 5) * 0.35);
        }
        cr_glyph_rot((unsigned char)s[i], (int)(bx + gw / 2) + fx,
                     gy + 16 + fy,
                     atan(cos(phs) * 20.0 * 0.021) + fdreh, cph, sc);
        bx += gw + 3 * sc;
    }
}

/* Output a text line with star ornament (star, text, star); a pure
 * star line stays raw */
static void cr_wave_deco(const char *s, double off, double tf)
{
    char buf[80];
    if (s[0] == '*') { cr_wave_text(s, off, tf); return; }
    sprintf(buf, "* %s *", s);
    cr_wave_text(buf, off, tf);
}

/* Flying monster: elliptical path around the logo, small/darker at the
 * back, large in front; 3 wing beat frames (0,1,2,1). In front it casts
 * a soft, semi-transparent shadow on the logo (only on logo pixels).
 * dive: 0 = on the path .. 1 = fully down before the scroll text. */
static void cr_monster_prog(double tf, double *turns_out, int *dir_out,
                            int *hover_out, double *dv_out)
{
    /* Flight program (loop, 46 s): direction +1/-1, lap fraction,
     * then hover in front for 3 s. dive: afterwards a DETOUR down
     * in front of the scroll text (2 s down, 5 s standing - there the
     * floor mirrors him -, 2 s up), then on as usual. Backward
     * segments bring him from LEFT to the front. Lap = 400 frames. */
    static const struct { int dir; double turns; int hover; int dive; } prog[5] = {
        {  1, 0.16, 1, 0 },   /* -> hover in front RIGHT (over P/H)         */
        {  1, 1.20, 1, 1 },   /* Lap, hover in front LEFT, then DIVE DOWN */
        {  1, 0.39, 0, 0 },   /* on to the back side                        */
        { -1, 0.50, 1, 0 },   /* backwards via left -> hover CENTER         */
        { -1, 1.25, 0, 0 } }; /* backward lap (again from the left)         */
    const double LAP = 400.0, HOV = 150.0, DDN = 100.0, DLOW = 250.0;
    double u = tf - 160.0, turns = 0.0, dv = 0.0;
    int k, hover = 0, dir = 1;
    if (u < 0.0) u = 0.0;
    u = fmod(u, 2300.0);                  /* 3.5 laps + 3 hover phases
                                           * + detour (450) */
    for (k = 0; k < 5; ++k) {
        double dur = prog[k].turns * LAP;
        dir = prog[k].dir;
        if (u < dur) { turns += prog[k].dir * prog[k].turns * (u / dur); break; }
        turns += prog[k].dir * prog[k].turns; u -= dur;
        if (prog[k].hover) {
            if (u < HOV) { hover = 1; break; }
            u -= HOV;
        }
        if (prog[k].dive) {               /* down - stand - up, smooth
                                           * (Smoothstep) */
            double e;
            hover = 1;
            if (u < DDN) { e = u / DDN; dv = e * e * (3.0 - 2.0 * e); break; }
            u -= DDN;
            if (u < DLOW) { dv = 1.0; break; }
            u -= DLOW;
            if (u < DDN) { e = 1.0 - u / DDN; dv = e * e * (3.0 - 2.0 * e); break; }
            u -= DDN;
            hover = 0;
        }
    }
    *turns_out = turns; *dir_out = dir; *hover_out = hover; *dv_out = dv;
}

/* Once per frame: derive the escape phase of the scroller letters
 * from the flight program. Diving (dv rises) = departure with dv;
 * standing below = 1.2 s of empty stage, then they return in 3 s,
 * while he watches; surfacing and normal flight = all back home. */
static void cr_flucht_takt(double tf)
{
    double turns, dv;
    int dir, hover;
    cr_monster_prog(tf, &turns, &dir, &hover, &dv);
    if (dv >= 0.999) {
        double tt;
        if (cr_flucht_voll < 0.0) cr_flucht_voll = tf;
        tt = tf - cr_flucht_voll;
        if (tt < 60.0) cr_fluchtF = 1.0;
        else if (tt < 210.0) {
            double e = 1.0 - (tt - 60.0) / 150.0;
            cr_fluchtF = e * e * (3.0 - 2.0 * e);
        } else cr_fluchtF = 0.0;
    } else if (dv <= 0.0) {
        cr_fluchtF = 0.0;
        cr_flucht_voll = -1.0;
    } else if (cr_flucht_voll < 0.0 && dv > cr_flucht_prev) {
        cr_fluchtF = dv;
    } else {
        cr_fluchtF = 0.0;
    }
    cr_flucht_prev = dv;
}

static void cr_monster_pos(double tf, int *cx, int *cy, double *sc, int *front,
                           double *dive);
static int cr_seq_cycle(void);
static void cr_rock_carry(double tf, double *rx, double *ry, double *rsc);

/* Set the finale schedule once (deterministic from text sequence and
 * flight program): cr_tend = first frame after the text sequence in
 * which the demon flies freely on its path (no hover/detour); from
 * there he keeps flying in his direction to the return point behind
 * the logo (turns = 0.75 mod 1), grabs the rock, comes via right to the
 * front (0.75 -> 1.25), holds, lets go. Impact from the fall formula. */
static void cr_finale_init(void)
{
    double t, turns, dv, dist, sc, dive;
    int dir, hover, cx, cy, front;
    if (cr_fin_ready) return;
    t = 40.0 + (double)cr_seq_cycle();
    for (;;) {
        cr_monster_prog(t, &turns, &dir, &hover, &dv);
        if (!hover && dv <= 0.0) break;
        t += 1.0;
    }
    cr_tend = t; cr_turns0 = turns; cr_dir0 = dir;
    dist = (dir > 0) ? fmod(0.75 - turns + 10.0, 1.0) : fmod(turns - 0.75 + 10.0, 1.0);
    cr_tA = dist * 400.0;
    cr_tdrop = cr_tend + cr_tA + CR_FIN_B + CR_FIN_C + CR_FIN_D;
    cr_fin_ready = 1;                     /* before the call: no recursion loop */
    cr_rock_carry(cr_tdrop, &cr_rx0, &cr_ry0, &sc);   /* release point = claws */
    (void)cx; (void)cy; (void)front; (void)dive;
    cr_timp = cr_tdrop + sqrt(2.0 * (331.0 - cr_ry0) / CR_ROCK_G);   /* bottom edge
                                                                   * hits CR_ML */
}

static int cr_finale_done(double tf)
{
    cr_finale_init();
    /* done when everything is wiped (failsafe: 30 s after wipe start,
     * with a recorded path its length + 20 s) */
    return cr_wp == 4 || tf >= cr_timp + CR_WIPE_START + 1500.0
                         + ((CR_WPATH_N > 0) ? (double)CR_WPATH_N : 0.0);
}

static void cr_monster_pos(double tf, int *cx, int *cy, double *sc, int *front,
                           double *dive)
{
    double turns, th, sn, dv = 0.0;
    int hover = 0, dir;
    cr_finale_init();
    if (tf >= cr_tend) {                  /* FINALE path */
        double te = tf - cr_tend;
        if (te < cr_tA)                       turns = cr_turns0 + cr_dir0 * te / 400.0;
        else if (te < cr_tA + CR_FIN_B)       turns = 0.75;   /* behind the logo */
        else if (te < cr_tA + CR_FIN_B + CR_FIN_C)
            turns = 0.75 + 0.5 * (te - cr_tA - CR_FIN_B) / CR_FIN_C;
        else { turns = 1.25; hover = 1; }     /* front, logo mid, hold */
        if (tf >= cr_timp + CR_LAUGH_AT && tf < cr_timp + CR_LAUGH_AT + 100.0) {
            /* laughter: fast bobbing (sample ~2 s) */
            double lt = tf - cr_timp - CR_LAUGH_AT;
            *cy = (int)(sin(lt * 0.9) * 3.0);
            *cx = (int)(sin(lt * 0.6) * 2.0);
        } else *cx = *cy = 0;
    } else {
        cr_monster_prog(tf, &turns, &dir, &hover, &dv);
        *cx = *cy = 0;
    }
    th = turns * 6.283185307179586;     /* exact: near sn = 1.0, sc = 1.0 */
    sn = sin(th);
    /* (+=: cx and cy may already carry the finale's laugh bob) */
    *cx += (int)(320.0 + 290.0 * cos(th));  /* side point lies BESIDE the logo:
                                             * switch front/back without overlap */
    *cy += (int)(118.0 + 34.0 * sn);  /* lowest point stays well above the scroll text */
    *sc = 0.50 + 0.50 * (sn + 1.0) * 0.5;             /* 0.5 back .. 1.0 front
                                                        * (source = maximum size) */
    *front = (sn > 0.0);
    if (dv > 0.0) {                       /* detour: straight from the path
                                           * point to the low point (center,
                                           * feet just ABOVE the mirror edge:
                                           * the bob (+-3 px) must not push
                                           * them into the floor) */
        *cx = (int)(*cx + (320.0 - *cx) * dv);
        *cy = (int)(*cy + ((double)(CR_ML - MN_H / 2 - 3) - *cy) * dv);
        *sc += (1.0 - *sc) * dv;
        *front = 1;
    }
    if (hover) {                           /* hovers: slight up-and-down */
        *cy += (int)(sin(tf * 0.08) * 4.0);
        *cx += (int)(sin(tf * 0.05) * 3.0);
    }
    *dive = dv;
}

/* Blit RGBA sprite scaled bilinear/premultiplied (like the
 * demon); mirror=1: flipped below the mirror edge, fading with
 * depth (for the falling rock above the floor). */
static void cr_blit_rgba(const unsigned char *img, int iw, int ih, double cxr,
                         double cyr, double sc, int mirror, int am)
{
    int w = (int)(iw * sc), h = (int)(ih * sc), x, y;
    int x0 = (int)(cxr - w / 2.0), y0 = (int)(cyr - h / 2.0);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            int px = x0 + x, py = y0 + y, ix, iy, k, yd = py;
            double u = (x + 0.5) / sc - 0.5, v = (y + 0.5) / sc - 0.5;
            double fx, fy, rs = 0.0, gs = 0.0, bs = 0.0, as = 0.0;
            unsigned long fa, r0, g0, b0, cr, cg, cb;
            Uint32 *d;
            if (px < 0 || px >= BOOT_TW) continue;
            if (mirror) {
                int depth;
                if (py >= CR_ML) continue;
                yd = 2 * CR_ML - py; depth = yd - CR_ML;
                if (depth >= 42 || yd >= BOOT_TH) continue;
            } else if (py < 0 || py >= BOOT_TH) continue;
            ix = (int)floor(u); iy = (int)floor(v); fx = u - ix; fy = v - iy;
            for (k = 0; k < 4; ++k) {
                int sx = ix + (k & 1), sy = iy + (k >> 1);
                double wgt = ((k & 1) ? fx : 1.0 - fx) * ((k >> 1) ? fy : 1.0 - fy);
                const unsigned char *p;
                if (sx < 0 || sx >= iw || sy < 0 || sy >= ih) continue;
                p = img + (sy * iw + sx) * 4;
                rs += p[0] * p[3] * wgt; gs += p[1] * p[3] * wgt;
                bs += p[2] * p[3] * wgt; as += p[3] * wgt;
            }
            if (as <= 0.5) continue;
            cr = (unsigned long)(rs / as); cg = (unsigned long)(gs / as);
            cb = (unsigned long)(bs / as);
            fa = (unsigned long)as;
            if (fa > 255) fa = 255;
            if (mirror) {
                fa = fa * (unsigned long)(42 - (yd - CR_ML)) / 42UL;
                fa = fa * 120UL >> 8;
            }
            fa = fa * (unsigned long)am >> 8;   /* Overall opacity (fade in) */
            if (!fa) continue;
            fa += fa >> 7;
            d = &bpix[yd * BOOT_TW + px];
            r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
            r0 = (r0 * (256 - fa) + cr * fa) >> 8;
            g0 = (g0 * (256 - fa) + cg * fa) >> 8;
            b0 = (b0 * (256 - fa) + cb * fa) >> 8;
            *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
        }
}

/* Rock in the foot claws: tied to the FOOT HEIGHT of the current
 * wing beat frame (feet end at line 148 / 151 / 144 of the three
 * frames), so it moves up and down with the claws while hovering */
static void cr_rock_carry(double tf, double *rx, double *ry, double *rsc)
{
    static const int foot[3] = { 148, 151, 144 };
    int cx, cy, front, fr = ((int)(tf / 8.0)) & 3;
    double sc, dive;
    if (fr == 3) fr = 1;                  /* 0,1,2,1 as cr_monster_draw */
    cr_monster_pos(tf, &cx, &cy, &sc, &front, &dive);
    *rx = cx + 2.0;                       /* half foot-height motion: subtle */
    *ry = cy + (95.0 + 0.5 * (double)(foot[fr] - 151)) * sc;
    *rsc = sc;
}

/* Rock: in the claws (from phase C), then free-falling. 1 = visible */
static int cr_rock_pos(double tf, double *rx, double *ry, double *rsc)
{
    double te;
    cr_finale_init();
    if (tf < cr_tend + cr_tA + CR_FIN_B) return 0;
    if (tf < cr_tdrop) { cr_rock_carry(tf, rx, ry, rsc); return 1; }
    te = tf - cr_tdrop;
    *rx = cr_rx0; *ry = cr_ry0 + 0.5 * CR_ROCK_G * te * te; *rsc = 1.0;
    return (*ry < BOOT_TH + 40.0);
}

/* Rock fades in behind the logo over 30 frames (the logo has a
 * small hole there - no popping in) */
static int cr_rock_alpha(double tf)
{
    double a = (tf - (cr_tend + cr_tA + CR_FIN_B)) * 256.0 / 30.0;
    return (a >= 256.0) ? 256 : (a <= 0.0) ? 0 : (int)a;
}

/* Pseudo-random 0..1 from index (deterministic, same headless) */
static double cr_rnd(unsigned long k)
{
    k ^= k >> 13; k = (k * 0x5BD1E995UL) & 0xFFFFFFFFUL; k ^= k >> 15;
    k = (k * 0x27D4EB2FUL) & 0xFFFFFFFFUL; k ^= k >> 12;
    return (double)(k & 0xFFFFUL) / 65536.0;
}

/* Mirror break: freeze the mirror strip (lines CR_ML..399) and split
 * it into Voronoi cells around random seeds, dense around the impact
 * point (small shards), coarser outward. Each shard gets a radial
 * velocity (near fast, far slow), an upward impulse and a rotation;
 * flight is analytic (no integration). */
static void cr_shatter_init(void)
{
    static int sxs[CR_NSH], sys[CR_NSH];
    static long cnt[CR_NSH], sumx[CR_NSH], sumy[CR_NSH];
    int i, x, y;
    memcpy(cr_snap, bpix + CR_ML * BOOT_TW, sizeof(cr_snap));
    for (i = 0; i < CR_NSH; ++i) {
        double r1 = cr_rnd(i * 7UL + 1), r2 = cr_rnd(i * 7UL + 2), r3 = cr_rnd(i * 7UL + 3);
        int sx = 320 + (int)((r1 + r2 - 1.0) * 330.0);
        if (sx < 2) sx = 2;
        if (sx > BOOT_TW - 3) sx = BOOT_TW - 3;
        sxs[i] = sx; sys[i] = (int)(r3 * (BOOT_TH - CR_ML - 1));
        cnt[i] = sumx[i] = sumy[i] = 0;
        cr_sh[i].x0 = BOOT_TW; cr_sh[i].y0 = BOOT_TH; cr_sh[i].x1 = -1; cr_sh[i].y1 = -1;
    }
    for (y = 0; y < BOOT_TH - CR_ML; ++y)
        for (x = 0; x < BOOT_TW; ++x) {
            long best = 0x7FFFFFFFL; int bi = 0;
            for (i = 0; i < CR_NSH; ++i) {
                long dx = x - sxs[i], dy = (y - sys[i]) * 3;   /* Cells wider than
                                                               * tall */
                long d = dx * dx + dy * dy;
                if (d < best) { best = d; bi = i; }
            }
            cr_shid[y * BOOT_TW + x] = (unsigned char)bi;
            ++cnt[bi]; sumx[bi] += x; sumy[bi] += y;
            if (x < cr_sh[bi].x0) cr_sh[bi].x0 = x;
            if (x > cr_sh[bi].x1) cr_sh[bi].x1 = x;
            if (y < cr_sh[bi].y0) cr_sh[bi].y0 = y;
            if (y > cr_sh[bi].y1) cr_sh[bi].y1 = y;
        }
    /* Cracks: darken cell borders in the snapshot - a crack pattern
     * already in the first break frame, not only once the shards
     * drift apart */
    for (y = 0; y < BOOT_TH - CR_ML; ++y)
        for (x = 0; x < BOOT_TW; ++x) {
            int id = cr_shid[y * BOOT_TW + x], edge = 0;
            if (x + 1 < BOOT_TW && cr_shid[y * BOOT_TW + x + 1] != id) edge = 1;
            if (y + 1 < BOOT_TH - CR_ML && cr_shid[(y + 1) * BOOT_TW + x] != id) edge = 1;
            if (edge) {
                Uint32 c = cr_snap[y * BOOT_TW + x];
                cr_snap[y * BOOT_TW + x] = 0xFF000000UL
                    | ((((c >> 16) & 0xFF) * 70UL >> 8) << 16)
                    | ((((c >> 8) & 0xFF) * 70UL >> 8) << 8)
                    | (((c & 0xFF) * 70UL) >> 8);
            }
        }
    for (i = 0; i < CR_NSH; ++i) {
        double dx, dy, dist, speed, r4 = cr_rnd(i * 7UL + 4), r5 = cr_rnd(i * 7UL + 5),
               r6 = cr_rnd(i * 7UL + 6);
        if (cnt[i] == 0) continue;
        cr_sh[i].mx = (double)sumx[i] / cnt[i];
        cr_sh[i].my = (double)sumy[i] / cnt[i] + CR_ML;
        dx = cr_sh[i].mx - cr_rx0; dy = cr_sh[i].my - (double)CR_ML;
        dist = sqrt(dx * dx + dy * dy);
        if (dist < 1.0) dist = 1.0;
        speed = 0.5 + 4.0 * exp(-dist / 160.0);
        cr_sh[i].vx = dx / dist * speed * (0.7 + 0.6 * r4);
        cr_sh[i].vy = -(1.0 + 4.0 * r5) * exp(-dist / 220.0) + dy / dist * speed * 0.25;
        cr_sh[i].w  = (r6 - 0.5) * 0.10;
    }
    cr_broken = 1;
    sound_play(SND_GLASS);
    sound_mod_fade(150);                  /* Music goes with the mirror */
}

static void cr_shards_draw(double tf)
{
    double t = tf - cr_timp, al, flash, cs, sn;
    int i;
    if (t < 0.0) return;
    al = (t < 50.0) ? 1.0 : 1.0 - (t - 50.0) / 110.0;
    if (al <= 0.0) return;
    flash = (t < 6.0) ? (6.0 - t) / 6.0 : 0.0;
    for (i = 0; i < CR_NSH; ++i) {
        double px0, py0, a, c[4][2], bx0, by0, bx1, by1;
        int k, x, y;
        unsigned long fa = (unsigned long)(al * 256.0), fl = (unsigned long)(flash * 180.0);
        if (cr_sh[i].x1 < 0) continue;
        px0 = cr_sh[i].mx + cr_sh[i].vx * t;
        py0 = cr_sh[i].my + cr_sh[i].vy * t + 0.5 * 0.05 * t * t;   /* gentle
                                                                 * gravity:
                                                                 * shards stay
                                                                 * ~2 s in view */
        a = cr_sh[i].w * t; cs = cos(a); sn = sin(a);
        /* Target box: source box rotated about centroid */
        for (k = 0; k < 4; ++k) {
            double qx = ((k & 1) ? cr_sh[i].x1 + 1 : cr_sh[i].x0) - cr_sh[i].mx;
            double qy = ((k >> 1) ? cr_sh[i].y1 + 1 : cr_sh[i].y0) + CR_ML - cr_sh[i].my;
            c[k][0] = px0 + qx * cs - qy * sn;
            c[k][1] = py0 + qx * sn + qy * cs;
        }
        bx0 = bx1 = c[0][0]; by0 = by1 = c[0][1];
        for (k = 1; k < 4; ++k) {
            if (c[k][0] < bx0) bx0 = c[k][0]; if (c[k][0] > bx1) bx1 = c[k][0];
            if (c[k][1] < by0) by0 = c[k][1]; if (c[k][1] > by1) by1 = c[k][1];
        }
        if (bx1 < 0 || by1 < 0 || bx0 >= BOOT_TW || by0 >= BOOT_TH) continue;
        for (y = (int)floor(by0) - 1; y <= (int)ceil(by1) + 1; ++y)
            for (x = (int)floor(bx0) - 1; x <= (int)ceil(bx1) + 1; ++x) {
                double dx, dy, sx, sy;
                int isx, isy;
                Uint32 c0, *d;
                unsigned long r0, g0, b0, cr, cg, cb;
                if (x < 0 || x >= BOOT_TW || y < 0 || y >= BOOT_TH) continue;
                dx = x + 0.5 - px0; dy = y + 0.5 - py0;
                sx = cr_sh[i].mx + dx * cs + dy * sn;          /* inverse rotate */
                sy = cr_sh[i].my - dx * sn + dy * cs - CR_ML;
                isx = (int)floor(sx); isy = (int)floor(sy);
                if (isx < 0 || isx >= BOOT_TW || isy < 0 || isy >= BOOT_TH - CR_ML) continue;
                if (cr_shid[isy * BOOT_TW + isx] != i) continue;
                c0 = cr_snap[isy * BOOT_TW + isx];
                cr = (c0 >> 16) & 0xFF; cg = (c0 >> 8) & 0xFF; cb = c0 & 0xFF;
                cr += ((255 - cr) * fl) >> 8; cg += ((255 - cg) * fl) >> 8;
                cb += ((255 - cb) * fl) >> 8;
                d = &bpix[y * BOOT_TW + x];
                r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
                r0 = (r0 * (256 - fa) + cr * fa) >> 8;
                g0 = (g0 * (256 - fa) + cg * fa) >> 8;
                b0 = (b0 * (256 - fa) + cb * fa) >> 8;
                *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
            }
    }
}

/* mode: 0 = sprite (ends at the mirror edge), 1 = shadow on the logo,
 * 2 = floor reflection (flipped below CR_ML, like the mirror text) */
static void cr_monster_draw(double tf, int mode)
{
    int cx, cy, front, fr, x, y, a, cph = (int)(tf * 10.0);
    double sc, d01, dive;
    const unsigned char *img;
    int w, h, x0, y0;
    if (tf < 160.0) return;
    cr_monster_pos(tf, &cx, &cy, &sc, &front, &dive);
    a = (int)((tf - 160.0) * 8.0); if (a > 256) a = 256;   /* fade in */
    d01 = (sc - 0.5) / 0.5;               /* 0 = far back .. 1 = far front */
    fr = ((int)(tf / 8.0)) & 3; if (fr == 3) fr = 1;       /* 0,1,2,1 */
    img = mn_rgba[fr];
    w = (int)(MN_W * sc); h = (int)(MN_H * sc);
    x0 = cx - w / 2; y0 = cy - h / 2;
    if (mode == 2 && y0 + h < CR_ML - 84) return;   /* too far above the floor */
    if (mode == 1) {                      /* Shadow: offset, only onto the logo,
                                           * BIGGER than the sprite (light source
                                           * close) and soft */
        double ss = sc * 1.3;             /* Shadow silhouette 30% bigger */
        int ws = (int)(MN_W * ss), hs = (int)(MN_H * ss);
        int xs0 = cx - ws / 2 + 24, ys0 = cy - hs / 2 + 34;
        if (!front) return;
        for (y = 0; y < hs; ++y)
            for (x = 0; x < ws; ++x) {
                int px = xs0 + x, py = ys0 + y;
                int lx = px - 110, ly = py - 4, k, sa = 0;
                unsigned long la, fa, r0, g0, b0;
                Uint32 *d;
                if (lx < 0 || lx >= CL_W || ly < 0 || ly >= CL_H) continue;
                la = cl_rgba[(ly * CL_W + lx) * 4 + 3];
                if (la < 128) continue;
                /* soft edge: 5x5 mean of the sprite alpha */
                for (k = 0; k < 25; ++k) {
                    int sx = (int)((x + (k % 5) - 2) / ss), sy = (int)((y + k / 5 - 2) / ss);
                    if (sx >= 0 && sx < MN_W && sy >= 0 && sy < MN_H)
                        sa += img[(sy * MN_W + sx) * 4 + 3];
                }
                fa = (unsigned long)(sa / 25) * 150UL >> 8;       /* max ~58% */
                fa = fa * (unsigned long)a >> 8;
                /* Shadow fades out toward the side point (no popping)
                 * and on the downward detour (below the logo no shadow
                 * falls onto the logo any more) */
                fa = fa * (unsigned long)(d01 > 0.5 ? (d01 - 0.5) * 512.0 : 0.0) >> 8;
                fa = fa * (unsigned long)((1.0 - dive) * 256.0) >> 8;
                if (!fa) continue;
                d = &bpix[py * BOOT_TW + px];
                r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
                r0 = r0 * (256 - fa) >> 8; g0 = g0 * (256 - fa) >> 8; b0 = b0 * (256 - fa) >> 8;
                *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
            }
        return;
    }
    /* Sprite: sample BILINEAR with premultiplied alpha - with a
     * continuously changing scale, bright details (eyes, teeth)
     * would jitter, as Nearest hits other texels each frame */
    /* Reflection (mode 2): the floor is only 42 lines deep, but
     * the sprite is 152 - 1:1 showed only the dark blue legs
     * (invisible on the blue floor). So squashed 2:1: the lower
     * ~84 lines (legs, belly, jaw) appear flipped below the edge,
     * fading with depth, same ripple as the mirror text */
    for (y = 0; y < ((mode == 2) ? BOOT_TH - CR_ML : h); ++y) {
        int ys = y, depth = 0;
        unsigned long rf = 256;
        if (mode == 2) {
            depth = y;
            ys = CR_ML - 1 - 2 * depth - y0;        /* sprite source line */
            if (ys < 0 || ys >= h) continue;
            rf = (unsigned long)(200.0 * pow(1.0 - depth / 42.0, 0.7));
        }
        for (x = 0; x < w; ++x) {
            int px = x0 + x, py = y0 + ys, ix, iy, k, xd = px, yd = py;
            double u = (x + 0.5) / sc - 0.5, v = (ys + 0.5) / sc - 0.5;
            double fx, fy, rs = 0.0, gs = 0.0, bs = 0.0, as = 0.0;
            unsigned long fa, r0, g0, b0, dim, cr, cg, cb;
            Uint32 *d;
            /* nothing flies into the mirror: sprite ends at the edge */
            if (px < 0 || px >= BOOT_TW || py < 0 || py >= CR_ML) continue;
            if (mode == 2) {
                yd = CR_ML + depth;
                xd = px + (int)(sin(yd * 0.35 + cph * 0.03) * 2.0);
                if (xd < 0 || xd >= BOOT_TW) continue;
            }
            ix = (int)floor(u); iy = (int)floor(v); fx = u - ix; fy = v - iy;
            for (k = 0; k < 4; ++k) {
                int sx = ix + (k & 1), sy = iy + (k >> 1);
                double wgt = ((k & 1) ? fx : 1.0 - fx) * ((k >> 1) ? fy : 1.0 - fy);
                const unsigned char *p;
                if (sx < 0 || sx >= MN_W || sy < 0 || sy >= MN_H) continue;
                p = img + (sy * MN_W + sx) * 4;
                rs += p[0] * p[3] * wgt; gs += p[1] * p[3] * wgt;
                bs += p[2] * p[3] * wgt; as += p[3] * wgt;
            }
            if (as <= 0.5) continue;
            cr = (unsigned long)(rs / as); cg = (unsigned long)(gs / as);
            cb = (unsigned long)(bs / as);
            fa = (unsigned long)as * (unsigned long)a >> 8;
            fa = fa * rf >> 8;            /* mirror: depth fade */
            if (!fa) continue;
            dim = 150UL + (unsigned long)(106.0 * d01);   /* Depth: continuously
                                                          * light (front) -> dark */
            d = &bpix[yd * BOOT_TW + xd];
            r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
            r0 = (r0 * (256 - fa) + ((cr * dim >> 8)) * fa) >> 8;
            g0 = (g0 * (256 - fa) + ((cg * dim >> 8)) * fa) >> 8;
            b0 = (b0 * (256 - fa) + ((cb * dim >> 8)) * fa) >> 8;
            *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
        }
    }
}

/* Credits text sequence (once, no loop): display time per line */
static const struct { const char *s; int hold; } cr_seq[] = {
    { "CREDITS:",           250 },
    { "RED SECTOR, SANITY", 150 },
    { "THE RETRO GUYS",     150 },
    { "SANITY, SILENTS",    150 },
    { "SCOOPEX, PARANOIMIA", 150 },
    { "TRISTAR, LEMON",     150 },
    { "SYNTHETIC DEVELOPMENT", 150 },
    { "DOMY FOR C TRANSLATION", 150 },
    { "DOMY FOR MODPLAYER", 150 },
    { "DOMY FOR WEBASSEMBLY", 150 },
    { "4MAT FOR MUSIC",     150 },
    { "MIDWAY FOR ORIGINAL", 150 },
    { "COMMODORE FOR GORF-C64", 150 },
    { "CLAUDE FOR CODING",  150 },
    { "****************",    150 } };      /* star line (star glyphs) */
#define CR_NSEQ ((int)(sizeof(cr_seq) / sizeof(cr_seq[0])))
#define CR_SEQ_E 150                          /* entry/exit frames */
#define CR_SEQ_V 5                            /* px/frame */
static int cr_seq_cycle(void)
{
    int k, c = 0;
    for (k = 0; k < CR_NSEQ; ++k) c += CR_SEQ_E + cr_seq[k].hold;
    return c;
}

/* Fade in the scroll text layer. On the detour the demon acts like
 * a GLASS BALL (sphere effect, user request: "the scroller wells
 * up around the demon / seeks a way out"): inside a circle around
 * him the layer is sampled radially magnified (center up to 2x,
 * edge seamlessly 1:1); strength grows with dive. Below the mirror
 * edge the same with the mirrored ball center. The demon himself
 * stands BEHIND the distorted text (user: "that comes off better").
 * In the ball the type shimmers REDDISH (green/blue damped, red
 * raised), becomes clearly translucent (up to ~40% opacity at the
 * center) and gets MOTION BLUR: the layer is averaged along the
 * scroll direction (x) over up to +-8 px, the trail grows toward
 * the ball center - all with the same envelope as the distortion,
 * hence seamless at the ball edge. */
static void cr_text_composite(double tf)
{
    int cx, cy, front, x, y;
    double sc, dive, rad, rad2, amp;
    cr_monster_pos(tf, &cx, &cy, &sc, &front, &dive);
    if (tf < 160.0 || !front) dive = 0.0;
    rad = 118.0 * sc; rad2 = rad * rad;
    amp = 0.5 * dive;
    for (y = CR_TY0; y < BOOT_TH; ++y) {
        int ccy = (y < CR_ML) ? cy : 2 * CR_ML - cy;   /* ball / mirror ball */
        for (x = 0; x < BOOT_TW; ++x) {
            double sx = x, sy = y, r2 = rad2;
            unsigned long al, cr, cg, cb, r0, g0, b0;
            Uint32 *d;
            if (amp > 0.0) {
                double dx = x - cx, dy = y - ccy;
                r2 = dx * dx + dy * dy;
                if (r2 < rad2) {
                    double g = 1.0 - amp * (1.0 - r2 / rad2);
                    sx = cx + dx * g; sy = ccy + dy * g;
                }
            }
            if (r2 >= rad2) {             /* outside the ball: 1:1 */
                Uint32 c = cr_tlayer[y * BOOT_TW + x];
                al = c >> 24;
                if (!al) continue;
                cr = (c >> 16) & 0xFF; cg = (c >> 8) & 0xFF; cb = c & 0xFF;
            } else {                      /* in the ball: bilinear, alpha-
                                           * weighted, averaged along x
                                           * over the blur trail */
                double e = (1.0 - r2 / rad2) * dive;   /* 0 rim .. 1 center */
                int taps = 1 + (int)(e * 8.0), t;      /* 1..9 samples */
                double as = 0.0, rs = 0.0, gs = 0.0, bs = 0.0, wsum = 0.0;
                for (t = 0; t < taps; ++t) {
                    /* trail symmetric about the lens position, step 2px */
                    double ox = (taps > 1) ? (t - (taps - 1) * 0.5) * 2.0 : 0.0;
                    double lx = sx + ox;
                    int ix = (int)floor(lx), iy = (int)floor(sy), k;
                    double fx = lx - ix, fy = sy - iy;
                    wsum += 1.0;
                    for (k = 0; k < 4; ++k) {
                        int tx = ix + (k & 1), ty = iy + (k >> 1);
                        double w = ((k & 1) ? fx : 1.0 - fx) * ((k >> 1) ? fy : 1.0 - fy);
                        Uint32 c;
                        double ca;
                        if (tx < 0 || tx >= BOOT_TW || ty < 0 || ty >= BOOT_TH) continue;
                        c = cr_tlayer[ty * BOOT_TW + tx];
                        ca = (double)(c >> 24) * w;
                        if (ca <= 0.0) continue;
                        as += ca;
                        rs += ((c >> 16) & 0xFF) * ca;
                        gs += ((c >> 8) & 0xFF) * ca;
                        bs += (c & 0xFF) * ca;
                    }
                }
                if (as < 0.5) continue;
                cr = (unsigned long)(rs / as); cg = (unsigned long)(gs / as);
                cb = (unsigned long)(bs / as);
                al = (unsigned long)(as / wsum);        /* blur mean */
                if (al > 255) al = 255;
                {   /* glass: reddish shimmer + transparency */
                    unsigned long ei = (unsigned long)(e * 256.0);
                    cr = cr + (((255 - cr) * ei * 120UL) >> 16);  /* red up */
                    cg = cg * (65536UL - ei * 140UL) >> 16;       /* green down */
                    cb = cb * (65536UL - ei * 160UL) >> 16;       /* blue down */
                    al = al * (256UL - ei * 155UL / 256UL) >> 8;  /* max ~40% */
                }
            }
            if (al > 255) al = 255;
            al += al >> 7;                /* 0..255 -> 0..256 */
            d = &bpix[y * BOOT_TW + x];
            r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
            r0 = (r0 * (256 - al) + cr * al) >> 8;
            g0 = (g0 * (256 - al) + cg * al) >> 8;
            b0 = (b0 * (256 - al) + cb * al) >> 8;
            *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
        }
    }
}

static double cr_smooth(double u)
{
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    return u * u * (3.0 - 2.0 * u);
}

/* Stamp the sponge footprint at (sx,sy) into the wipe mask: the FULL
 * sponge area (including the part hidden by the fingers) as an ellipse
 * over the sprite's sponge bbox (AR_SPEX/Y, AR_SPRX/Y from the
 * header), flattened a touch bigger.
 * PIXELY: quantized to a 4x4 grid (whole cells whose center lies
 * in the ellipse) - it is pixel art, not a soft edge (user) */
#define CR_WCELL 4
static void cr_wipe_stamp(double sx, double sy)
{
    const double ex = sx - AR_SPCX + AR_SPEX, ey = sy - AR_SPCY + AR_SPEY;
    const double rx = AR_SPRX, ry = AR_SPRY;
    int x, y, i, j;
    for (y = ((int)(ey - ry) / CR_WCELL) * CR_WCELL; y <= (int)(ey + ry); y += CR_WCELL) {
        double dy = (y + CR_WCELL * 0.5 - ey) / ry;
        if (y < 0 || y >= BOOT_TH || dy * dy > 1.0) continue;
        for (x = ((int)(ex - rx) / CR_WCELL) * CR_WCELL; x <= (int)(ex + rx); x += CR_WCELL) {
            double dx = (x + CR_WCELL * 0.5 - ex) / rx;
            if (x < 0 || x >= BOOT_TW || dx * dx + dy * dy > 1.0) continue;
            for (j = 0; j < CR_WCELL && y + j < BOOT_TH; ++j)
                for (i = 0; i < CR_WCELL && x + i < BOOT_TW; ++i)
                    cr_wmask[(y + j) * BOOT_TW + x + i] = 255;
        }
    }
}

/* Find the nearest unwiped pixel to (px,py) (grid 4, then a fine
 * search for edges): 1 = found. Human-like: first take whatever
 * is left nearby */
/* Reservation: the deliberately spared monster piece (head and upper
 * body of the demon, at the front in the logo center). The strokes
 * wipe AROUND it (keep-out zone = reservation + sponge radius +
 * scrub amplitude for the sponge center: x 138..442, y 26..259); the
 * touch-up pass first wipes everything outside the keep-out zone, the
 * monster and its edge last of all */
#define CR_RES_X0 190
#define CR_RES_X1 390
#define CR_RES_Y0 80
#define CR_RES_Y1 205
#define CR_SZ_X0 (CR_RES_X0 - 52)
#define CR_SZ_X1 (CR_RES_X1 + 52)
#define CR_SZ_Y0 (CR_RES_Y0 - 54)
#define CR_SZ_Y1 (CR_RES_Y1 + 54)
static int cr_in_zone(double x, double y)
{
    return x > CR_SZ_X0 && x < CR_SZ_X1 && y > CR_SZ_Y0 && y < CR_SZ_Y1;
}
/* "Dirty" = unwiped AND visible in the freeze frame (not near-black):
 * empty space does not need cleaning */
static int cr_dirty_px(int x, int y)
{
    Uint32 c;
    if (cr_wmask[y * BOOT_TW + x]) return 0;
    c = cr_freeze[y * BOOT_TW + x];
    return (((c >> 16) & 0xFF) | ((c >> 8) & 0xFF) | (c & 0xFF)) >= 28;
}
static int cr_wipe_dirty(double px, double py, int skip_res, int *tx, int *ty)
{
    int x, y, found = 0;
    double best = 1e18;
    for (y = 0; y < BOOT_TH; y += 4)
        for (x = 0; x < BOOT_TW; x += 4) {
            if (!cr_dirty_px(x, y)) continue;
            if (skip_res && cr_in_zone(x, y)) continue;
            {
                double d = (x - px) * (x - px) + (y - py) * (y - py);
                if (d < best) { best = d; *tx = x; *ty = y; found = 1; }
            }
        }
    if (found) return 1;
    for (y = 0; y < BOOT_TH; ++y)
        for (x = 0; x < BOOT_TW; ++x) {
            if (!cr_dirty_px(x, y)) continue;
            if (skip_res && cr_in_zone(x, y)) continue;
            *tx = x; *ty = y; return 1;
        }
    return 0;
}

/* Wipe plan: strokes (x0,y0)->(x1,y1); vert = vertical stroke (smaller
 * sideways amplitude, so the columns cover with no gaps); detour =
 * swing UPWARD over the monster (stroke 1). Order: two paths along
 * the top, three columns left, traverse below the monster, four
 * columns right, two paths along the bottom - "back and forth, then
 * top to bottom" (user request) */
static const struct { short x0, y0, x1, y1, vert, detour; } cr_wplan[12] = {
    {  30,  25, 610,  25, 0, 0 },
    { 610,  75,  30,  75, 0, 1 },
    {  30,  75,  30, 275, 1, 0 },
    {  80, 275,  80,  60, 1, 0 },
    { 130,  60, 130, 275, 1, 0 },
    { 130, 275, 460, 275, 0, 0 },
    { 460, 275, 460,  60, 1, 0 },
    { 510,  60, 510, 275, 1, 0 },
    { 560, 275, 560,  60, 1, 0 },
    { 610,  60, 610, 275, 1, 0 },
    { 610, 325,  30, 325, 0, 0 },
    {  30, 375, 610, 375, 0, 0 } };
#define CR_NWPLAN 12

/* Advance the wiper one time step, apply mask, draw arm.
 * Human-like: stroke speed varies (10..17 px/frame), strokes alternate
 * direction and orientation (plan above), stroke 1 swings up over the
 * monster, touch-up paths dodge the keep-out zone.
 * Wipe sound very, very subtle (sound_wipe: base level on contact
 * + speed share, sluggish). No squeak (user decision). */
static void cr_wipe_step(double tf, double dt)
{
    const double T_IN = 40.0;
    double ax, ay, cx, cy, axmax, aymax;
    int i, tx, ty, wiping;
    if (cr_wp < 0) {
        if (tf < cr_timp + CR_WIPE_START) { sound_wipe(0); return; }
        cr_wp = 0; cr_wt = 0.0; cr_wy = 460.0; cr_wcirc = 0.0;
        cr_wx = (CR_WPATH_N > 0) ? (double)cr_wpath[0][0] : (double)cr_wplan[0].x0;
        cr_wband = 0; cr_wtarget = -1; cr_wdown = 0;
    }
    if (cr_wp != 4) cr_wt += dt;
    /* Target amplitude per stroke type: horizontal 18/14, vert 6/14
     * (column core 68-12=56 >= 50) - smoothly tracked */
    axmax = (cr_wp == 1 && cr_wplan[cr_wband].vert) ? 6.0 : 18.0;
    aymax = 14.0;
    cr_wax += (axmax - cr_wax) * (dt / 8.0 > 1.0 ? 1.0 : dt / 8.0);
    cr_way += (aymax - cr_way) * (dt / 8.0 > 1.0 ? 1.0 : dt / 8.0);
    switch (cr_wp) {
    case 0: {                             /* Entry from below to the first
                                           * start point */
        double y0 = (CR_WPATH_N > 0) ? (double)cr_wpath[0][1] : (double)cr_wplan[0].y0;
        cr_wy = 460.0 + (y0 - 460.0) * cr_smooth(cr_wt / T_IN);
        if (cr_wt >= T_IN) {
            double len = fabs((double)cr_wplan[0].x1 - cr_wplan[0].x0);
            cr_wt = 0.0;
            if (CR_WPATH_N > 0) { cr_wp = 5; }
            else { cr_wp = 1; cr_wdur = len / (10.0 + 7.0 * cr_rnd(900UL)); }
        }
        break; }
    case 5: {                             /* RECORDED path (wiperec):
                                           * position linear between the
                                           * 50 Hz sample points, sponge
                                           * wipes only when it was down */
        int i0 = (int)cr_wt;
        double fr = cr_wt - i0;
        /* Path finished: NO touch-up pass (user: "if 2 pixels remain,
         * do not wipe yourself") - straight to fade out/title */
        if (i0 >= CR_WPATH_N - 1) { cr_wp = 4; cr_wt = 0.0; cr_wdown = 0; break; }
        cr_wx = cr_wpath[i0][0] + (cr_wpath[i0 + 1][0] - cr_wpath[i0][0]) * fr;
        cr_wy = cr_wpath[i0][1] + (cr_wpath[i0 + 1][1] - cr_wpath[i0][1]) * fr;
        cr_wdown = cr_wpath[i0][2];
        break; }
    case 1: {                             /* Stroke, eased at the ends */
        double u = cr_smooth(cr_wt / cr_wdur);
        cr_wx = cr_wplan[cr_wband].x0 + (cr_wplan[cr_wband].x1 - cr_wplan[cr_wband].x0) * u;
        cr_wy = cr_wplan[cr_wband].y0 + (cr_wplan[cr_wband].y1 - cr_wplan[cr_wband].y0) * u;
        if (cr_wplan[cr_wband].detour) {
            /* swing upward over the monster: ramp 110 px before
             * the keep-out zone, above at y = keep-out top edge */
            double b = 0.0;
            if (cr_wx >= CR_SZ_X0 && cr_wx <= CR_SZ_X1) b = 1.0;
            else if (cr_wx > CR_SZ_X0 - 110.0 && cr_wx < CR_SZ_X0)
                b = cr_smooth((cr_wx - (CR_SZ_X0 - 110.0)) / 110.0);
            else if (cr_wx > CR_SZ_X1 && cr_wx < CR_SZ_X1 + 110.0)
                b = cr_smooth((CR_SZ_X1 + 110.0 - cr_wx) / 110.0);
            cr_wy += ((double)CR_SZ_Y0 - cr_wy) * b;
        }
        if (cr_wt >= cr_wdur) {
            cr_wt = 0.0;
            if (cr_wband + 1 < CR_NWPLAN) cr_wp = 2; else cr_wp = 3;
        }
        break; }
    case 2: {                             /* Transition: end -> start of
                                           * the next stroke (wipes too) */
        double xa = cr_wplan[cr_wband].x1, ya = cr_wplan[cr_wband].y1;
        double xb = cr_wplan[cr_wband + 1].x0, yb = cr_wplan[cr_wband + 1].y0;
        double d = sqrt((xb - xa) * (xb - xa) + (yb - ya) * (yb - ya));
        double tt = 6.0 + d / 14.0 + 4.0 * cr_rnd(910UL + cr_wband), u = cr_smooth(cr_wt / tt);
        cr_wx = xa + (xb - xa) * u; cr_wy = ya + (yb - ya) * u;
        if (cr_wt >= tt) {
            double len;
            ++cr_wband; cr_wp = 1; cr_wt = 0.0;
            len = sqrt((double)(cr_wplan[cr_wband].x1 - cr_wplan[cr_wband].x0) *
                       (cr_wplan[cr_wband].x1 - cr_wplan[cr_wband].x0) +
                       (double)(cr_wplan[cr_wband].y1 - cr_wplan[cr_wband].y0) *
                       (cr_wplan[cr_wband].y1 - cr_wplan[cr_wband].y0));
            cr_wdur = len / (10.0 + 7.0 * cr_rnd(920UL + cr_wband));
            if (cr_wdur < 12.0) cr_wdur = 12.0;
        }
        break; }
    case 3: {                             /* Touch-up pass: first everything out-
                                           * side the keep-out zone, then the
                                           * monster; next target first */
        int outside = cr_wipe_dirty(cr_wx, cr_wy, 1, &tx, &ty);   /* plan mode only */
        if ((!outside && !cr_wipe_dirty(cr_wx, cr_wy, 0, &tx, &ty)) || cr_wt > 700.0) {
            cr_wp = 4; cr_wt = 0.0;
        } else {
            double dx = tx - cr_wx, dy = ty - cr_wy, d = sqrt(dx * dx + dy * dy);
            double st = 14.0 * dt, nx, ny;
            int key = (ty / 32) * 64 + tx / 32;   /* note target switch */
            if (key != cr_wtarget) cr_wtarget = key;
            if (d > st) { nx = cr_wx + dx / d * st; ny = cr_wy + dy / d * st; }
            else { nx = tx; ny = ty; }
            if (outside && cr_in_zone(nx, ny)) {
                /* path leads through the monster: follow the keep-out
                 * edge (upward or downward, whichever is nearer) */
                ny = (cr_wy < (CR_SZ_Y0 + CR_SZ_Y1) / 2.0) ? (double)CR_SZ_Y0 : (double)CR_SZ_Y1;
                if (fabs(ny - cr_wy) > st) ny = cr_wy + (ny > cr_wy ? st : -st);
                if (cr_in_zone(nx, ny)) nx = cr_wx;
            }
            cr_wx = nx; cr_wy = ny;
        }
        break; }
    default: break;                       /* 4: hand stays put */
    }
    /* Scrubbing: two oscillators (x, y offset by 90 degrees) with
     * slowly "breathing" amplitudes - morphs steplessly between circles
     * (both large), sideways (x only), up-down (y only) and a straight
     * pull (both small), with no switching jump; humanly irregular */
    wiping = (cr_wp >= 1 && cr_wp <= 3) || (cr_wp == 5 && cr_wdown);
    if (cr_wp >= 1 && cr_wp <= 3) cr_wcirc += 0.38 * dt;
    ax = cr_wax * (0.5 + 0.5 * sin(cr_wcirc * 0.071 + 1.0));
    ay = cr_way * (0.5 + 0.5 * sin(cr_wcirc * 0.053));
    if (cr_wp == 5) ax = ay = 0.0;        /* path: the hand scrubs itself */
    cx = cr_wx + ax * cos(cr_wcirc);
    cy = cr_wy + ay * sin(cr_wcirc);
    if (wiping) {
        /* Footprint: current position and the path since the last render
         * (in-between step), so that no pixel is skipped between
         * two frames */
        cr_wipe_stamp((cr_wpx + cx) * 0.5, (cr_wpy + cy) * 0.5);
        cr_wipe_stamp(cx, cy);
    }
    {   /* Wipe level: base level 40 + speed share, speed smoothed over
         * ~12 frames (mouse recording jumps each frame) */
        double vx = (cx - cr_wpx) / (dt > 0.05 ? dt : 1.0);
        double vy = (cy - cr_wpy) / (dt > 0.05 ? dt : 1.0);
        double sp = sqrt(vx * vx + vy * vy);
        int lvl;
        cr_wspd += (sp - cr_wspd) * (dt / 12.0 > 1.0 ? 1.0 : dt / 12.0);
        lvl = (wiping && cr_wp != 4) ? 40 + (int)(cr_wspd * 60.0 / 12.0) : 0;
        sound_wipe(lvl > 100 ? 100 : lvl);
    }
    cr_wpx = cx; cr_wpy = cy;
    /* The demon's "Iehh" as soon as more than half of HIS pixels
     * (monster mask from the freeze) are wiped away; "Ouh" when he
     * is practically all gone (>= 97%, in case a few pixels missed) */
    if ((!cr_iehh || !cr_ouh) && wiping) {
        long vis = 0, gone = 0;
        for (i = 0; i < BOOT_TW * BOOT_TH; ++i)
            if (cr_mmask[i]) { ++vis; if (cr_wmask[i]) ++gone; }
        if (vis > 0 && !cr_iehh && gone * 2 > vis) { cr_iehh = 1; sound_play(SND_IEHH); }
        if (vis > 0 && !cr_ouh && gone * 100 >= vis * 97) { cr_ouh = 1; sound_play(SND_OUH); }
    }
    /* apply mask: wiped = black */
    for (i = 0; i < BOOT_TW * BOOT_TH; ++i)
        if (cr_wmask[i]) bpix[i] = 0xFF000000UL;
    /* arm: sprite at the sponge center, pole tiled downward */
    {
        double ax2 = cx - AR_SPCX + AR_W / 2.0, ay2 = cy - AR_SPCY + AR_H / 2.0;
        double ry = ay2 + AR_H / 2.0;     /* sprite bottom edge */
        int rh = AR_H - AR_RODY0;
        cr_blit_rgba(ar_rgba, AR_W, AR_H, ax2, ay2, 1.0, 0, 256);
        while (ry < BOOT_TH) {
            cr_blit_rgba(ar_rgba + AR_RODY0 * AR_W * 4, AR_W, rh, ax2, ry + rh / 2.0, 1.0, 0, 256);
            ry += rh;
        }
    }
}

static void credits_render(void)
{
    double tf = cr_tf, dt, rot, cs, sn;
    int i, x, y, t = (int)tf;
    unsigned long dec;
    (void)cr_text;                        /* large variant follows later */
    if (!cr_ready) cr_morph_init();       /* Headless/Fallback */
    cr_finale_init();
    /* Impact: freeze the mirror from the LAST finished frame (bpix
     * still holds it), spawn shards, sound, MOD fade */
    if (!cr_broken && tf >= cr_timp) cr_shatter_init();
    if (cr_broken && !cr_laughed && tf >= cr_timp + CR_LAUGH_AT) {
        cr_laughed = 1;                   /* the demon laughs about the break */
        sound_play(SND_HAHA);
    }
    /* Time step since the last render (in 50Hz frames); with VSync
     * ~60 renders/s meet 50 logic frames -> everything continuous
     * scales with dt instead of with the call counter */
    dt = tf - cr_tprev;
    if (dt < 0.0 || dt > 3.0) dt = 1.0;
    cr_tprev = tf;
    /* Wipe phase: freeze frame. On the first wipe frame freeze the LAST
     * finished frame (bpix still holds it, no arm) - starfield,
     * demon, shards, copper stand still and get wiped away */
    if (cr_broken && tf >= cr_timp + CR_WIPE_START) {
        if (!cr_frozen) {
            /* Freeze frame + exact monster mask (sprite alpha at the
             * frozen position/frame) for the "Iehh" */
            int mcx, mcy, mfront, mx, my, fr; double msc, mdv;
            memcpy(cr_freeze, bpix, sizeof(cr_freeze));
            memset(cr_mmask, 0, sizeof(cr_mmask));
            cr_monster_pos(cr_tprev, &mcx, &mcy, &msc, &mfront, &mdv);
            fr = ((int)(cr_tprev / 8.0)) & 3; if (fr == 3) fr = 1;
            for (my = 0; my < MN_H; ++my)
                for (mx = 0; mx < MN_W; ++mx) {
                    int px = mcx - MN_W / 2 + mx, py = mcy - MN_H / 2 + my;
                    if (px < 0 || px >= BOOT_TW || py < 0 || py >= CR_ML) continue;
                    if (mn_rgba[fr][(my * MN_W + mx) * 4 + 3] >= 128)
                        cr_mmask[py * BOOT_TW + px] = 1;
                }
            cr_frozen = 1;
        }
        memcpy(bpix, cr_freeze, sizeof(cr_freeze));
        cr_wipe_step(tf, dt);
        return;
    }
    /* Let the trail decay = real slight motion blur (168/256 per logic
     * frame, converted to the real time step) */
    dec = (unsigned long)(256.0 * pow(168.0 / 256.0, dt) + 0.5);
    for (i = 0; i < BOOT_TW * BOOT_TH; ++i) {
        Uint32 c = cr_trail[i];
        cr_trail[i] = 0xFF000000UL
            | ((((c >> 16) & 0xFF) * dec >> 8) << 16)
            | ((((c >> 8) & 0xFF) * dec >> 8) << 8)
            | (((c & 0xFF) * dec) >> 8);
    }
    /* Rotation: same direction as the title starfield, but
     * more leisurely (user request) */
    rot = tf * -0.004;
    cs = cos(rot); sn = sin(rot);
    cr_cs = cs; cr_sn = sn;               /* for spawn centering */
    /* The WHOLE room pans sideways at times: camera off-
     * set in WORLD coordinates (before the depth projection) -
     * near stars shift a lot, far ones barely (true parallax).
     * A slow envelope switches frontal <-> sideways; at t=0
     * exactly centered -> seamless morph. */
    {
        double env = (sin(tf * 0.004) + 1.0) * 0.5;
        cr_ox = sin(tf * 0.006) * 0.80 * env;
        cr_oy = sin(tf * 0.0042) * 0.22 * env;
    }
    for (i = 0; i < CR_STARS; ++i) {
        double rx, ry, px, py;
        unsigned long br, r, gg, b;
        if (cr_sz[i] <= 0.0) {            /* staggered refill */
            if ((double)t >= -cr_sz[i] - 1.0) cr_star_reset(i, 0);
            else continue;
        }
        cr_sz[i] -= 0.002 * dt;           /* permanently toward the viewer */
        if (cr_sz[i] < 0.045) { cr_star_reset(i, 1); continue; }
        rx = cr_sx[i] * cs - cr_sy[i] * sn;
        ry = cr_sx[i] * sn + cr_sy[i] * cs;
        px = 320.0 + (rx + cr_ox) / cr_sz[i] * 230.0;
        py = 200.0 + (ry + cr_oy) / cr_sz[i] * 230.0;
        if (px < -2 || px > BOOT_TW + 2 || py < -2 || py > BOOT_TH + 2) {
            cr_star_reset(i, 0);          /* sideways exit: random
                                           * depth, else front thins out */
            continue;
        }
        /* front brighter than back - but back must stay VISIBLE */
        br = (unsigned long)(255.0 * (1.1 - cr_sz[i]));
        if (br > 255) br = 255;
        br = 36 + ((br * br / 255) * 219 >> 8);
        if ((i & 3) == 0) { r = br * 3 / 4; gg = br * 7 / 8; b = br; }
        else if ((i & 3) == 1) { r = br * 7 / 8; gg = br * 15 / 16; b = br; }
        else { r = br; gg = br; b = br; }
        cr_plot((int)px, (int)py, r, gg, b);
        if (cr_sz[i] < 0.30) {            /* front: 2x2, ~full */
            cr_plot((int)px + 1, (int)py, r * 3 / 4, gg * 3 / 4, b * 3 / 4);
            cr_plot((int)px, (int)py + 1, r * 3 / 4, gg * 3 / 4, b * 3 / 4);
            cr_plot((int)px + 1, (int)py + 1, r >> 1, gg >> 1, b >> 1);
        } else if (cr_sz[i] < 0.50) {     /* middle: 1px + half neighbor */
            cr_plot((int)px + 1, (int)py, r >> 1, gg >> 1, b >> 1);
        }
    }
    memcpy(bpix, cr_trail, sizeof(cr_trail));
    /* Below the mirror edge NO starfield flies: the lines there show
     * only the toppled, dark reflection of the stars from above,
     * fading with depth (the stars themselves are covered there).
     * After the mirror breaks the real starfield stays: through the
     * smashed floor you look out into space. */
    for (y = CR_ML; y < BOOT_TH && !cr_broken; ++y) {
        int depth = y - CR_ML, ys = 2 * CR_ML - y;
        unsigned long f = (depth < 42) ? (unsigned long)(42 - depth) * 100UL / 42UL : 0UL;
        for (x = 0; x < BOOT_TW; ++x) {
            Uint32 c = bpix[ys * BOOT_TW + x];
            bpix[y * BOOT_TW + x] = 0xFF000000UL
                | ((((c >> 16) & 0xFF) * f >> 8) << 16)
                | ((((c >> 8) & 0xFF) * f >> 8) << 8)
                | (((c & 0xFF) * f) >> 8);
        }
    }
    /* Title snapshot fades out additively (logo, texts, robot) -
     * the old stars hand over to their morph successors here */
    if (t < 64) {
        unsigned long ta = 256UL - (unsigned long)t * 4UL;
        for (y = 0; y < VIC_H; ++y)
            for (x = 0; x < VIC_W; ++x) {
                Uint32 c = cr_last[y * VIC_W + x];
                unsigned long r = (((c >> 16) & 0xFF) * ta) >> 8;
                unsigned long gg = (((c >> 8) & 0xFF) * ta) >> 8;
                unsigned long b = ((c & 0xFF) * ta) >> 8;
                int dx, dy;
                if (!(r | gg | b)) continue;
                for (dy = 0; dy < 2; ++dy)
                    for (dx = 0; dx < 2; ++dx) {
                        Uint32 *d = &bpix[(y * 2 + dy) * BOOT_TW
                                          + x * 2 + dx];
                        unsigned long r0 = ((*d >> 16) & 0xFF) + r;
                        unsigned long g0 = ((*d >> 8) & 0xFF) + gg;
                        unsigned long b0 = (*d & 0xFF) + b;
                        if (r0 > 255) r0 = 255;
                        if (g0 > 255) g0 = 255;
                        if (b0 > 255) b0 = 255;
                        *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
                    }
            }
    }
    {   /* Monster BEHIND the logo (upper half of the path), rock in the
         * claws first (the feet cover its edge) */
        int mcx, mcy, mfront; double msc, mdv, rx, ry, rsc;
        cr_monster_pos(tf, &mcx, &mcy, &msc, &mfront, &mdv);
        if (!mfront) {
            if (cr_rock_pos(tf, &rx, &ry, &rsc))
                cr_blit_rgba(rk_rgba, RK_W, RK_H, rx, ry, rsc, 0, cr_rock_alpha(tf));
            cr_monster_draw(tf, 0);
        }
    }
    /* Logo at the top, two phases: the WHITE silhouette briefly fades
     * in from black (0.8s), then colors break through (1.2s) */
    {
        int la, wf;
        if (t < 85)       { la = (t - 60) * 256 / 25;       wf = 256; }
        else if (t < 125) { la = 256; wf = 256 - (t - 85) * 256 / 40; }
        else              { la = 256; wf = 0; }
        if (la < 0) la = 0;
        if (la > 256) la = 256;
        for (y = 0; y < CL_H; ++y)
            for (x = 0; x < CL_W; ++x) {
                const unsigned char *p = cl_rgba + (y * CL_W + x) * 4;
                unsigned long a = ((unsigned long)p[3] * (unsigned long)la) >> 8;
                Uint32 *d;
                unsigned long r0, g0, b0;
                if (!a) continue;
                d = &bpix[(y + 4) * BOOT_TW + (x + 110)];
                r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
                r0 = (r0 * (256 - a) + p[0] * a) >> 8;
                g0 = (g0 * (256 - a) + p[1] * a) >> 8;
                b0 = (b0 * (256 - a) + p[2] * a) >> 8;
                if (wf) {                 /* White flash, weighted by
                                           * pixel alpha (else noise alpha
                                           * in the PNG = white box) */
                    unsigned long wa = ((unsigned long)wf * p[3]) >> 8;
                    r0 += ((255 - r0) * wa) >> 8;
                    g0 += ((255 - g0) * wa) >> 8;
                    b0 += ((255 - b0) * wa) >> 8;
                }
                *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
            }
    }
    /* Rainbow copper line under the logo: thin bright core, ends
     * vignetted; hue makes EXACTLY one lap across the width
     * (seamless) and drifts slowly to the left */
    {
        int la = t - 60, lx0 = 12, lw = 616, ly = 272, lx, lr;
        static const unsigned char rowf[5] = { 70, 180, 255, 180, 70 };
        if (la > 256) la = 256;
        if (la > 0)
            for (lx = 0; lx < lw; ++lx) {
                int r, gg, b, e, v;
                cr_rainbow((lx * 1536 / lw + (int)(tf * 10.0)) % 1536, &r, &gg, &b);
                e = (lx < lw - 1 - lx) ? lx : lw - 1 - lx;
                v = (e < 70) ? e * 256 / 70 : 256;   /* end vignette*/
                v = (v * la) >> 8;
                for (lr = 0; lr < 5; ++lr) {
                    int m = (v * rowf[lr] * 3 / 4) >> 8;
                    bpix[(ly + lr) * BOOT_TW + lx0 + lx] = 0xFF000000UL
                        | (((unsigned long)(r * m) >> 8) << 16)
                        | (((unsigned long)(gg * m) >> 8) << 8)
                        | ((unsigned long)(b * m) >> 8);
                }
            }
    }
    /* small signature at right under the strip */
    {
        static const char *sig = "DOMY OF SYNDEV";
        int la = t - 60;
        if (la > 256) la = 256;
        if (la > 0)
            cr_text_small(sig, 628 - cr_text_small_w(sig), 281, la);
    }
    cr_flares(t);                         /* Sparkle on the logo */
    cr_monster_draw(tf, 1);               /* Shadow on logo (front only) */
    /* Mirror floor: soft blue gradient below the mirror edge - at the
     * edge a fine brighter horizon line, below it blue that fades
     * into the depth toward the bottom edge (stars stay faintly
     * visible); fades in with the rest */
    {
        int la = t - 60;
        if (la > 256) la = 256;
        if (cr_broken) la = 0;            /* Floor is gone */
        if (la > 0)
            for (y = CR_ML; y < BOOT_TH; ++y) {
                int depth = y - CR_ML;
                unsigned long fa = (depth == 0) ? 150UL
                                 : (unsigned long)(108 - depth * 76 / 41);
                unsigned long cr = (depth == 0) ? 60 : 24;
                unsigned long cg = (depth == 0) ? 100 : 48;
                unsigned long cb = (depth == 0) ? 200 : 130;
                fa = (fa * (unsigned long)la) >> 8;
                for (x = 0; x < BOOT_TW; ++x) {
                    Uint32 *d = &bpix[y * BOOT_TW + x];
                    unsigned long r0 = (*d >> 16) & 0xFF, g0 = (*d >> 8) & 0xFF,
                                  b0 = *d & 0xFF;
                    r0 = (r0 * (256 - fa) + cr * fa) >> 8;
                    g0 = (g0 * (256 - fa) + cg * fa) >> 8;
                    b0 = (b0 * (256 - fa) + cb * fa) >> 8;
                    *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
                }
            }
    }
    /* Credits sequence: each entry moves in from the right, stands its
     * time swinging in the middle, moves out to the left while the
     * next one moves in; after the last one it starts over. The glyphs
     * land in the text layer (rows >= CR_TY0 by geometry: wave
     * 300+-20, glyph +-25, mirror up to 399) */
    memset(cr_tlayer, 0, sizeof(cr_tlayer));
    cr_flucht_takt(tf);
    {
        const int E = CR_SEQ_E, V = CR_SEQ_V;   /* Move in/out: 150 frames at 5px */
        if (tf >= 40.0) {
            int cycle = cr_seq_cycle(), k, start = 0, m;
            double u = tf - 40.0;
            if (u >= cycle) {             /* Sequence over: last line
                                           * moves out left, then idle */
                double lu = u - cycle;
                if (lu < E)
                    for (m = 0; m < 2; ++m) {
                        cr_mirror = m;
                        cr_wave_deco(cr_seq[CR_NSEQ - 1].s, -lu * V, tf);
                    }
                cr_mirror = 0;
            } else
            for (k = 0; k < CR_NSEQ; ++k) {
                int len = E + cr_seq[k].hold;
                if (u < start + len) {
                    double lu = u - start;
                    /* Predecessor moves out to the left (not for the very first);
                     * pass 0 = text, 1 = floor mirroring */
                    for (m = 0; m < 2; ++m) {
                        cr_mirror = m;
                        if (lu < E && k > 0)
                            cr_wave_deco(cr_seq[k - 1].s, -lu * V, tf);
                        cr_wave_deco(cr_seq[k].s, (lu < E) ? (E - lu) * V : 0.0, tf);
                    }
                    cr_mirror = 0;
                    break;
                }
                start += len;
            }
        }
    }
    {   /* Monster IN FRONT of the logo (lower half of the path) with floor
         * mirroring (draws only near the edge) - BEFORE fading in the
         * text layer: on the detour the demon stands BEHIND the scroll
         * text, which is warped around him as by a glass ball.
         * Finale: draw the rock in the claws in front of the demon */
        int mcx, mcy, mfront; double msc, mdv, rx, ry, rsc;
        cr_monster_pos(tf, &mcx, &mcy, &msc, &mfront, &mdv);
        if (mfront) {                     /* Always draw the rock IN FRONT
                                           * of the demon (carried or free):
                                           * no layer jump on release,
                                           * the feet stay in front */
            if (cr_rock_pos(tf, &rx, &ry, &rsc))
                cr_blit_rgba(rk_rgba, RK_W, RK_H, rx, ry, rsc, 0, cr_rock_alpha(tf));
            cr_monster_draw(tf, 0);
            if (!cr_broken) cr_monster_draw(tf, 2);
        }
    }
    cr_text_composite(tf);                /* Fade in the layer: lens */
    {   /* Finale: mirror image of the falling rock (while the floor
         * is intact), shards, fade out */
        double rx, ry, rsc;
        if (tf >= cr_tdrop && !cr_broken && cr_rock_pos(tf, &rx, &ry, &rsc))
            cr_blit_rgba(rk_rgba, RK_W, RK_H, rx, ry, rsc, 1, 256);
        if (cr_broken) cr_shards_draw(tf);
    }
}

static int video_init(int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    win = SDL_CreateWindow("GORPH", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           VIC_W * scale, VIC_H * scale, SDL_WINDOW_RESIZABLE);
    if (!win) return -1;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) return -1;
    SDL_RenderSetLogicalSize(ren, VIC_W, VIC_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, VIC_W, VIC_H);
    btex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, BOOT_TW, BOOT_TH);
    return (tex && btex) ? 0 : -1;
}

/* TEMP debug: jump straight into mission n (0..3) (remove later) */
static void jump_mission(int n)
{
    if (n < 0 || n > 3) return;
    g.mission = n;
    g.level = n;                 /* MS counter to match: key 1 -> MS:01 etc. */
    game_start_mission();
}

int main(int argc, char **argv)
{
    int running = 1, frames = -1, i, prevfire = 0, cr_vs = 0, cheat = 0;
    const char *shot = NULL;
    Uint32 next;
    SDL_Event e;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "-cheat")) cheat = 1;   /* Debug keys on */
    }

    game_init();
    if (!shot || getenv("GORPH_BOOT")) {
        g.state = ST_BOOT;                /* Boot screen only at start */
        g.statetimer = 0;
    }

    if (shot) {
        SDL_Surface *surf;
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
        if (frames < 0) frames = 120;
        for (i = 0; i < frames; ++i) {
            int dbg = getenv("GORPH_JUMP") ? atoi(getenv("GORPH_JUMP")) : -1;
            if (dbg >= 0 && i == 10) jump_mission(dbg);
            /* GORPH_CREDITS=N: morph from the title screen into the credits
             * at frame N (title image is rendered once for it) */
            if (getenv("GORPH_CREDITS") && g.state == ST_TITLE &&
                i == atoi(getenv("GORPH_CREDITS"))) {
                vic_render(frame);
                game_glitch(frame);
                to_pixels();
                tw_overlay();
                robot_overlay();
                cr_morph_init();
                g.state = ST_CREDITS;
                g.statetimer = 0;
            }
            if (getenv("GORPH_AUTOFIRE")) {
                g.in_fire = (unsigned char)(i >= 20 && i < 24);
                g.fire_edge = (unsigned char)(i == 20);
            }
            game_frame();
            game_draw();
            if (g.state == ST_CREDITS && cr_finale_done((double)g.statetimer)) {
                sound_mod_stop();         /* Finale over: back to title */
                sound_wipe(0);
                game_title_return();
            }
            if (g.state == ST_CREDITS) {  /* Trail buffer needs history */
                cr_tf = (double)g.statetimer
                      + (getenv("GORPH_FRAC") ? atof(getenv("GORPH_FRAC")) : 0.0);
                credits_render();         /* GORPH_FRAC: reproduce sub-frame time
                                           * as in the VSync path */
            }
        }
        vic_render(frame);
        game_glitch(frame);
        to_pixels();
        tw_overlay();
        robot_overlay();
        if (getenv("GORPH_WDBG")) {       /* Headless diagnostics: wipe mask
                                           * as PGM + wiper state */
            FILE *f = fopen(getenv("GORPH_WDBG"), "wb");
            if (f) { fprintf(f, "P5 %d %d 255\n", BOOT_TW, BOOT_TH);
                     fwrite(cr_wmask, 1, sizeof(cr_wmask), f); fclose(f); }
            {   /* plus monster mask (value 1 -> 255) */
                char nm[512]; int k;
                sprintf(nm, "%s.mon.pgm", getenv("GORPH_WDBG"));
                f = fopen(nm, "wb");
                if (f) { fprintf(f, "P5 %d %d 255\n", BOOT_TW, BOOT_TH);
                         for (k = 0; k < BOOT_TW * BOOT_TH; ++k) fputc(cr_mmask[k] ? 255 : 0, f);
                         fclose(f); }
            }
            fprintf(stderr, "wp=%d band=%d wx=%.1f wy=%.1f circ=%.2f wt=%.1f\n",
                    cr_wp, cr_wband, cr_wx, cr_wy, cr_wcirc, cr_wt);
        }
        if (g.state == ST_BOOT || g.state == ST_CREDITS) {
            if (g.state == ST_BOOT) boot_render();
            surf = SDL_CreateRGBSurfaceFrom(bpix, BOOT_TW, BOOT_TH, 32,
                                            BOOT_TW * 4,
                                            0x00FF0000, 0x0000FF00,
                                            0x000000FF, 0xFF000000);
        } else
        surf = SDL_CreateRGBSurfaceFrom(pixels, VIC_W, VIC_H, 32, VIC_W * 4,
                                        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        SDL_SaveBMP(surf, shot);
        SDL_FreeSurface(surf);
        SDL_Quit();
        return 0;
    }

    if (video_init(3) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    sound_init();
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
        for (i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    }
    next = SDL_GetTicks();

    while (running) {
        const Uint8 *k;
        int fire;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
                g.state != ST_CREDITS) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                Uint32 fl = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fl ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            /* Any key ends attract mode (back to the title screen;
             * Space then starts the game normally) */
            if (g.demo && e.type == SDL_KEYDOWN && !e.key.repeat) {
                game_title_return();      /* the key is thereby used up:
                                           * C does NOT start credits now */
                continue;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode s = e.key.keysym.sym;
                if (cheat && s == SDLK_0 && g.state == ST_CREDITS) {
                    g.statetimer = 4300;  /* Cheat 0: jump to just before the end
                                           * (star line, then finale) -
                                           * music to the real position,
                                           * finale state reset */
                    cr_finale_reset();
                    sound_mod_seek(4300.0 / 50.0);
                    continue;
                }
                if (g.state == ST_CREDITS) {  /* any key: back */
                    sound_mod_stop();
                    sound_wipe(0);
                    game_title_return();
                    prevfire = 1;         /* Space exit does not fire now */
                    continue;
                }
                if (s == SDLK_c && g.state == ST_TITLE && g.bootzoom == 0) {
                    cr_morph_init();      /* take over stars + title image
                                           * (still needs the title time) */
                    g.state = ST_CREDITS;
                    g.statetimer = 0;
                    sound_wind(0);        /* title orbit wind off */
                    sound_mod_start();    /* chiptune runs in the credits */
                    continue;
                }
                /* Debug/cheat keys only with "gorph -cheat": 1-4 mission,
                 * 5 new title intro, 6 flagship kill, 8 level-8 outro,
                 * 9 new boot screen, N next mission incl. rank */
                if (!cheat) continue;
                if (s == SDLK_1 || s == SDLK_KP_1) jump_mission(0);
                if (s == SDLK_2 || s == SDLK_KP_2) jump_mission(1);
                if (s == SDLK_3 || s == SDLK_KP_3) jump_mission(2);
                if (s == SDLK_4 || s == SDLK_KP_4) jump_mission(3);
                if (s == SDLK_5 || s == SDLK_KP_5)   /* TEMP: new title intro */
                    game_init();
                if (s == SDLK_6 || s == SDLK_KP_6)   /* TEMP: flagship kill */
                    game_debug_win();
                if (s == SDLK_9 || s == SDLK_KP_9) { /* TEMP: new boot screen */
                    game_init();
                    g.state = ST_BOOT;
                    g.statetimer = 0;
                }
                if (s == SDLK_8 || s == SDLK_KP_8) { /* TEMP: level-8 outro */
                    jump_mission(3);
                    g.level = 8;
                    game_debug_win();
                }
                if (s == SDLK_n) {       /* TEMP: next mission incl. rank */
                    ++g.mission;
                    if (g.mission >= NUM_MISSIONS) {
                        g.mission = 0;
                        if (g.rank < MAX_RANK) ++g.rank;
                        if (g.rank == 1 && g.lives < 6) ++g.lives;
                    }
                    game_start_mission();
                }
            }
        }
        k = SDL_GetKeyboardState(NULL);
        g.in_left  = (unsigned char)(k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]);
        g.in_right = (unsigned char)(k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]);
        g.in_up    = (unsigned char)(k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]);
        g.in_down  = (unsigned char)(k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]);
        fire = (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_LCTRL]);
        if (pad) {
            Sint16 ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            Sint16 ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || ax < -12000) g.in_left = 1;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax >  12000) g.in_right = 1;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)    || ay < -12000) g.in_up = 1;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ay >  12000) g.in_down = 1;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) fire = 1;
        }
        g.in_fire   = (unsigned char)fire;
        g.fire_edge = (unsigned char)(fire && !prevfire);
        prevfire = fire;

        /* Credits: logic stays at 50 Hz (accumulator), rendering runs with
         * VSync on every display frame with sub-frame time -> no 50-on-60
         * judder. Outside the credits unchanged at 50 Hz. */
        {
            int want = (g.state == ST_CREDITS);
            if (want != cr_vs) {
                cr_vs = (SDL_RenderSetVSync(ren, want) == 0) ? want : 0;
                if (want) { next = SDL_GetTicks(); cr_tprev = -1.0; }
            }
        }
        if (cr_vs) {
            Uint32 now = SDL_GetTicks();
            int n = 0;
            while ((Sint32)(now - next) >= 0 && n < 4) {
                game_frame(); game_draw(); next += 20; ++n;
            }
            if ((Sint32)(now - next) >= 0) next = now;   /* cap the backlog */
            now = SDL_GetTicks();
            {
                double frac = 1.0 - (double)(Sint32)(next - now) / 20.0;
                if (frac < 0.0) frac = 0.0;
                if (frac > 1.0) frac = 1.0;
                cr_tf = (double)g.statetimer + frac;
            }
        } else {
            game_frame();
            game_draw();
            if (g.state == ST_CREDITS)    /* Fallback without VSync: time in
                                           * whole logic frames */
                cr_tf = (double)g.statetimer;
        }
        if (g.state == ST_CREDITS && cr_finale_done(cr_tf)) {
            sound_mod_stop();             /* Finale over: back to title */
            sound_wipe(0);
            game_title_return();
            prevfire = 1;
            cr_titlefade = 1;             /* fade title in, hand out */
        }
        vic_render(frame);
        game_glitch(frame);
        to_pixels();
        tw_overlay();
        robot_overlay();
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (g.state == ST_CREDITS) {      /* Amiga credits, high-res */
            credits_render();
            SDL_UpdateTexture(btex, NULL, bpix, BOOT_TW * sizeof(Uint32));
            SDL_SetTextureAlphaMod(btex, 255);
            SDL_RenderCopy(ren, btex, NULL, NULL);
        } else if (g.state == ST_BOOT) {  /* high-res boot texture */
            boot_render();
            SDL_UpdateTexture(btex, NULL, bpix, BOOT_TW * sizeof(Uint32));
            SDL_SetTextureAlphaMod(btex, 255);
            SDL_RenderCopy(ren, btex, NULL, NULL);
        } else {
            SDL_UpdateTexture(tex, NULL, pixels, VIC_W * sizeof(Uint32));
            SDL_RenderCopy(ren, tex, NULL, NULL);
            if (cr_titlefade && g.state == ST_TITLE) {
                /* After the wipe: the last credits image (black + hand) lies
                 * over the title screen and fades out in 12 frames ->
                 * hand disappears, title appears (fast, user) */
                int al = 255 - (g.statetimer - 750) * 255 / 12;   /* ~0.25 s */
                if (al <= 0) { al = 0; cr_titlefade = 0; }
                SDL_UpdateTexture(btex, NULL, bpix, BOOT_TW * sizeof(Uint32));
                SDL_SetTextureBlendMode(btex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(btex, (Uint8)al);
                SDL_RenderCopy(ren, btex, NULL, NULL);
            } else cr_titlefade = 0;
            if (g.bootzoom > 0) {         /* boot fades out over the title screen:
                                           * first the blue holds, then slow fade */
                int al = (g.bootzoom < 70) ? 255
                       : 255 - (g.bootzoom - 70) * 255 / 120;
                if (al < 0) al = 0;
                if (al > 255) al = 255;
                boot_render();
                SDL_UpdateTexture(btex, NULL, bpix, BOOT_TW * sizeof(Uint32));
                SDL_SetTextureBlendMode(btex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(btex, (Uint8)al);
                SDL_RenderCopy(ren, btex, NULL, NULL);
            }
        }
        SDL_RenderPresent(ren);

        if (cr_vs) continue;              /* VSync paces, logic above */
        next += 20;                       /* 50 Hz */
        if (SDL_GetTicks() < next) SDL_Delay(next - SDL_GetTicks());
        else next = SDL_GetTicks();
    }
    if (pad) SDL_GameControllerClose(pad);
    sound_shutdown();
    SDL_Quit();
    return 0;
}
