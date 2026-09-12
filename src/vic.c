#include "vic.h"
#include <string.h>

Vic vic;

/* C64-Palette nach Pepto */
const unsigned long vic_palette[16] = {
    0x000000UL, 0xFFFFFFUL, 0x68372BUL, 0x70A4B2UL,
    0x6F3D86UL, 0x588D43UL, 0x4443ABUL, 0xB8C76FUL,   /* Blau: echtes Gorph-Blau (Nutzer-Muster) */
    0x6F4F25UL, 0x433900UL, 0x9A6759UL, 0x444444UL,
    0x6C6C6CUL, 0x9AD284UL, 0x6C5EB5UL, 0x959595UL
};

void vic_reset(void)
{
    memset(&vic, 0, sizeof(vic));
    vic.spmc0 = 7;
    vic.spmc1 = 2;
}

/* Multicolor-Bitmap: Bitpaare waehlen bg / screen-hi / screen-lo / colorram */
static void render_bitmap(unsigned char *out)
{
    int cx, cy, ry, p;
    for (cy = 0; cy < VIC_ROWS; ++cy) {
        for (cx = 0; cx < VIC_COLS; ++cx) {
            int cell = cy * VIC_COLS + cx;
            unsigned char sc = vic.screen[cell];
            unsigned char c11 = (unsigned char)(vic.color[cell] & 0x0F);
            unsigned char c01 = (unsigned char)(sc >> 4);
            unsigned char c10 = (unsigned char)(sc & 0x0F);
            for (ry = 0; ry < 8; ++ry) {
                unsigned char b = vic.bitmap[cell * 8 + ry];
                unsigned char *dst = out + (cy * 8 + ry) * VIC_W + cx * 8;
                for (p = 0; p < 4; ++p) {
                    unsigned char v = (unsigned char)((b >> (6 - p * 2)) & 3);
                    unsigned char c = (v == 0) ? vic.bg : (v == 1) ? c01 : (v == 2) ? c10 : c11;
                    dst[p * 2]     = c;
                    dst[p * 2 + 1] = c;
                }
            }
        }
    }
}

/* Multicolor-Textmodus: Farbnibble unter 8 -> Hires-Zeichen in dieser
 * Farbe; ab 8 -> Multicolor mit $D021/$D022/$D023/Farbe&7 */
static void render_text(unsigned char *out)
{
    int cx, cy, ry, p;
    for (cy = 0; cy < VIC_ROWS; ++cy) {
        for (cx = 0; cx < VIC_COLS; ++cx) {
            int cell = cy * VIC_COLS + cx;
            unsigned char code = vic.screen[cell];
            unsigned char raw  = vic.color[cell];
            unsigned char col  = (unsigned char)(raw & 0x0F);
            const unsigned char *gl = vic.charset + ((code & 0x7F) * 8);
            for (ry = 0; ry < 8; ++ry) {
                unsigned char b = gl[ry];
                unsigned char *dst = out + (cy * 8 + ry) * VIC_W + cx * 8;
                /* Bit 4 (0x10) erzwingt Hires auch bei Farbe >= 8 -
                 * fuer Graustufen-Text (Typewriter-Fade) */
                if ((col & 8) && !(raw & 0x10)) {
                    for (p = 0; p < 4; ++p) {
                        unsigned char v = (unsigned char)((b >> (6 - p * 2)) & 3);
                        unsigned char c = (v == 0) ? vic.bg  : (v == 1) ? vic.bg1
                                        : (v == 2) ? vic.bg2 : (unsigned char)(col & 7);
                        dst[p * 2]     = c;
                        dst[p * 2 + 1] = c;
                    }
                } else {
                    for (p = 0; p < 8; ++p)
                        dst[p] = ((b >> (7 - p)) & 1) ? col : vic.bg;
                }
            }
        }
    }
}

static void render_sprite(unsigned char *out, int i)
{
    const unsigned char *d = vic.spdata[i];
    int mc  = (vic.spmulti >> i) & 1;
    int xe  = ((vic.spxexp >> i) & 1) ? 2 : 1;
    int ye  = ((vic.spyexp >> i) & 1) ? 2 : 1;
    int x0  = vic.spx[i] - 24;
    int y0  = vic.spy[i] - 50;
    int row, byi, p, dx, dy, X, Y;

    for (row = 0; row < 21; ++row) {
        for (byi = 0; byi < 3; ++byi) {
            unsigned char b = d[row * 3 + byi];
            if (!b) continue;
            if (mc) {
                for (p = 0; p < 4; ++p) {
                    unsigned char v = (unsigned char)((b >> (6 - p * 2)) & 3);
                    unsigned char c;
                    if (!v) continue;
                    c = (v == 1) ? vic.spmc0 : (v == 2) ? vic.spcol[i] : vic.spmc1;
                    for (dy = 0; dy < ye; ++dy) {
                        for (dx = 0; dx < 2 * xe; ++dx) {
                            X = x0 + (byi * 4 + p) * 2 * xe + dx;
                            Y = y0 + row * ye + dy;
                            if (X >= 0 && X < VIC_W && Y >= 0 && Y < VIC_H)
                                out[Y * VIC_W + X] = c;
                        }
                    }
                }
            } else {
                for (p = 0; p < 8; ++p) {
                    if (!((b >> (7 - p)) & 1)) continue;
                    for (dy = 0; dy < ye; ++dy) {
                        for (dx = 0; dx < xe; ++dx) {
                            X = x0 + (byi * 8 + p) * xe + dx;
                            Y = y0 + row * ye + dy;
                            if (X >= 0 && X < VIC_W && Y >= 0 && Y < VIC_H)
                                out[Y * VIC_W + X] = vic.spcol[i];
                        }
                    }
                }
            }
        }
    }
}

/* Ist Sprite-Pixel (px,py relativ zur Ecke, in Geraetepixeln) gesetzt?
 * Multicolor: Bitpaar 01 zaehlt fuer Kollisionen nicht. */
static int sprite_solid(int i, int px, int py)
{
    int xe = ((vic.spxexp >> i) & 1) ? 2 : 1;
    int ye = ((vic.spyexp >> i) & 1) ? 2 : 1;
    int row = py / ye;
    unsigned char b;
    if (row < 0 || row >= 21) return 0;
    px /= xe;
    if (px < 0 || px >= 24) return 0;
    b = vic.spdata[i][row * 3 + (px >> 3)];
    if ((vic.spmulti >> i) & 1) {
        int pair = (b >> (6 - ((px >> 1) & 3) * 2)) & 3;
        return pair != 0;    /* alle sichtbaren Paare kollidieren (auch %01) */
    }
    return (b >> (7 - (px & 7))) & 1;
}

/* Vordergrundpixel der Anzeige an Bildschirmposition (0..319, 0..199)?
 * Textmodus: Hires-Zeichen (Farbnibble < 8) kollidieren mit jedem Bit,
 * Multicolor-Zeichen nur mit den Paaren 10/11.  Bitmap: Paare 10/11. */
static int bg_solid(int x, int y)
{
    int cell, b;
    if (x < 0 || x >= VIC_W || y < 0 || y >= VIC_H) return 0;
    cell = (y >> 3) * VIC_COLS + (x >> 3);
    if (vic.mode == VIC_MODE_TEXT) {
        unsigned char code = vic.screen[cell];
        unsigned char col  = vic.color[cell];
        b = vic.charset[(code & 0x7F) * 8 + (y & 7)];
        if (col & 8) return ((b >> (6 - ((x >> 1) & 3) * 2)) & 3) >= 2;
        return (b >> (7 - (x & 7))) & 1;
    }
    b = vic.bitmap[cell * 8 + (y & 7)];
    return ((b >> (6 - ((x >> 1) & 3) * 2)) & 3) >= 2;
}

/* Wie sprite_solid, aber HW-treu: MC-Paar %01 ist fuer Kollisionen
 * transparent (VIC-II $D01E-Verhalten). */
static int sprite_solid_hw(int i, int px, int py)
{
    int xe = ((vic.spxexp >> i) & 1) ? 2 : 1;
    int ye = ((vic.spyexp >> i) & 1) ? 2 : 1;
    int row = py / ye;
    unsigned char b;
    if (row < 0 || row >= 21) return 0;
    px /= xe;
    if (px < 0 || px >= 24) return 0;
    b = vic.spdata[i][row * 3 + (px >> 3)];
    if ((vic.spmulti >> i) & 1)
        return ((b >> (6 - ((px >> 1) & 3) * 2)) & 3) >= 2;
    return (b >> (7 - (px & 7))) & 1;
}

/* Sprite-Sprite-Kollision wie die VIC-Hardware ($D01E): %01 zaehlt nicht */
int vic_sprites_overlap_hw(int a, int b)
{
    int ax0 = vic.spx[a] - 24, ay0 = vic.spy[a] - 50;
    int bx0 = vic.spx[b] - 24, by0 = vic.spy[b] - 50;
    int aw = ((vic.spxexp >> a) & 1) ? 48 : 24;
    int ah = ((vic.spyexp >> a) & 1) ? 42 : 21;
    int bw = ((vic.spxexp >> b) & 1) ? 48 : 24;
    int bh = ((vic.spyexp >> b) & 1) ? 42 : 21;
    int x0 = ax0 > bx0 ? ax0 : bx0;
    int y0 = ay0 > by0 ? ay0 : by0;
    int x1 = (ax0 + aw) < (bx0 + bw) ? ax0 + aw : bx0 + bw;
    int y1 = (ay0 + ah) < (by0 + bh) ? ay0 + ah : by0 + bh;
    int x, y;
    if (!((vic.spenable >> a) & 1) || !((vic.spenable >> b) & 1)) return 0;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if (sprite_solid_hw(a, x - ax0, y - ay0) &&
                sprite_solid_hw(b, x - bx0, y - by0))
                return 1;
    return 0;
}

int vic_sprites_overlap(int a, int b)
{
    int ax0 = vic.spx[a] - 24, ay0 = vic.spy[a] - 50;
    int bx0 = vic.spx[b] - 24, by0 = vic.spy[b] - 50;
    int aw = ((vic.spxexp >> a) & 1) ? 48 : 24;
    int ah = ((vic.spyexp >> a) & 1) ? 42 : 21;
    int bw = ((vic.spxexp >> b) & 1) ? 48 : 24;
    int bh = ((vic.spyexp >> b) & 1) ? 42 : 21;
    int x0 = ax0 > bx0 ? ax0 : bx0;
    int y0 = ay0 > by0 ? ay0 : by0;
    int x1 = (ax0 + aw) < (bx0 + bw) ? ax0 + aw : bx0 + bw;
    int y1 = (ay0 + ah) < (by0 + bh) ? ay0 + ah : by0 + bh;
    int x, y;
    if (!((vic.spenable >> a) & 1) || !((vic.spenable >> b) & 1)) return 0;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if (sprite_solid(a, x - ax0, y - ay0) &&
                sprite_solid(b, x - bx0, y - by0))
                return 1;
    return 0;
}

/* Beruehrt Sprite i pixelgenau eine Textzelle mit Code in [lo,hi]?
 * (Gate wie $D01F, Zellauswahl macht der Aufrufer wie $956B ueber
 * die Sprite-Ecke.) */
int vic_sprite_hits_code(int i, int lo, int hi)
{
    int x0 = vic.spx[i] - 24, y0 = vic.spy[i] - 50;
    int w = ((vic.spxexp >> i) & 1) ? 48 : 24;
    int h = ((vic.spyexp >> i) & 1) ? 42 : 21;
    int x, y;
    if (!((vic.spenable >> i) & 1)) return 0;
    if (vic.mode != VIC_MODE_TEXT) return 0;
    for (y = 0; y < h; ++y) {
        int Y = y0 + y;
        if (Y < 0 || Y >= VIC_H) continue;
        for (x = 0; x < w; ++x) {
            int X = x0 + x, cell, code;
            unsigned char b;
            if (X < 0 || X >= VIC_W) continue;
            if (!sprite_solid(i, x, y)) continue;
            cell = (Y >> 3) * VIC_COLS + (X >> 3);
            code = vic.screen[cell];
            if (code < lo || code > hi) continue;
            b = vic.charset[(code & 0x7F) * 8 + (Y & 7)];
            if (vic.color[cell] & 8) {
                if (((b >> (6 - ((X >> 1) & 3) * 2)) & 3) >= 2) return 1;
            } else {
                if ((b >> (7 - (X & 7))) & 1) return 1;
            }
        }
    }
    return 0;
}

int vic_sprite_bg(int i)
{
    int x0 = vic.spx[i] - 24, y0 = vic.spy[i] - 50;
    int w = ((vic.spxexp >> i) & 1) ? 48 : 24;
    int h = ((vic.spyexp >> i) & 1) ? 42 : 21;
    int x, y;
    if (!((vic.spenable >> i) & 1)) return 0;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            if (sprite_solid(i, x, y) && bg_solid(x0 + x, y0 + y))
                return 1;
    return 0;
}

int vic_shake_x = 0, vic_shake_y = 0;

void vic_render(unsigned char *out)
{
    int i;
    if (vic.mode == VIC_MODE_TEXT) render_text(out);
    else                           render_bitmap(out);
    for (i = NUM_SPRITES - 1; i >= 0; --i)
        if ((vic.spenable >> i) & 1)
            render_sprite(out, i);

    /* Screenshake: fertiges Bild um (x,y) versetzen, Rand = Rahmenfarbe */
    if (vic_shake_x || vic_shake_y) {
        static unsigned char tmp[VIC_W * VIC_H];
        int x, y;
        memcpy(tmp, out, VIC_W * VIC_H);
        for (y = 0; y < VIC_H; ++y)
            for (x = 0; x < VIC_W; ++x) {
                int sx = x - vic_shake_x, sy = y - vic_shake_y;
                out[y * VIC_W + x] =
                    (sx >= 0 && sx < VIC_W && sy >= 0 && sy < VIC_H)
                        ? tmp[sy * VIC_W + sx]
                        : (unsigned char)(vic.border & 15);
            }
    }
}
