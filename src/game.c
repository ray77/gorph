/* game.c - game logic, after the disassembly of the C64 version.
 *
 * Astro Battles and Flag Ship run in multicolor text mode: the
 * matrix $4000 holds character codes, enemies are 2x2 cell blocks
 * with consecutive codes ($8649), the shield consists of the
 * characters $19-$1D ($96C2/$96DA).  Laser Attack and Space Warp
 * run in the multicolor bitmap with the software blitter ($9967).
 */
#include "gorph.h"
#include "vic.h"
#include "shape.h"
#include "sound.h"
#include "gorph_data.h"
#include <string.h>
#include <stdio.h>

Game g;

/* --- Original tables ------------------------------------------------- */

static const signed char form_col[8]  = { 1, 4, 7, 10, 13, 16, 19, 22 };  /* $853E */
static const signed char rank_row[6]  = { 6, 8, 10, 12, 12, 14 };         /* $8546 */
static const unsigned char row_color[3] = { 7, 5, 2 };                    /* $8706 */
static const unsigned char row_code[3]  = { 0x01, 0x09, 0x11 };           /* $84C2 */
static const int points_tab[8] = { 20, 50, 100, 150, 200, 250, 300, 1000 }; /* $9F39/$9F41 */

static const char *mission_name[NUM_MISSIONS] = {   /* $9E45 */
    "ASTRO BATTLES", "LASER ATTACK", "SPACE WARP", "FLAG SHIP"
};
static const char *rank_name[6] = {                 /* $9E1B */
    "CADET", "CAPTAIN", "COLONEL", "GENERAL", "WARRIOR", "AVENGER"
};

/* Register sets $826A: border/background, mode, sprite colors */
static const unsigned char mis_border[4] = { 6, 0, 0, 0 };
static const unsigned char mis_bg[4]     = { 6, 0, 0, 0 };
static const unsigned char mis_bg1[4]    = { 1, 0, 0, 7 };  /* $D022 */
static const unsigned char mis_bg2[4]    = { 0, 0, 0, 0 };  /* $D023 */
static const unsigned char mis_mode[4]   = {
    VIC_MODE_TEXT, VIC_MODE_BITMAP, VIC_MODE_BITMAP, VIC_MODE_TEXT
};
static const unsigned char mis_spcol[4][8] = {
    { 2, 1, 1, 1, 6, 0, 0, 0 },
    { 6, 6, 6, 6, 6, 6, 6, 6 },
    { 6, 1, 6, 0, 6, 6, 6, 0 },
    { 6, 1, 6, 6, 6, 6, 6, 7 }
};

/* Sprite blocks (pointer - 64) */
#define BLK_PLAYER    0     /* $40 $5000 */
#define BLK_GORPH      1     /* $41 $5040 large ship       */
#define BLK_SAUCER    2     /* $42 $5080 small saucer     */
#define BLK_FLY       3     /* $43 $50C0 fly              */
#define BLK_FLAG_L    4     /* $44 flagship left          */
#define BLK_FLAG_R    5     /* $45 flagship right         */
#define BLK_FIGHTER   6     /* $46 small fighter (M1)     */
#define BLK_LEADER   20     /* $54 lead ship (M1)         */
#define BLK_BOOM1    22     /* $56 explosion sequence     */
#define BLK_BOMB_A   31     /* $5F zigzag bomb            */
#define BLK_BOMB_B   32     /* $60 */
#define BLK_SHOT     33     /* $61 player shot            */

/* Char codes */
#define CH_EMPTY   0x00
#define CH_STAR    0x1E
#define CH_LIFE    0x3E
#define CH_TEXT    0x1F     /* 'A' */

#define SP_PLAYER 0
#define SP_SHOT   1
#define SP_BOMB0  2
#define SP_BOMB1  3
#define SP_BOOM   4
#define SP_GORPH   7

/* --- Miscellany ------------------------------------------------------ */

static unsigned long rngstate = 0x29A5721UL;
static int rnd(int n)
{
    rngstate = rngstate * 1103515245UL + 12345UL;
    return (int)((rngstate >> 16) & 0x7FFF) % n;
}

static void set_sprite(int i, int block, int x, int y, int col, int mc)
{
    memcpy(vic.spdata[i], gd_sprites + block * 64, 64);
    vic.spx[i]   = (short)x;
    vic.spy[i]   = (short)y;
    vic.spcol[i] = (unsigned char)col;
    vic.spenable = (unsigned short)(vic.spenable | (1 << i));
    if (mc) vic.spmulti = (unsigned short)(vic.spmulti | (1 << i));
    else    vic.spmulti = (unsigned short)(vic.spmulti & ~(1 << i));
}

static void sprite_off(int i)
{
    vic.spenable = (unsigned short)(vic.spenable & ~(1 << i));
}

static int  tsin_q(int q);              /* Title orbit (defined below) */
static void robot_scaled(int spr, int w_p, int hs);

/* Set all visible MC pairs of a sprite to %10 -> the whole shape
 * takes on the sprite color (for the GORPH letter flash) */
static void sprite_solidify(int i)
{
    int b, p;
    for (b = 0; b < 63; ++b) {
        unsigned char v = vic.spdata[i][b], o = 0;
        for (p = 0; p < 4; ++p)
            if ((v >> (p * 2)) & 3) o = (unsigned char)(o | (2 << (p * 2)));
        vic.spdata[i][b] = o;
    }
}

/* Thin out sprite data (dither at MC pair level): phase 0 = 50%
 * checkerboard, phase 1 = 25% - for "smeared" motion blur ghosts */
static void sprite_dither(int i, int phase)
{
    int b;
    for (b = 0; b < 63; ++b) {
        int row = b / 3;
        unsigned char m;
        if (phase == 0)
            m = (unsigned char)((row & 1) ? 0x33 : 0xCC);
        else
            m = (unsigned char)((row & 1) ? 0x00 : ((row & 2) ? 0x33 : 0xCC));
        vic.spdata[i][b] = (unsigned char)(vic.spdata[i][b] & m);
    }
}

/* $9B48 (force) / $9B4B (only free cell: 0 or >= $19) */
static void mat_put(int col, int row, int code, int color, int force)
{
    int cell;
    if (col < 0 || col >= VIC_COLS || row < 0 || row >= VIC_ROWS) return;
    cell = row * VIC_COLS + col;
    if (!force) {
        unsigned char c = vic.screen[cell];
        if (c != 0 && c < 0x19) return;
    }
    vic.color[cell]  = (unsigned char)color;
    vic.screen[cell] = (unsigned char)code;
}

/* Text into the matrix: charset order $1F.. = A-I K-U/V W, $33.. = 0-9 */

/* Narrow MC font ($1F+, original status lines) */
static int char_code(char c)
{
    static const char set[] = "ABCDEFGHIKLMNOPRSTUW";
    int i;
    if (c >= '0' && c <= '9') return 0x33 + (c - '0');
    if (c == ':') return 0x3D;
    if (c == 'V') return 0x31;              /* shares the shape with U */
    for (i = 0; set[i]; ++i)
        if (set[i] == c) return CH_TEXT + i;
    return CH_EMPTY;
}

/* Full hires font (glyphs $41-$5A A-Z, $70-$79 digits, $7A ':') -
 * for title/intro texts; status lines use the narrow MC font. */
static int char_hires(char c)
{
    if (c >= 'A' && c <= 'Z') return 0x41 + (c - 'A');
    if (c >= '0' && c <= '9') return 0x70 + (c - '0');
    if (c == ':') return 0x7A;
    if (c == '.') return 0x6E;
    if (c == '!') return 0x61;
    if (c == ',') return 0x6C;
    if (c == '-') return 0x6D;
    if (c == '=') return 0x7D;
    return CH_EMPTY;
}

static void mat_text(int col, int row, const char *s, int color)
{
    int i;
    for (i = 0; s[i]; ++i)
        mat_put(col + i, row, char_hires(s[i]), color, 1);
}

/* Status line text in narrow MC font (yellow, like original) */
static void mat_text_mc(int col, int row, const char *s)
{
    int i;
    for (i = 0; s[i]; ++i)
        mat_put(col + i, row, char_code(s[i]), 0x0F, 1);
}

/* The same characters as bitmap cells (status line in bitmap mode, $9DC7) */
static void bm_text(int col, int row, const char *s)
{
    int i, y;
    for (i = 0; s[i]; ++i) {
        int cell = row * VIC_COLS + col + i;
        int code = char_code(s[i]);       /* narrow MC font (original) */
        if (col + i >= VIC_COLS) break;
        for (y = 0; y < 8; ++y)
            vic.bitmap[cell * 8 + y] = vic.charset[code * 8 + y];
    }
}

/* --- Bitmap basics ($9884/$9B7F, $98A8) ------------------------------ */

static void bitmap_clear(void)
{
    memset(vic.bitmap, 0, VIC_BITMAP_SZ);
    memset(vic.screen, 0x76, VIC_SCREEN_SZ);   /* %01 yel, %10 blue */
    memset(vic.color,  0x02, VIC_SCREEN_SZ);   /* %11 red           */
}

static void plot(int xmc, int y, int color, int force)
{
    static const unsigned char keep[4] = { 0x3F, 0xCF, 0xF3, 0xFC };
    static const unsigned char pat[4]  = { 0x00, 0x55, 0xAA, 0xFF };
    int off;
    if (xmc < 0 || xmc >= 160 || y < 0 || y >= VIC_H) return;
    off = (y >> 3) * (VIC_COLS * 8) + (xmc >> 2) * 8 + (y & 7);
    if (vic.bitmap[off] && !force) return;
    vic.bitmap[off] = (unsigned char)((vic.bitmap[off] & keep[xmc & 3]) |
                      (unsigned char)(~keep[xmc & 3] & pat[color & 3]));
}

/* --- Status line ($9CBB text / $9D21 bitmap) ------------------------- */

static void status_lines(void)
{
    char buf[16];
    const char *mn = mission_name[g.mission];
    int i;

    if (vic.mode == VIC_MODE_TEXT) {
        /* Status lines: narrow MC font yellow (as original + bitmap levels) */
        mat_text_mc(1, 0, "SCORE:");
        sprintf(buf, "%06ld", g.score);
        mat_text_mc(7, 0, buf);
        for (i = 0; i < 6; ++i)   /* full life count (longplay: start 5) */
            mat_put(17 + i, 0,
                    (g.state != ST_TITLE && !g.demo && i < g.lives) ? CH_LIFE : CH_EMPTY,
                    0x0F, 1);
        if (g.demo && ((g.frame >> 4) & 1))   /* DEMO blinks, not crosses */
            mat_text_mc(18, 0, "DEMO");
        mat_text_mc(27, 0, "HSCORE:");
        sprintf(buf, "%06ld", g.hiscore);
        mat_text_mc(34, 0, buf);

        mat_text_mc(1, 24, "SPACE ");
        mat_text_mc(7, 24, rank_name[g.rank]);
        sprintf(buf, "MS:%02d", g.level % 100);
        mat_text_mc(18, 24, buf);
        mat_text_mc(40 - (int)strlen(mn), 24, mn);
    } else {
        bm_text(1, 0, "SCORE:");
        sprintf(buf, "%06ld", g.score);
        bm_text(7, 0, buf);
        for (i = 0; i < 6; ++i) {         /* life crosses in bitmap too */
            int code = (!g.demo && i < g.lives) ? CH_LIFE : CH_EMPTY;
            int y;
            for (y = 0; y < 8; ++y)
                vic.bitmap[(17 + i) * 8 + y] = vic.charset[code * 8 + y];
        }
        if (g.demo && ((g.frame >> 4) & 1))
            bm_text(18, 0, "DEMO");
        bm_text(27, 0, "HSCORE:");
        sprintf(buf, "%06ld", g.hiscore);
        bm_text(34, 0, buf);
        bm_text(1, 24, "SPACE ");
        bm_text(7, 24, rank_name[g.rank]);
        sprintf(buf, "MS:%02d", g.level % 100);
        bm_text(18, 24, buf);
        bm_text(40 - (int)strlen(mn), 24, mn);
    }
}

/* --- Stars ($95CC text / $9663 bitmap) ------------------------------- */

/* (matrix title stars stars_text REMOVED - the title screen now uses
 * the rotating pixel starfield title_starfield as a post pass.) */

/* White star in bitmap: %01 pixel + cell hi-nibble set to white */
static void plot_star(int xmc, int y)
{
    int cell = (y >> 3) * VIC_COLS + (xmc >> 2);
    plot(xmc, y, 1, 0);
    if (cell >= 0 && cell < VIC_SCREEN_SZ)
        vic.screen[cell] = (unsigned char)((vic.screen[cell] & 0x0F) | 0x10);
}

static void stars_bitmap_all(void)
{
    int i;
    for (i = 0; i < 32; ++i) {
        if (!gd_star_col[i]) continue;
        plot_star(gd_star_col[i] * 4, gd_star_row[i] * 8);
    }
}

/* Sparkle ($9663): exactly one star is dark per frame, index moves */
static void star_twinkle(void)
{
    static int tw = 0;
    int prev = tw;
    tw = (tw + 1) & 31;
    if (gd_star_col[prev])
        plot_star(gd_star_col[prev] * 4, gd_star_row[prev] * 8);
    if (gd_star_col[tw])
        plot(gd_star_col[tw] * 4, gd_star_row[tw] * 8, 0, 1);
}

/* (text_stars_fall removed - outro uses pixel starfield) */

/* Stars in text mode (Flag Ship): white, moving sparkle */
static void text_stars(void)
{
    static int tw = 0;
    int i;
    tw = (tw + 1) & 31;
    for (i = 0; i < 32; ++i) {
        int row = gd_star_row[i];
        if (!gd_star_col[i] || row < 2 || row > 23) continue;
        mat_put(gd_star_col[i], row,
                (i == tw) ? CH_EMPTY : CH_STAR, 1, 0);
    }
}

/* --- Shield ($96C2 dome / $96DA dish) -------------------------------- */

/* One table entry: bit 7 = row += step first; code = value & $7F.
 * X = 17..0; left column 19-X (hi nibble), right 20+X (lo nibble). */
static void shield_draw(int bowl)
{
    const unsigned char *tab = bowl ? gd_shield_bowl : gd_shield_dome;
    int startrow = bowl ? 10 : 18;
    int step     = bowl ? 1 : -1;
    int color    = bowl ? 2 : 1;
    int blank    = (!bowl && g.shot == 2 && g.shoty >= 0xA0);  /* $A3 */
    int side;

    for (side = 0; side < 2; ++side) {
        int row = startrow;
        int x;
        for (x = 17; x >= 0; --x) {
            int e    = tab[x];
            int code = e & 0x7F;
            int col  = side ? 20 + x : 19 - x;
            int dead = side ? !(g.shield[x] & 0x0F) : !(g.shield[x] & 0xF0);
            if (e & 0x80) row += step;
            mat_put(col, row, (dead || blank) ? CH_EMPTY : code, color, 0);
            /* Continuation cell for the 4-row blocks across cell borders */
            if (!dead && !blank) {
                if (code + 1 == 0x1D)
                    mat_put(col, row - step, 0x1D, color, 0);
            } else if (code + 1 == 0x1D) {
                mat_put(col, row - step, CH_EMPTY, color, 0);
            }
        }
    }
}

/* $97A6: hit at column y - clears the whole nibble */
static void shield_hit_col(int col)
{
    if (col >= 2 && col <= 19)  g.shield[19 - col] &= 0x0F;
    else if (col >= 20 && col <= 37) g.shield[col - 20] &= 0xF0;
}

/* Flicker ($9784): every second frame period XOR-toggle codes $19-$1D */
static void shield_flicker(void)
{
    static const unsigned char pat[4] = { 0xCC, 0xC3, 0x66, 0x66 };
    int i, k = 0;
    if (g.frame & 1) return;
    for (i = 0x19 * 8; i < 0x19 * 8 + 34; ++i) {
        if (vic.charset[i] == 0) continue;   /* zero bytes do not count */
        vic.charset[i] ^= pat[k & 3];
        ++k;
    }
}

/* --- Explosion sprite 4 ($8685/$A027) -------------------------------- */

static void boom_at(int sx, int sy)
{
    g.boomtimer = 16;
    g.boomx = sx;
    g.boomy = sy;
    sound_play(SND_HIT);
}

static void boom_draw(void)
{
    static const unsigned char seq[4] = { 22, 23, 25, 26 };       /* $A0AC */
    int idx;
    if (g.boomtimer <= 0) return;
    idx = 3 - g.boomtimer / 4;
    if (idx < 0) idx = 0;      /* fresh boom (16) even before the decrement */
    set_sprite(SP_BOOM, seq[idx], g.boomx, g.boomy, 7, 1);
}

static void boom_update(void)
{
    if (g.boomtimer <= 0) { sprite_off(SP_BOOM); return; }
    --g.boomtimer;
    boom_draw();
}

static void laser_draw_player(int erase);

/* Player explosion: ship disappears (blitter erase or sprite off),
 * boom sequence replaces it (boom_update runs in ST_PLAYER_HIT). */
static void player_explode(void)
{
    if (g.mission == 1) {
        laser_draw_player(1);            /* erase ship from the bitmap */
        boom_at(g.bx * 2 + 24, g.by + 50);
    } else {
        sprite_off(SP_PLAYER);           /* ship gone, explosion in its place */
        boom_at(g.px, g.py);
    }
}

static void add_score(int idx)
{
    if (g.demo) return;                   /* attract mode: no scoring ($AC) */
    g.score += points_tab[idx];
    if (g.score > 999999L) g.score -= 1000000L;
    if (g.score > g.hiscore) g.hiscore = g.score;
}

/* ===================================================================== */
/*  Astro Battles (Mission 0)                                            */
/* ===================================================================== */

static void draw_enemy(int i)
{
    int col = g.ex[i], row = g.ey[i];
    int color = row_color[i >> 3];
    int b = g.etype[i];
    if (g.etype[i] == 0xFF) return;
    if (row >= 24) return;
    mat_put(col,     row,     b,     color, 1);
    mat_put(col,     row + 1, b + 1, color, 1);
    mat_put(col + 1, row,     b + 2, color, 1);
    mat_put(col + 1, row + 1, b + 3, color, 1);
}

static void erase_enemy_cells(int col, int row)
{
    mat_put(col,     row,     CH_EMPTY, 1, 1);
    mat_put(col,     row + 1, CH_EMPTY, 1, 1);
    mat_put(col + 1, row,     CH_EMPTY, 1, 1);
    mat_put(col + 1, row + 1, CH_EMPTY, 1, 1);
}

static void astro_init(void)
{
    int r, c, i = 0;
    int top = rank_row[g.rank];
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 8; ++c, ++i) {
            g.etype[i] = row_code[r];
            g.ex[i] = form_col[c];
            g.ey[i] = (signed char)(top + r * 2);
        }
    g.eleft     = 23;          /* $31 = $17 */
    g.stepdelay = 34;          /* $07 = $22 */
    g.steptimer = 34;
    g.animdir   = 4;           /* $90 */
    g.xdir      = 1;           /* $91 */
    g.matcount  = 24;          /* $34 */
    g.matdone   = 0;           /* $35 */
    g.gorphx     = 344; g.gorphdir = -2; g.gorphtype = 0; g.gorphtimer = 0;
    memset(g.shield, 0xFF, sizeof(g.shield));
    g.bomb_on[0] = g.bomb_on[1] = 0;
}

/* Fly-in: sprite 7 oscillates, at the trigger positions $88BD the
 * next enemy appears in the order $88A5 */
/* Y jump arc of the Gorph ship during materialization ($8895):
 * the ship hops up and down while sweeping horizontally. */
static const unsigned char gorph_yarc[16] = {
    0x32,0x32,0x33,0x33,0x34,0x35,0x35,0x36,
    0x37,0x38,0x3A,0x3B,0x3D,0x3F,0x42,0x45
};

static void astro_materialize(void)
{
    static int phase;
    int i, idx, prevx = g.gorphx;
    if (g.gorphtimer > 0) {                /* shot down: briefly gone */
        --g.gorphtimer;
        sprite_off(SP_GORPH);
        return;
    }
    g.gorphx += g.gorphdir;
    /* Robot stays in the alien/trigger area (trigger 33..201), turns just
     * past the last trigger - not to the screen edge (measured emuref). */
    if (g.gorphx < 24)  { g.gorphx = 24;  g.gorphdir = 2; }
    if (g.gorphx > 206) { g.gorphx = 206; g.gorphdir = -2; }
    ++phase;
    idx = phase & 31;                 /* Triangle 0..15..0 (pingpong) */
    if (idx > 15) idx = 31 - idx;
    set_sprite(SP_GORPH, BLK_GORPH, g.gorphx, gorph_yarc[idx], 10, 1);
    vic.spyexp = (unsigned short)(vic.spyexp | (1 << SP_GORPH));   /* 2x height */

    /* Drop exactly ONE alien at every column crossing ($88BD)
     * ($34 decrements only on a real crossing -> the alien inevitably
     * lands under the robot, $857C-$8599). Order $88A5 = bottom->top. */
    for (i = 0; i < 8; ++i) {
        int t = (int)gd_mattrig[i];
        if (g.matcount > 0 &&
            ((prevx < t && g.gorphx >= t) || (prevx > t && g.gorphx <= t))) {
            --g.matcount;
            if (g.etype[gd_matorder[g.matcount]] != 0xFF) {
                draw_enemy(gd_matorder[g.matcount]);
                sound_play(SND_STEP);
            }
        }
    }
    if (g.matcount <= 0) {
        g.matdone = 1;
        sprite_off(SP_GORPH);
        g.gorphtimer = 150 + rnd(300);
    }
}

/* March ($85B7): all objects in one go, blocked by $02 */
static void astro_march(void)
{
    int i, hitedge = 0;

    if (--g.steptimer > 0) return;
    g.steptimer = g.stepdelay;
    sound_play(SND_STEP);

    for (i = NUM_ENEMIES - 1; i >= 0; --i) {
        int oldc;
        if (g.etype[i] == 0xFF) continue;
        g.etype[i] = (unsigned char)(g.etype[i] + g.animdir);
        oldc = g.ex[i];
        g.ex[i] = (signed char)(g.ex[i] + g.xdir);
        if (g.ex[i] == 1 || g.ex[i] == 0x25) hitedge = 1;
        draw_enemy(i);
        /* clear trailing column */
        if (g.xdir > 0) {
            mat_put(oldc, g.ey[i],     CH_EMPTY, 1, 1);
            mat_put(oldc, g.ey[i] + 1, CH_EMPTY, 1, 1);
        } else {
            mat_put(oldc + 1, g.ey[i],     CH_EMPTY, 1, 1);
            mat_put(oldc + 1, g.ey[i] + 1, CH_EMPTY, 1, 1);
        }
    }
    g.animdir = -g.animdir;

    if (hitedge) {
        int top;
        g.xdir = -g.xdir;
        /* $8614: from bottom to top - each row overwrites the
         * old cells of the row above */
        for (i = NUM_ENEMIES - 1; i >= 0; --i) {
            g.ey[i] = (signed char)(g.ey[i] + 2);
            if (g.etype[i] == 0xFF) continue;
            draw_enemy(i);
        }
#ifdef GORPH_DEBUG
        {
            int k, bad = 0;
            for (k = 0; k < NUM_ENEMIES; ++k) {
                if (g.etype[k] == 0xFF) continue;
                if (vic.screen[g.ey[k] * VIC_COLS + g.ex[k]] != g.etype[k]) ++bad;
            }
            fprintf(stderr, "abstieg: top-vorher unbekannt, lebend=%d falsch=%d\n",
                    g.eleft + 1, bad);
        }
#endif
        /* $8624: clear the two topmost rows that became free */
        top = 25;
        for (i = 0; i < NUM_ENEMIES; ++i)
            if (g.etype[i] != 0xFF && g.ey[i] < top) top = g.ey[i];
        if (top >= 2 && top < 25) {
            int c;
            for (c = 0; c < VIC_COLS; ++c) {
                mat_put(c, top - 2, CH_EMPTY, 1, 1);
                mat_put(c, top - 1, CH_EMPTY, 1, 1);
            }
        }
    }

    /* $8661: ground reached -> life lost, wave over */
    for (i = 0; i < NUM_ENEMIES; ++i)
        if (g.etype[i] != 0xFF && g.ey[i] >= 22) {
            g.pdead = 1;
            sound_play(SND_PLAYER_DIE);
            player_explode();
        g.state = ST_PLAYER_HIT;
            g.statetimer = 60;
            return;
        }
}

/* $8685: enemy i falls */
/* Is alien i already SET during materialization? (only those
 * may die - otherwise an invisible one of the same column dies and
 * the robot later "sets" a corpse) */
static int mat_placed(int i)
{
    int k;
    if (g.matdone) return 1;
    for (k = g.matcount; k < NUM_ENEMIES; ++k)
        if ((int)gd_matorder[k] == i) return 1;
    return 0;
}

static void astro_kill(int i)
{
    --g.eleft;
    if (g.stepdelay > 5) --g.stepdelay;
    if (g.eleft == 8) g.stepdelay = 16;
    if (g.eleft == 0) g.stepdelay = 5;
    g.etype[i] = 0xFF;
    erase_enemy_cells(g.ex[i], g.ey[i]);
    boom_at((g.ex[i] + 1) * 8 + 0x12, (g.ey[i] + 1) * 8 + 0x2A);
    add_score(1);              /* 50 points */
}

/* Bombs ($8709): at most two, shooter random, bottom row first */
static void astro_bombs(void)
{
    int s;

    if (!g.matdone || g.pdead) goto fall;
    for (s = 0; s < 2; ++s) {
        int k, tries;
        if (g.bomb_on[s]) continue;
        if (rnd(64) > 2 + g.rank) continue;
        k = -1;
        for (tries = 0; tries < 3 && k < 0; ++tries) {
            int base = (2 - tries) * 8;      /* bottom, middle, top */
            int c = rnd(8);
            if (g.etype[base + c] != 0xFF) k = base + c;
        }
        if (k < 0) continue;
        g.bomb_on[s] = 1;
        g.bombx[s] = g.ex[k] * 8 + 0x20;
        g.bomby[s] = g.ey[k] * 8 + 0x3D;
    }

fall:
    for (s = 0; s < 2; ++s) {
        if (!g.bomb_on[s]) { sprite_off(SP_BOMB0 + s); continue; }
        g.bomby[s] += 2 + s;              /* Sprite 2: 2 px, Sprite 3: 3 px */
        if (g.bomby[s] > 0xF9) { g.bomb_on[s] = 0; sprite_off(SP_BOMB0 + s); continue; }
        set_sprite(SP_BOMB0 + s, ((g.frame >> 2) & 1) ? BLK_BOMB_B : BLK_BOMB_A,
                   g.bombx[s], g.bomby[s], 1, 0);
    }
}

/* Gorph ship ($87B6): occasionally swings past on top */
static void astro_gorphship(void)
{
    static const int gorphblk[3] = { BLK_GORPH, BLK_FLY, BLK_SAUCER };
    if (g.gorphtimer > 0) { --g.gorphtimer; sprite_off(SP_GORPH); return; }
    if (g.gorphtimer == 0) {
        g.gorphtype = rnd(3);
        g.gorphdir  = rnd(2) ? 2 : -2;
        g.gorphx    = (g.gorphdir > 0) ? 24 : 337;
        g.gorphtimer = -1;
    }
    g.gorphx += g.gorphdir;
    if (g.gorphx < 20 || g.gorphx > 340) { g.gorphtimer = 200 + rnd(400); return; }
    set_sprite(SP_GORPH, gorphblk[g.gorphtype], g.gorphx, 0x45, 10, 1);   /* $87DE: Y=$45 */
    if (g.gorphtype == 0)                  /* $8816: only type 1 2x height */
        vic.spyexp = (unsigned short)(vic.spyexp | (1 << SP_GORPH));
    else
        vic.spyexp = (unsigned short)(vic.spyexp & ~(1 << SP_GORPH));
}

/* Collision phase mission 0 ($9280): pixel-exact against the display
 * state of the last frame, cell pick from the sprite corner ($956B) */
/* Points popup (arcade look): the score appears briefly in small
 * MC font at the kill spot and fades out quickly. */
static void pop_spawn(int sx, int sy, const char *s)
{
    int i = (g.pop_t[0] <= g.pop_t[1]) ? 0 : 1;
    int col = (sx - 24) / 8, row = (sy - 50) / 8;
    if (col < 0) col = 0; if (col > 37) col = 37;
    if (row < 1) row = 1; if (row > 24) row = 24;
    g.pop_t[i] = 30;
    g.pop_x[i] = col; g.pop_y[i] = row;
    strncpy(g.pop_s[i], s, 3); g.pop_s[i][3] = '\0';
}

static void score_pops(void)
{
    int i, c;
    for (i = 0; i < 2; ++i) {
        if (g.pop_t[i] <= 0) continue;
        --g.pop_t[i];
        for (c = 0; g.pop_s[i][c]; ++c) {
            /* Fade out: last 12 frames blink (color steps do not work on
             * the narrow MC digits - pixels are bg1/bg2 pairs) */
            if (g.pop_t[i] == 0 ||
                (g.pop_t[i] <= 12 && (g.pop_t[i] & 2) == 0))
                mat_put(g.pop_x[i] + c, g.pop_y[i], CH_EMPTY, 1, 1);
            else
                mat_put(g.pop_x[i] + c, g.pop_y[i],
                        char_code(g.pop_s[i][c]), 0x0F, 1);
        }
    }
}

static void astro_collide(void)
{
    int col, i;

    /* Player touches an enemy ($10 bit 0 -> $933F) */
    if (!g.pdead && g.matdone &&
        vic_sprite_hits_code(SP_PLAYER, 0x01, 0x18)) {
        g.pdead = 1;
        sound_play(SND_PLAYER_DIE);
        player_explode();
        g.state = ST_PLAYER_HIT;
        g.statetimer = 60;
        return;
    }

    /* Bombs against shield and player */
    for (i = 0; i < 2; ++i) {
        if (!g.bomb_on[i]) continue;
        if (vic_sprite_hits_code(SP_BOMB0 + i, 0x19, 0x1D)) {
            shield_hit_col((g.bombx[i] - 24) >> 3);   /* column from corner */
            g.bomb_on[i] = 0;
            sprite_off(SP_BOMB0 + i);
            sound_play(SND_HIT);
            continue;
        }
        if (!g.pdead && vic_sprites_overlap(SP_PLAYER, SP_BOMB0 + i)) {
            g.bomb_on[i] = 0;
            sprite_off(SP_BOMB0 + i);
            g.pdead = 1;
            sound_play(SND_PLAYER_DIE);
            player_explode();
        g.state = ST_PLAYER_HIT;
            g.statetimer = 60;
        }
    }

    if (g.shot != 2) return;

    /* During materialization both the robot and the aliens already set
     * can be shot (as in the original). Robot hits via box; on a miss it
     * falls through to the formation check (bombs/free-flying ship are
     * inactive here). */
    if (!g.matdone) {
        int dx = g.shotx - g.gorphx;
        int dy = g.shoty - vic.spy[SP_GORPH];
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (g.gorphtimer == 0 && dx < 0x14 && dy < 0x18) {   /* robot hittable */
            boom_at(g.gorphx, vic.spy[SP_GORPH]);
            add_score(5);                        /* 250 ($9326: Mat = -1 level) */
            pop_spawn(g.gorphx, vic.spy[SP_GORPH], "250");
            g.gorphtimer = 100;                   /* robot briefly gone, then back */
            g.shot = -1;
            sprite_off(SP_SHOT);
            sprite_off(SP_GORPH);
            return;
        }
        /* no robot -> on to the formation check below */
    }

    /* shot against bombs and Gorph ship (D01E pairing) */
    for (i = 0; i < 2; ++i)
        if (g.bomb_on[i] && vic_sprites_overlap(SP_SHOT, SP_BOMB0 + i)) {
            g.bomb_on[i] = 0;
            sprite_off(SP_BOMB0 + i);
            g.shot = -1;
            sprite_off(SP_SHOT);
            sound_play(SND_HIT);
            return;
        }
    if (g.gorphtimer < 0 && vic_sprites_overlap(SP_SHOT, SP_GORPH)) {
        /* $935F: robot 300, fly 100, saucer 200 */
        static const int gorph_sc[3] = { 6, 2, 4 };
        static const char *gorph_ps[3] = { "300", "100", "200" };
        boom_at(g.gorphx, 0x45);
        add_score(gorph_sc[g.gorphtype]);
        pop_spawn(g.gorphx, 0x45, gorph_ps[g.gorphtype]);
        g.gorphtimer = 300 + rnd(400);
        sprite_off(SP_GORPH);
        g.shot = -1;
        sprite_off(SP_SHOT);
        return;
    }

    /* Shot against the formation: pixel-exact gate, then as in $92A6
     * only the column compare - the lowest living one falls */
    if (!vic_sprite_hits_code(SP_SHOT, 0x01, 0x18)) return;
    col = (g.shotx - 24) >> 3;
    for (i = NUM_ENEMIES - 1; i >= 0; --i) {
        if (g.etype[i] == 0xFF) continue;
        if (!mat_placed(i)) continue;
        if (g.ex[i] == col || g.ex[i] + 1 == col) {
            astro_kill(i);
            g.shot = -1;
            sprite_off(SP_SHOT);
            return;
        }
    }
}

static void astro_update(void)
{
    score_pops();                         /* Score popups (also during Mat) */
    if (!g.matdone) { astro_materialize(); return; }
    shield_flicker();
    shield_draw(0);
    astro_march();
    if (g.state != ST_PLAY) return;
    astro_bombs();
    astro_gorphship();
    if (g.eleft < 0) { g.state = ST_MISSION_CLEAR; g.statetimer = 120; }
}

/* ===================================================================== */
/*  Laser Attack (Mission 1) - Bitmap mode                               */
/* ===================================================================== */

/* $891E: spawn interval per rank */
static const int laser_step[8] = { 96, 72, 48, 24, 24, 8, 8, 32 };
/* $8C54/$8C5C: formation slots relative to the cannon (slot 3 = robot top) */
static const int lm_offx[4] = { 0, 24, 48, 24 };
static const int lm_offy[4] = { 32, 32, 32, 12 };
/* $8C64/$8C6C: re-entry drift and sink rate */
static const signed char lm_drift[8] = { 0, 2, 2, -2, -2, 2, 0, -2 };
static const signed char lm_sink[8]  = { 2, 2, 4, 2, 4, 2, 4, 4 };
/* $8C74: respawn X top (sprite coordinate - 24) */
static const int lm_respx[4] = { 0x18, 0x62, 0xAC, 0xFF };

#define LASER_SPEED 2        /* 2 px/frame ($89CF: 4 px in 2 frames) */
#define LASER_BOT 190        /* lowest beam line (line $2F) */

static void laser_draw_player(int erase)
{
    shape_blit(0x00, g.bx,     g.by, 24, 3, erase);
    shape_blit(0x06, g.bx + 8, g.by, 24, 3, erase);
}

/* draw/clear cannon (movable bitmap blob) */
static void laser_turret_blit(int s, int erase)
{
    shape_blit(0x0F, g.tur_x[s],     g.tur_y[s] + 16, 16, 2, erase);
    shape_blit(0x15, g.tur_x[s] + 8, g.tur_y[s] + 16, 16, 2, erase);
}

static void laser_draw_static(void)
{
    int s;
    for (s = 0; s < 2; ++s)
        if (g.turret[s]) laser_turret_blit(s, 0);
    laser_draw_player(0);
}

/* formation slot of member m (sprite coordinates) */
static int lm_slot_x(int m) { return g.tur_x[m / 4] * 2 + 24 + lm_offx[m & 3]; }
static int lm_slot_y(int m) { return g.tur_y[m / 4] + lm_offy[m & 3]; }

/* $9BF2: quantized 8-direction at the player - one-shot, NO homing */
static void laser_aim(int m)
{
    int dx = (g.bx * 2 + 24 + 12) - g.lm_x[m];
    int dy = (g.by + 50)          - g.lm_y[m];
    int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    int ax = dx < 0 ? -dx : dx,   ay = dy < 0 ? -dy : dy;
    if (sy == 0) sy = 1;
    if (ax == ay || ax == 0 || ay == 0) {
        g.lm_dx[m] = (signed char)(2 * sx); g.lm_dy[m] = (signed char)(2 * sy);
    } else if (ax < ay) {
        g.lm_dx[m] = (signed char)(1 * sx); g.lm_dy[m] = (signed char)(3 * sy);
    } else {
        g.lm_dx[m] = (signed char)(3 * sx); g.lm_dy[m] = (signed char)(1 * sy);
    }
}

/* member m detaches and dives ($8B5D/$8B71) */
static void laser_attack(int m)
{
    g.lm_state[m] = 1;
    laser_aim(m);
    sound_play(SND_SIREN);    /* $F5=$11: siren warble on detach */
}

/* $8B37: respawn at top as attacker (never returns again) */
static void laser_redive(int m)
{
    g.lm_x[m] = 24 + lm_respx[rnd(4)];
    g.lm_y[m] = 50 + 0x1F;
    laser_attack(m);
}

/* $8AB1: thread in at top - the diagonal lands exactly on the slot */
static void laser_reenter(int m)
{
    int r, dist, off;
    if (!g.turret[m / 4]) { laser_redive(m); return; }
    r = rnd(8);
    g.lm_dx[m] = lm_drift[r];
    g.lm_dy[m] = lm_sink[r];
    g.lm_y[m] = 50;
    dist = lm_slot_y(m) - 50;
    off  = (g.lm_dy[m] == 4) ? dist / 2 : dist;   /* $8B17-$8B1E */
    if (g.lm_dx[m] > 0)      g.lm_x[m] = lm_slot_x(m) - off;
    else if (g.lm_dx[m] < 0) g.lm_x[m] = lm_slot_x(m) + off;
    else                     g.lm_x[m] = lm_slot_x(m);
    g.lm_state[m] = 2;
}

/* $8B5D: spawn tick - a random seated member detaches */
static void laser_spawn(void)
{
    int m;
    if (g.pdead) return;                  /* $892B: not during explosion */
    if (--g.lspawn > 0) return;
    g.lspawn = laser_step[g.rank];
    m = rnd(8);
    if (g.lm_state[m] == 0) laser_attack(m);
}

/* cannon s fires its own beam downward */
static void laser_fire_from(int s)
{
    if (g.laser_on[s]) return;
    g.laser_on[s] = 1;
    g.laserx[s] = g.tur_x[s] + 5;         /* $899C: column = X+5 */
    g.lasery[s] = g.tur_y[s] + 33;
    g.lasertimer[s] = (LASER_BOT - g.lasery[s]) / LASER_SPEED + 1;
    sound_play(SND_BEAM);
}

/* $8A43: roll a new course - X toward the player (rank 0: fixed $45),
 * Y toward $42; (0,0) forbidden */
static void laser_cannon_aim(int k)
{
    int tx = (g.rank == 0) ? 0x45 : g.bx;
    do {
        g.tur_vx[k] = (signed char)(rnd(4) * 2);
        g.tur_vy[k] = (signed char)(rnd(4) * 2);
    } while (!g.tur_vx[k] && !g.tur_vy[k]);
    if (g.tur_x[k] > tx)   g.tur_vx[k] = (signed char)-g.tur_vx[k];
    if (g.tur_y[k] > 0x42) g.tur_vy[k] = (signed char)-g.tur_vy[k];
    g.tur_timer[k] = 1 + rnd(31);
    g.tur_state[k] = 1;
}

static void laser_init(void)
{
    int s, m;
    bitmap_clear();
    stars_bitmap_all();
    g.bx = 72;  g.by = 168;              /* $4C/$4D */
    for (s = 0; s < 2; ++s) {
        g.turret[s] = 1;
        g.tur_x[s] = 32 + s * 76;        /* $50=$20 / $51=$6C */
        g.tur_y[s] = 64;                 /* $52/$53=$40 */
        g.laser_on[s] = 0;
        /* $8905 -> $899C: both cannons start STANDING in beam mode */
        g.tur_state[s] = 0;
        g.tur_vx[s] = g.tur_vy[s] = 0;
        g.tur_timer[s] = 0;
        laser_fire_from(s);
    }
    for (m = 0; m < 8; ++m) {
        g.lm_state[m] = 0;
        g.lm_boom[m] = 0;
        g.lm_x[m] = lm_slot_x(m);
        g.lm_y[m] = lm_slot_y(m);
        g.lm_dx[m] = g.lm_dy[m] = 0;
    }
    g.eleft = 9;                          /* 8 members, 2 cannons */
    g.lspawn = laser_step[g.rank];
    laser_draw_static();
}

/* $8961: cannon moves; state-0 members ride along ($8A84). Stopping
 * (timer/limit) -> beam ($899C). Each cannon only in its own frame. */
static void laser_cannons(void)
{
    int k = g.frame & 1;
    int m, nx, ny, stop;
    if (!g.turret[k]) return;
    if (!g.tur_state[k]) {
        /* $89DF-$89E3: beam done -> new course at once. Also catches the
         * case where the beam was cleared externally (respawn). */
        if (!g.laser_on[k]) laser_cannon_aim(k);
        return;
    }
    stop = (--g.tur_timer[k] <= 0);
    nx = g.tur_x[k] + g.tur_vx[k];
    ny = g.tur_y[k] + g.tur_vy[k];
    if (nx < 0x0E || nx >= 0x8A - 16) { nx = g.tur_x[k]; stop = 1; }
    if (ny < 0x26 || ny >= 0x78)      { ny = g.tur_y[k]; stop = 1; }
    if (nx != g.tur_x[k] || ny != g.tur_y[k]) {
        laser_turret_blit(k, 1);
        for (m = k * 4; m < k * 4 + 4; ++m)
            if (g.lm_state[m] == 0) {
                g.lm_x[m] += (nx - g.tur_x[k]) * 2;
                g.lm_y[m] +=  ny - g.tur_y[k];
            }
        g.tur_x[k] = nx; g.tur_y[k] = ny;
        laser_turret_blit(k, 0);
    }
    if (stop) {
        g.tur_state[k] = 0;
        g.tur_vx[k] = g.tur_vy[k] = 0;
        laser_fire_from(k);
    }
}

/* Member explosion sequences - keeps running during PLAYER_HIT and
 * MISSION_CLEAR too (otherwise the last explosion freezes). */
static void laser_booms(void)
{
    static const int seq[4] = { 22, 23, 25, 26 };
    int m;
    for (m = 0; m < 8; ++m) {
        if (g.lm_boom[m] <= 0) continue;
        --g.lm_boom[m];
        if (!g.lm_boom[m]) { sprite_off(m); continue; }
        set_sprite(m, seq[3 - g.lm_boom[m] / 4], g.lm_x[m], g.lm_y[m], 7, 1);
    }
}

/* All 8 members: formation / dive / return; ramming kills the player.
 * Rank < 3: member movement only every 2nd frame ($8932). */
static void laser_members(void)
{
    int m, k, tick = (g.rank >= 3) || !(g.frame & 1);
    for (m = 0; m < 8; ++m) {
        int mx, my, ddx, ddy;
        k = m / 4;
        if (g.lm_boom[m] > 0) continue;   /* explosion runs in laser_booms */
        if (g.lm_state[m] == 0xFF) { sprite_off(m); continue; }
        if (tick) {
            if (g.lm_state[m] == 1) {          /* dive, straight */
                g.lm_x[m] += g.lm_dx[m];
                g.lm_y[m] += g.lm_dy[m];
                if (g.lm_x[m] < 20 || g.lm_x[m] > 343 ||
                    g.lm_y[m] < 29 || g.lm_y[m] > 249) {
                    if (g.rank < 5) laser_reenter(m);   /* $8BC9 */
                    else            laser_redive(m);
                }
            } else if (g.lm_state[m] == 2) {   /* descent to formation */
                /* $8BD6: X 2x cannon-vx only at rank<3 (half-rate offset),
                 * Y halved at rank>=3. Cannon step counts only while
                 * the cannon is actually moving (else stale drift). */
                int mv = g.turret[k] && g.tur_state[k] == 1;
                int xs = !mv ? 0 : (g.rank < 3) ? g.tur_vx[k] * 2 : g.tur_vx[k];
                int ys = !mv ? 0 : (g.rank < 3) ? g.tur_vy[k]     : g.tur_vy[k] / 2;
                g.lm_x[m] += xs + g.lm_dx[m];
                g.lm_y[m] += ys + g.lm_dy[m];
                if (g.lm_y[m] >= lm_slot_y(m)) {    /* lock in ($8C15) */
                    g.lm_x[m] = lm_slot_x(m);
                    g.lm_y[m] = lm_slot_y(m);
                    g.lm_state[m] = 0;
                } else if (g.lm_x[m] < 20 || g.lm_x[m] > 343 ||
                           g.lm_y[m] < 29) {
                    /* drifted out (cannon pulled member away) -> thread
                     * back at the top instead of vanishing forever */
                    laser_reenter(m);
                }
            }
        }
        set_sprite(m, ((m & 3) == 3) ? BLK_LEADER : BLK_FIGHTER,
                   g.lm_x[m], g.lm_y[m], mis_spcol[1][m], 1);
        /* $93D7: attacker rams player -> both die */
        if (!g.pdead && g.lm_state[m] == 1) {
            mx = (g.lm_x[m] - 24) / 2 + 6;
            my = g.lm_y[m] - 50 + 10;
            ddx = mx - (g.bx + 8);  if (ddx < 0) ddx = -ddx;
            ddy = my - (g.by + 12); if (ddy < 0) ddy = -ddy;
            if (ddx < 10 && ddy < 12) {
                g.lm_state[m] = 0xFF;
                g.lm_boom[m] = 16;
                --g.eleft;
                g.pdead = 1;
                sound_play(SND_PLAYER_DIE);
                player_explode();
                g.state = ST_PLAYER_HIT;
                g.statetimer = 60;
            }
        }
    }
}

/* Beam phases as in ROM: laser_on 1 = grows downward, 2 = is erased
 * again from the top at once ($89E8: erase pass, same rate). */
static void laser_beams(void)
{
    int s, y;
    for (s = 0; s < 2; ++s) {
        int total, elapsed, edge, dx;
        if (!g.laser_on[s]) continue;
        total = (LASER_BOT - g.lasery[s]) / LASER_SPEED + 1;
        elapsed = total - g.lasertimer[s];
        edge = g.lasery[s] + elapsed * LASER_SPEED;
        if (edge > LASER_BOT) edge = LASER_BOT;
        --g.lasertimer[s];
        if (g.laser_on[s] == 1) {             /* grow phase: head sinks */
            for (y = g.lasery[s]; y < edge; ++y) plot(g.laserx[s], y, 1, 1);
            if (g.lasertimer[s] <= 0) {       /* reached bottom -> erase */
                g.laser_on[s] = 2;
                g.lasertimer[s] = total;
            }
        } else {                              /* erase pass from top ($8A38) */
            for (y = g.lasery[s]; y < edge; ++y) plot(g.laserx[s], y, 0, 1);
            if (g.lasertimer[s] <= 0) {
                for (y = g.lasery[s]; y < LASER_BOT; ++y) plot(g.laserx[s], y, 0, 1);
                g.laser_on[s] = 0;
                if (g.turret[s]) laser_cannon_aim(s);  /* $8A43: keep moving */
                continue;
            }
        }
        if (!g.pdead) {
            int top = (g.laser_on[s] == 2) ? edge : g.lasery[s];
            int bot = (g.laser_on[s] == 2) ? LASER_BOT : edge;
            dx = g.laserx[s] - (g.bx + 8); if (dx < 0) dx = -dx;
            if (dx < 8 && bot >= g.by && g.by + 8 >= top) {
                g.pdead = 1;
                sound_play(SND_PLAYER_DIE);
                player_explode();
                g.state = ST_PLAYER_HIT;
                g.statetimer = 60;
            }
        }
    }
}

static void laser_shot_hits(void)
{
    int s, m;
    if (g.shot != 1) return;
    for (s = 0; s < 2; ++s) {                 /* cannons (hitbox moves) */
        int tx, ty;
        if (!g.turret[s]) continue;
        tx = g.tur_x[s]; ty = g.tur_y[s];
        if (g.shotx - tx >= -2 && g.shotx - tx < 0x12 &&
            g.shoty - ty >= 0x0C && g.shoty - ty < 0x22) {
            g.turret[s] = 0;
            laser_turret_blit(s, 1);
            boom_at(tx * 2 + 24, ty + 16 + 50);
            add_score(6);                     /* cannon 300 ($9438) */
            --g.eleft;
            /* $942E: all seated members of this cannon dive at once */
            for (m = s * 4; m < s * 4 + 4; ++m)
                if (g.lm_state[m] == 0) laser_attack(m);
            shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 1);
            g.shot = -1;
            return;
        }
    }
    for (m = 0; m < 8; ++m) {                 /* member ($95B1: 11 x 16) */
        int mx, my, dx, dy;
        if (g.lm_state[m] == 0xFF || g.lm_boom[m]) continue;
        mx = (g.lm_x[m] - 24) / 2;
        my = g.lm_y[m] - 50;
        dx = g.shotx - mx; if (dx < 0) dx = -dx;
        dy = g.shoty - my; if (dy < 0) dy = -dy;
        if (dx < 0x0B && dy < 0x10) {
            g.lm_state[m] = 0xFF;             /* slot stays empty for good */
            g.lm_boom[m] = 16;
            --g.eleft;
            add_score(2);                        /* all members 100 ($93D4) */
            sound_play(SND_HIT);
            shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 1);
            g.shot = -1;
            return;
        }
    }
}

static void laser_update(void)
{
    star_twinkle();
    laser_spawn();
    laser_cannons();
    laser_booms();
    laser_members();
    laser_beams();
    laser_shot_hits();
    /* sprite 4 gets overdrawn/disabled by member 4 - re-assert the
     * cannon explosion afterwards (explosion takes priority) */
    boom_draw();
    if (g.eleft < 0) { g.state = ST_MISSION_CLEAR; g.statetimer = 120; }
}

/* ===================================================================== */
/*  Space Warp (Mission 2) - bitmap mode                                 */
/* ===================================================================== */

/* sprite blocks Space Warp / Flag Ship */
#define BLK_STONE   9      /* rock, tumble frames 9/10/11 ($49..$4B) */
#define BLK_PIECE   7      /* breaking-off fragment                  */

/* rock vectors ($9192 / $919A) - always downward */
static const signed char stone_dx[8] = { 0, 0,  1, -1,  2, -2,  3, -4 };
static const signed char stone_dy[8] = { 2, 2,  3,  3,  2,  2,  1,  1 };

/* slope denominator ($987C): slow axis steps every (slope+1) frames */
static const unsigned char warp_slope[8] = { 0, 1, 2, 3, 5, 6, 7, 127 };

/* vanishing point in multicolor px (original (80,75)) */
#define WARP_CX 80
#define WARP_CY 75


/* shared rock output (sprites), n slots on sp[] */
static void stones_update(const int *sp, int n)
{
    int s;
    for (s = 0; s < n; ++s) {
        if (!g.stone_on[s]) { sprite_off(sp[s]); continue; }
        if (!(g.frame & 1)) {             /* half rate (~80 px/s, video) */
            g.stone_x[s] += g.stone_vx[s];
            g.stone_y[s] += g.stone_vy[s];
        }
        if (--g.stone_life[s] <= 0 || g.stone_y[s] > 250 ||
            g.stone_x[s] < 8 || g.stone_x[s] > 344) {
            g.stone_on[s] = 0; sprite_off(sp[s]); continue;
        }
        /* no rotation: rocks grow bigger with depth (video) */
        set_sprite(sp[s],
                   BLK_STONE + ((g.stone_y[s] < 130) ? 0 :
                                (g.stone_y[s] < 180) ? 1 : 2),
                   g.stone_x[s], g.stone_y[s], 8, 1);
        if (!g.pdead && vic_sprites_overlap(SP_PLAYER, sp[s])) {
            g.stone_on[s] = 0; sprite_off(sp[s]);
            g.pdead = 1; sound_play(SND_PLAYER_DIE);
            player_explode();
        g.state = ST_PLAYER_HIT; g.statetimer = 60;
        }
    }
}

static int stone_spawn(int x, int y, int aimed, int n)
{
    int s, a, vx, vy;
    for (s = 0; s < n; ++s) if (!g.stone_on[s]) break;
    if (s >= n) return 0;
    if (aimed) {                          /* $9BE0: quantized 8-direction */
        int dxp = g.px - x, dyp = g.py - y;
        int sx = (dxp > 0) - (dxp < 0), sy = (dyp > 0) - (dyp < 0);
        int axp = dxp < 0 ? -dxp : dxp, ayp = dyp < 0 ? -dyp : dyp;
        if (sy <= 0) sy = 1;
        if (axp == ayp || !axp || !ayp) { vx = 2 * sx; vy = 2 * sy; }
        else if (axp > ayp)             { vx = 3 * sx; vy = 1 * sy; }
        else                            { vx = 1 * sx; vy = 3 * sy; }
    } else {
        a = rnd(8);                       /* $9192/$919A random vectors */
        vx = stone_dx[a]; vy = stone_dy[a];
    }
    g.stone_on[s] = 1;
    g.stone_x[s] = x; g.stone_y[s] = y;
    g.stone_vx[s] = (signed char)vx;
    g.stone_vy[s] = (signed char)vy;
    g.stone_life[s] = 70;
    return 1;
}

/* DDA straight-line model ($97CB): each object draws a fixed straight
 * line from vanishing point at constant speed. flags: bit3=fast axis (1=X),
 * bit7=X-direction, bit6=Y-direction, bit0=plot. slope = slow-axis divider.
 * direction/axis/slope stay constant in flight -> star explosion. */
static void warp_respawn(int i)
{
    g.wx[i] = WARP_CX; g.wy[i] = WARP_CY;
    g.wflags[i] = (unsigned char)rnd(256);
    g.wtype[i]  = warp_slope[rnd(8)];
    g.wsub[i]   = (signed char)g.wtype[i];
}

static void warp_init(void)
{
    int i;
    g.wmis_cool = 50;                     /* first toss pause */
    bitmap_clear();                       /* ONCE only - trails remain */
    for (i = 0; i < 24; ++i) { warp_respawn(i); g.wflags[i] |= 1; }
    stars_bitmap_all();                   /* white stars */
    g.eleft = (g.rank == 0) ? 12 : 16;    /* $8CA1 / $825E[2]=$10 */
    memcpy(g.warp_shape, gd_sprites + 21 * 64, 64);   /* Ring = Block 21 */
    if (g.rank == 0) {                    /* $8CA6: marker preset $AA */
        g.warp_shape[0x10] = 0xAA;
        g.warp_shape[0x16] = 0xAA;
    }
    g.wobj_on = 0; g.wobj_boom = 0;
    for (i = 0; i < 4; ++i) g.stone_on[i] = 0;
    g.warptimer = 0;
}

static void warp_step_obj(int i)
{
    int dx = (g.wflags[i] & 0x80) ? 1 : -1;
    int dy = (g.wflags[i] & 0x40) ? 1 : -1;
    if (g.wflags[i] & 0x08) {             /* X = fast axis */
        if (--g.wsub[i] < 0) { g.wy[i] += dy; g.wsub[i] = (signed char)g.wtype[i]; }
        g.wx[i] += dx;
    } else {                             /* Y = fast axis */
        if (--g.wsub[i] < 0) { g.wx[i] += dx; g.wsub[i] = (signed char)g.wtype[i]; }
        g.wy[i] += dy;
    }
    if (g.wx[i] <= 0 || g.wx[i] >= 160 || g.wy[i] <= 0 ||
        g.wy[i] >= 190 || g.wy[i] < 9) { warp_respawn(i); return; }
    if (g.wflags[i] & 1) plot(g.wx[i], g.wy[i], 3, 0);   /* trail stays put */
    else plot(g.wx[i], g.wy[i], 0, 1);   /* erase star eats trails (video) */
}

/* ===== Warp object (Sprite 2) + Missiles (Sprites 4-6) =============== */
/* spiral path: vector rotates per segment (sin/cos $8DC3/$8DE3, segment
 * lengths $8E03, angle index $20->0; index 0 = long straight final run). */
static const signed char warp_dx[32] = {
    1,1,1,2,2,3,4,2,4,2,2,1,1,0,-1,-2,-2,-2,-2,-2,-1,0,1,3,2,1,1,-1,-1,-3,-1,-1 };
static const signed char warp_dy[32] = {
    4,3,2,2,1,1,1,0,-1,-1,-2,-2,-3,-2,-3,-2,-1,0,1,2,3,2,2,1,0,-1,-3,-3,-1,-1,1,3 };
static const unsigned char warp_seg[32] = {
    96,8,14,14,9,8,7,14,4,9,7,9,7,12,8,6,8,11,6,6,4,6,7,4,6,7,3,2,5,2,3,2 };
static const unsigned char warp_quad[4] = { 0x00, 0x40, 0x80, 0xC0 };

/* $8DBF: start shape per type (ptr $4C/$4F/$52 = blocks 12/15/18),
 * growth = stage++ at angle index $16/$0C ($8D00 INC $43FA). */
static const int wobj_blk[3] = { 12, 15, 18 };
/* $A0AC: explosion sequence (ptr $5A,$59,$57,$56 = blocks 26,25,23,22) */
static const int wobj_boomseq[4] = { 26, 25, 23, 22 };
/* $8E23/$8E33: ring marker - EOR mask/byte offset in ring shape */
static const unsigned char wring_msk[16] = {
    0x0C,0xC0,0x30,0x30,0xC0,0x0C,0xC0,0x0C,0x30,0x30,0x0C,0xC0,0x0C,0x0C,0xC0,0xC0 };
static const unsigned char wring_off[16] = {
    0x07,0x0B,0x11,0x17,0x1D,0x1F,0x1F,0x1B,0x15,0x0F,0x09,0x07,0x10,0x16,0x16,0x10 };

#define SP_WOBJ  2
#define SP_WRING 7
#define WOBJ_CX  0xB8
#define WOBJ_CY  0x7D

static void set_sprite_data(int i, const unsigned char *data, int x, int y, int col);

static void wobj_spawn(void)
{
    g.wobj_type  = rnd(3);                /* $8D5C type 1..3 */
    g.wobj_frame = 0;
    g.wobj_quad  = warp_quad[rnd(4)];     /* $8DBC sign */
    g.wobj_swap  = (unsigned char)rnd(2); /* spin sense (table swap $8D6D) */
    g.wobj_angle = 31;                    /* $79 = $20 */
    g.wobj_timer = warp_seg[31];
    g.wobj_x = WOBJ_CX;
    g.wobj_y = WOBJ_CY;
    g.wobj_on = 1;
}

/* ring sprite (block 21) at center ($D00E/$D00F=$AD/$70), Y-expanded,
 * blue ($D02E=6); marker cleared from the work copy per ejection. */
static void warp_ringsprite(void)
{
    /* area (%10) = sprite color 0 black -> "black hole";
     * dot ring (%01) = MC0 yellow */
    set_sprite_data(SP_WRING, g.warp_shape, 0xAD, 0x70, 0);
    vic.spyexp = (unsigned short)(vic.spyexp | (1 << SP_WRING));
}

/* warp object: ejection, spiral flight, growth, collisions ($8CE2/$943B) */
static void warp_object(void)
{
    int idx, dx, dy, ax, ay;
    if (g.wobj_boom > 0) {                /* explosion seq, 4 frames each */
        --g.wobj_boom;
        if (!g.wobj_boom) { sprite_off(SP_WOBJ); return; }
        set_sprite(SP_WOBJ, wobj_boomseq[3 - g.wobj_boom / 4],
                   g.wobj_x, g.wobj_y, 7, 1);
        return;
    }
    if (!g.wobj_on) {                     /* $8D3D: next ejection */
        --g.eleft;
        if (g.eleft < 0) return;
        if (g.eleft < 16)                 /* toggle ring marker ($8D41) */
            g.warp_shape[wring_off[g.eleft]] ^= wring_msk[g.eleft];
        wobj_spawn();
        sound_play(SND_EJECT);
        return;
    }
    /* Rank 0: the WHOLE object move only every 2nd frame ($8CEA gate sits
     * before the complete block -> ~25-75 px/s as in the video).
     * Rank 1: 3/4 rate as intermediate (full rate was too harsh a jump) */
    if ((g.rank == 0) ? !(g.frame & 1)
        : (g.rank == 1) ? ((g.frame & 3) != 3) : 1) {
        if (--g.wobj_timer <= 0 && g.wobj_angle > 0) {
            --g.wobj_angle;
            if (g.wobj_angle == 0x16 || g.wobj_angle == 0x0C)
                if (g.wobj_frame < 2) ++g.wobj_frame;   /* growth stage */
            g.wobj_timer = warp_seg[g.wobj_angle];
        }
        idx = g.wobj_angle & 31;
        dx = g.wobj_swap ? warp_dy[idx] : warp_dx[idx];
        dy = g.wobj_swap ? warp_dx[idx] : warp_dy[idx];
        if (!(g.wobj_quad & 0x80)) dx = -dx;
        if (!(g.wobj_quad & 0x40)) dy = -dy;
        g.wobj_x += dx;
        g.wobj_y += dy;
        if (g.wobj_x <= 0 || g.wobj_x > 0x157 ||
            g.wobj_y < 0x1D || g.wobj_y > 0xF9) {   /* window exit */
            g.wobj_on = 0; sprite_off(SP_WOBJ);
            return;
        }
    }
    set_sprite(SP_WOBJ, wobj_blk[g.wobj_type] + g.wobj_frame,
               g.wobj_x, g.wobj_y, 1, 1);       /* white ($D029=$01) */
    /* player collision (tighter than full sprite) */
    ax = g.wobj_x - g.px; if (ax < 0) ax = -ax;
    ay = g.wobj_y - g.py; if (ay < 0) ay = -ay;
    if (!g.pdead && ax < 0x10 && ay < 0x12) {
        g.pdead = 1;
        sound_play(SND_PLAYER_DIE);
        player_explode();
        g.state = ST_PLAYER_HIT;
        g.statetimer = 60;
        return;
    }
    /* shot collision: pixel-exact ($D01E semantics) */
    if (g.shot == 2) {
        if (vic_sprites_overlap(SP_SHOT, SP_WOBJ)) {
            add_score(5);                 /* 250 */
            g.wobj_on = 0;
            g.wobj_boom = 16;
            g.shot = -1; sprite_off(SP_SHOT);
            sound_play(SND_HIT);
        }
    }
}

/* Missiles: the object fires rocks at the player ($9084 spawner,
 * $91A2 movement; shape anim blocks 9-11 every 24 frames). */
static const int wmis_sp[3]    = { 4, 5, 6 };
static const int wmis_slots[6] = { 1, 1, 2, 2, 3, 3 };   /* $9261 */
static const int wmis_rate[6]  = { 7, 7, 3, 3, 1, 0 };   /* $9267 */

static void warp_missiles(void)
{
    int s, nmax = wmis_slots[g.rank];
    /* spawner: only if object active and player alive ($32 gate);
     * cooldown between throws (else steady fire = "aims too sharply") */
    if (g.wmis_cool > 0) --g.wmis_cool;
    if (g.wobj_on && !g.pdead && g.state == ST_PLAY && g.wmis_cool <= 0) {
        for (s = 0; s < nmax; ++s) if (!g.stone_on[s]) break;
        if (s < nmax) {
            int vx, vy;
            if ((g.frame & wmis_rate[g.rank]) == wmis_rate[g.rank]) {
                /* aimed ($9BE0): larger axis 3, smaller 1; equal 2/2 */
                int dxp = g.px - g.wobj_x, dyp = g.py - (g.wobj_y + 10);
                int sx = (dxp > 0) - (dxp < 0), sy = (dyp > 0) - (dyp < 0);
                int axp = dxp < 0 ? -dxp : dxp, ayp = dyp < 0 ? -dyp : dyp;
                if (axp == ayp || !axp || !ayp) { vx = 2 * sx; vy = 2 * sy; }
                else if (axp > ayp)             { vx = 3 * sx; vy = 1 * sy; }
                else                            { vx = 1 * sx; vy = 3 * sy; }
            } else {
                int r = rnd(8);           /* $9192/$919A */
                vx = stone_dx[r]; vy = stone_dy[r];
            }
            g.stone_on[s] = 1;
            g.stone_x[s] = g.wobj_x;
            g.stone_y[s] = g.wobj_y + 10;
            g.stone_vx[s] = (signed char)vx;
            g.stone_vy[s] = (signed char)vy;
            g.stone_frame[s] = 0;
            g.wmis_cool = 22 + rnd(20);   /* ~0.4-0.8s pause until the next */
        }
    }
    for (s = 0; s < 3; ++s) {
        int ax, ay;
        if (!g.stone_on[s]) { sprite_off(wmis_sp[s]); continue; }
        /* rank 0 half, rank 1 3/4 rate (intermediate), else full */
        if ((g.rank == 0) ? !(g.frame & 1)
            : (g.rank == 1) ? ((g.frame & 3) != 3) : 1) {
            g.stone_x[s] += g.stone_vx[s];
            g.stone_y[s] += g.stone_vy[s];
        }
        if (++g.stone_frame[s] >= 72) g.stone_frame[s] = 0;
        if (g.stone_x[s] <= 0 || g.stone_x[s] > 0x157 ||
            g.stone_y[s] < 0x1D || g.stone_y[s] > 0xF9) {
            g.stone_on[s] = 0; sprite_off(wmis_sp[s]); continue;
        }
        set_sprite(wmis_sp[s], BLK_STONE + g.stone_frame[s] / 24,
                   g.stone_x[s], g.stone_y[s], 8, 1);
        ax = g.stone_x[s] - g.px; if (ax < 0) ax = -ax;
        ay = g.stone_y[s] - g.py; if (ay < 0) ay = -ay;
        if (!g.pdead && ax < 0x12 && ay < 0x10) {
            g.stone_on[s] = 0; sprite_off(wmis_sp[s]);
            g.pdead = 1;
            sound_play(SND_PLAYER_DIE);
            player_explode();
            g.state = ST_PLAYER_HIT;
            g.statetimer = 60;
        }
    }
}

static void warp_update(void)
{
    static int rr;
    int k;
    ++g.warptimer;
    /* 24 stars (straight DDA radial trails), 8/frame round-robin */
    for (k = 0; k < 8; ++k) { rr = (rr + 23) % 24; warp_step_obj(rr); }
    star_twinkle();
    warp_object();
    warp_missiles();
    warp_ringsprite();
    if (g.eleft < 0 && !g.wobj_on && g.wobj_boom <= 0) {
        g.state = ST_MISSION_CLEAR;
        g.statetimer = 120;
    }
}

/* ===================================================================== */
/*  Flag Ship (Mission 3) - text mode                                    */
/* ===================================================================== */

#define SP_PIECE 7            /* breaking-off part (Flag Ship) */
#define FLAG_YMIN 0x50
#define FLAG_YMAX 0x6C

/* MC pixel value (0..3) at data pos (col hires 0..23, row 0..20) */
static int spr_mc(const unsigned char *d, int col, int row)
{
    int mc = col >> 1, b, sh;
    if (col < 0 || col > 23 || row < 0 || row > 20) return 0;
    b = row * 3 + (mc >> 2); sh = (3 - (mc & 3)) * 2;
    return (d[b] >> sh) & 3;
}
/* clear MC pixel */
static void spr_clr(unsigned char *d, int col, int row)
{
    int mc = col >> 1, b, sh;
    if (col < 0 || col > 23 || row < 0 || row > 20) return;
    b = row * 3 + (mc >> 2); sh = (3 - (mc & 3)) * 2;
    d[b] = (unsigned char)(d[b] & ~(3 << sh));
}
/* horizontally mirrored copy (reverse 12 MC pixels per line) */
static void spr_mirror(unsigned char *dst, const unsigned char *src)
{
    int row, mc;
    memset(dst, 0, 64);
    for (row = 0; row < 21; ++row)
        for (mc = 0; mc < 12; ++mc) {
            int sb = row*3 + (mc>>2), ss = (3-(mc&3))*2;
            int v  = (src[sb] >> ss) & 3;
            int dm = 11 - mc, db = row*3 + (dm>>2), ds = (3-(dm&3))*2;
            dst[db] = (unsigned char)(dst[db] | (v << ds));
        }
}
/* fragment: small RECTANGLE 4x3 pixels (user);
 * %10 pairs -> color = sprite color (piece_col) */
static const unsigned char piece_spr[64] = {
    0xA0,0,0, 0xA0,0,0, 0xA0,0,0, 0,0,0, 0,0,0,
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
    0,0,0, 0,0,0, 0,0,0, 0,0,0,0
};

/* mark cell for stepless RGB dimming in the overlay (a 0..256) */
static void tw_push(int cx, int cy, int a)
{
    if (g.tw_n >= 64) return;
    if (a > 256) a = 256;
    if (a < 0) a = 0;
    g.tw_cx[g.tw_n] = (unsigned char)cx;
    g.tw_cy[g.tw_n] = (unsigned char)cy;
    g.tw_a[g.tw_n]  = (unsigned short)a;
    ++g.tw_n;
}

static int flag_hitv = 1;   /* pair value of the last material hit */

/* set sprite i directly from a data buffer (multicolor) */
static void set_sprite_data(int i, const unsigned char *data, int x, int y, int col)
{
    memcpy(vic.spdata[i], data, 64);
    vic.spx[i]=(short)x; vic.spy[i]=(short)y; vic.spcol[i]=(unsigned char)col;
    vic.spenable |= (unsigned short)(1<<i);
    vic.spmulti  |= (unsigned short)(1<<i);
}

static void flag_init(void)
{
    int i;
    memcpy(g.flagdata[0], gd_sprites + BLK_FLAG_L*64, 64);   /* hull left (with red tank) */
    memcpy(g.flagdata[1], gd_sprites + BLK_FLAG_R*64, 64);   /* thin bow right */
    g.flagx = 60; g.flagy = FLAG_YMIN;
    g.flagdir = 1; g.flagdy = 2; g.flag_face = 1;   /* bow right */
    g.flaghp = 1;                          /* ROM: ONE %11 hit destroys */
    g.eleft = 0;
    g.piece_on = 0;
    g.flag_aggro = 0;
    g.fp_n = 0;
    g.ap_n = 0;
    memset(g.shield, 0xFF, sizeof(g.shield));   /* dotted protective arc intact */
    for (i = 0; i < 4; ++i) g.stone_on[i] = 0;
}

/* Big ship = 2 X-expanded halves (24px data each ->48px) = 96px.
 * When turning, the WHOLE ship is mirrored (bow in travel direction). */
static void flag_draw_ship(void)
{
    unsigned char tmp[64];
    if (g.flag_face) {                     /* bow right: canonical */
        set_sprite_data(2, g.flagdata[0], g.flagx,      g.flagy, 6);
        set_sprite_data(3, g.flagdata[1], g.flagx + 48, g.flagy, 6);
    } else {                               /* bow left: mirrored + swapped */
        spr_mirror(tmp, g.flagdata[1]); set_sprite_data(2, tmp, g.flagx,      g.flagy, 6);
        spr_mirror(tmp, g.flagdata[0]); set_sprite_data(3, tmp, g.flagx + 48, g.flagy, 6);
    }
    vic.spxexp |= (unsigned short)((1<<2)|(1<<3));
}

/* Shot hits ship half spr(2/3). ROM $8F7C-$8FFA byte-verified:
 * hit cell = SHOT TIP (pair = dx/4 due to X-expand, row = dy 1:1),
 * NO search - then clear 1 pair x 4 rows DOWNWARD ($98FA mask,
 * clip at sprite end). If the loop thereby hits a %11 pair in
 * rows 0-13 (window $02..$27; tank AND bow tip) -> instant destruction.
 * Empty cells are no-ops; the shot is always consumed (20 points).
 * Through fully eroded holes the shot passes through - the caller
 * handles that via vic_sprites_overlap (HW latch equivalent).
 * 2=destruction, 1=shell/idle. */
static int flag_hit_half(int spr)
{
    int spx  = (spr==2) ? g.flagx : g.flagx + 48;
    int half = g.flag_face ? (spr==2?0:1) : (spr==2?1:0);
    int mirror = g.flag_face ? 0 : 1;
    int pair = (g.shotx - spx) >> 2;
    int dy   = g.shoty - g.flagy;
    int canon, k, core = 0;
    if (pair < 0 || pair > 11 || dy < 0) return 1;   /* $3F clip: only 20 pt */
    canon = mirror ? (11 - pair) : pair;
    for (k = 0; k < 4 && dy + k <= 20; ++k) {
        int v = spr_mc(g.flagdata[half], canon * 2, dy + k);
        if (v == 3 && dy + k <= 13) core = 1;
        if (v) flag_hitv = v;
        spr_clr(g.flagdata[half], canon * 2, dy + k);
    }
    return core ? 2 : 1;
}

/* ship breakup: the remaining pixels of the ship turn into colored
 * particles (%01 yellow, %10 blue, %11 red) that rain down like the
 * stars in the finale (user request). */
static void flag_explode_parts(void)
{
    static const unsigned char pc[4] = { 0, 7, 6, 2 };
    int half, r, pair;
    int cx8 = g.flagx + 48 - 24, cy8 = g.flagy - 50 + 10;   /* center */
    g.fp_n = 0;
    for (half = 0; half < 2; ++half) {
        int base = g.flag_face ? (half ? g.flagx + 48 : g.flagx)
                               : (half ? g.flagx : g.flagx + 48);
        for (r = 0; r < 21; ++r)
            for (pair = 0; pair < 12; ++pair) {
                int v = spr_mc(g.flagdata[half], pair * 2, r);
                int sp = g.flag_face ? pair : (11 - pair);
                int px, py, vx, vy;
                if (!v || g.fp_n >= 160 || rnd(3) == 2) continue;
                px = base - 24 + sp * 4;
                py = g.flagy - 50 + r;
                /* fling outward radially (leisurely) + scatter */
                vx = (px > cx8 ? 1 : -1) * (1 + rnd(2)) + rnd(3) - 1;
                vy = (py > cy8 ? 1 : -1) * (1 + rnd(2)) + rnd(3) - 2;
                g.fp_x8[g.fp_n] = px;
                g.fp_y8[g.fp_n] = py;
                g.fp_vx[g.fp_n] = (signed char)vx;
                g.fp_vy[g.fp_n] = (signed char)vy;
                g.fp_c[g.fp_n]  = pc[v];
                ++g.fp_n;
            }
    }
}

static void flag_parts_update(void)
{
    /* ballistics: parts fly apart radially, gravity pulls them
     * down in arcs. With age the glyphs grow (asterisk ->
     * fat star -> block) = parts come CLOSER. */
    int i, age, gl;
    age = (g.flag_boomt > 0) ? 240 - g.flag_boomt
        : (g.flag_outro > 0) ? 240 + (422 - g.flag_outro) : 240;
    gl  = (age < 40) ? CH_STAR : (age < 90) ? 0x6A : 0x63;
    for (i = 0; i < g.fp_n; ++i) {
        int col = g.fp_x8[i] >> 3, row = g.fp_y8[i] >> 3, nc, nr;
        if (row > 23 || col < 0 || col > 39) continue;   /* out */
        g.fp_x8[i] += g.fp_vx[i];
        g.fp_y8[i] += g.fp_vy[i];
        if ((g.frame & 7) == 0 && g.fp_vy[i] < 3) ++g.fp_vy[i];
        nc = g.fp_x8[i] >> 3;
        nr = g.fp_y8[i] >> 3;
        if ((nc != col || nr != row) && row >= 2)
            mat_put(col, row, CH_EMPTY, 1, 1);
        if (nr >= 2 && nr <= 23 && nc >= 0 && nc <= 39)
            mat_put(nc, nr, gl, g.fp_c[i], 1);
    }
}

/* ship destruction ($9002-$9D21): explosion, extra life rank 0,
 * pixel rain, 1000 points */
static void flag_kill(void)
{
    g.flaghp = 0;
    boom_at(g.flagx + 48, g.flagy);
    vic.spxexp = (unsigned short)(vic.spxexp | (1 << SP_BOOM));
    vic.spyexp = (unsigned short)(vic.spyexp | (1 << SP_BOOM));
    g.flag_boomt = 240;                   /* long explosion (~5s, longplay) */
    sound_play(SND_BIGBOOM);
    if (g.rank == 0 && g.lives < 6) ++g.lives;   /* $9009 */
    g.flag_bx = (g.flagx + 48 - 24) / 8;
    g.flag_by = (g.flagy - 50) / 8 + 1;
    flag_explode_parts();                 /* breakup + pixel rain */
    sprite_off(2); sprite_off(3);
    add_score(7);                         /* 1000 ($9000) */
    --g.eleft;
}

/* TEMP debug (key 6): trigger flagship death sequence; a mission
 * intro still running is skipped in the process */
void game_debug_win(void)
{
    if (g.mission != 3 || g.flaghp <= 0 || g.flag_boomt > 0) return;
    if (g.state == ST_MISSION_INTRO) g.statetimer = 1;
    else if (g.state != ST_PLAY) return;
    flag_kill();
}

/* Huge death explosion: 8 rays grow radially out of the ship
 * (text matrix), plus a double-size explosion sprite. */
static void flag_bigboom(void)
{
    /* fullscreen flashes + shake; the rotating explosion rays
     * are drawn by the pixel-post-pass flagboom_fx (nothing stays) */
    static const unsigned char flashc[8] = { 3, 2, 1, 0, 15, 4, 13, 6 };
    if (g.flag_boomt <= 0) return;
    --g.flag_boomt;
    if (g.flag_boomt > 110)               /* phase 1: color screen flash */
        vic.bg = vic.border = flashc[(g.flag_boomt >> 1) & 7];
    else { vic.bg = 0; vic.border = 0; }
    vic_shake_x = rnd(7) - 3;             /* screenshake (user request) */
    vic_shake_y = rnd(5) - 2;
    if (g.flag_boomt == 0) {
        vic.bg = 0; vic.border = 0;
        vic_shake_x = vic_shake_y = 0;
        sprite_off(2); sprite_off(3);
        vic.spxexp = (unsigned short)(vic.spxexp & ~(1 << SP_BOOM));
        vic.spyexp = (unsigned short)(vic.spyexp & ~(1 << SP_BOOM));
        g.flag_outro = 422;               /* finale (length: youwon sample) */
    }
}

static void flag_update(void)
{
    static const int stone_sp[2] = { 5, 6 };
    int hit;

    flag_parts_update();                  /* ship pixel rain (also outro) */

    if (g.flag_outro > 0) {               /* finale: stars fall, typewriter
                                           * subtitles to speech sample, fade */
        static const char *wl1a = "YOU WON. FOR NOW.";
        static const char *wl1b = "YOU WON. AGAIN!";
        static const char *wl2 = "GORPHIANS CONQUER YOUR NEXT GALAXY!";
        int e = 422 - g.flag_outro;       /* elapsed frames */
        int v2 = (g.level >= 8);          /* MS:08+: "AGAIN!" variant */
        const char *wl1 = v2 ? wl1b : wl1a;
        int n1 = v2 ? 15 : 17, c1 = v2 ? 12 : 11, n2 = 35, i, shown;
        --g.flag_outro;
        /* matrix stars out (pixel starfield takes over); protective-arc
         * cells are collected into trickling pixel particles */
        if (e == 0) {
            int c2, r2;
            g.ap_n = 0;
            for (r2 = 2; r2 <= 23; ++r2)
                for (c2 = 0; c2 < VIC_COLS; ++c2) {
                    unsigned char cd = vic.screen[r2 * VIC_COLS + c2];
                    if (cd == CH_STAR)
                        mat_put(c2, r2, CH_EMPTY, 1, 1);
                    else if (cd >= 0x19 && cd <= 0x1D) {
                        if (g.ap_n < 48) {
                            g.ap_x[g.ap_n]  = c2 * 8 + 3;
                            g.ap_y8[g.ap_n] = r2 * 64 + 32;
                            g.ap_v[g.ap_n]  = (signed char)(3 + rnd(7));
                            ++g.ap_n;
                        }
                        mat_put(c2, r2, CH_EMPTY, 1, 1);
                    }
                }
        }
        {   /* shield debris trickles down (drawing: outro_arcfall);
             * plus a soft trickling sound while it falls */
            int i2;
            for (i2 = 0; i2 < g.ap_n; ++i2) g.ap_y8[i2] += g.ap_v[i2];
            sound_wind((e < 130 && g.ap_n > 0) ? 14 : 0);
        }
        /* watermark robot: alpha ramp in (while the voice is
         * speaking), hold, cleanly out after the text */
        g.robot_a = (e < 20) ? 0
                  : (e < 70)  ? (e - 20) * 30 / 50
                  : (e < 300) ? 30
                  : (e < 350) ? (350 - e) * 30 / 50 : 0;
        /* player ship flickers briefly and vanishes before the text
         * starts (otherwise it sat over line 19) */
        if (e >= 28 || (e >= 8 && ((e >> 2) & 1)))
            sprite_off(SP_PLAYER);
        if (e == 20) sound_play(v2 ? SND_WON2 : SND_WON);
        /* typewriter LIP-SYNCED: character schedule from the envelope
         * of the youwon sample (4 speech phrases, characters per phrase
         * spread linearly); fade in dark blue -> cyan -> white. */
        {
            /* GORPHIANS = 9 characters: same speech phrase, one character
             * more spread linearly in the same time window */
            static const unsigned short wct2[50] = {
                 24, 32, 40, 48, 56, 64, 72, 80, 88, 96,104,112,120,
                128,136,162,166,170,174,178,182,186,191,195,200,204,
                209,214,219,223,228,233,238,243,247,252,257,262,266,
                271,276,281,285,290,295,300,304,309,314,319 };
            static const unsigned short wct[52] = {
                 23, 31, 40, 48, 57, 65, 74, 82,104,104,110,117,123,
                130,136,143,149,176,180,183,187,190,194,198,202,205,
                209,214,218,222,226,231,235,239,243,248,252,256,260,
                270,270,273,276,280,283,287,290,293,297,300,304,307 };
            shown = 0;
            for (i = 0; i < n1 + n2; ++i) {
                int age;
                if (e < (int)(v2 ? wct2[i] : wct[i])) break;
                shown = i + 1;
                age = e - (int)(v2 ? wct2[i] : wct[i]);
                {
                    /* stepless: draw white, RGB alpha via overlay */
                    int a = age * 256 / 10;
                    if (i < n1) {
                        mat_put(c1 + i, 17, char_hires(wl1[i]), 1, 1);
                        tw_push(c1 + i, 17, a);
                    } else {
                        mat_put(2 + (i - n1), 19, char_hires(wl2[i - n1]), 1, 1);
                        tw_push(2 + (i - n1), 19, a);
                    }
                }
            }
        }
        if (e >= 30) {                    /* cursor pulses by RGB alpha */
            int p2 = e % 24, tri = (p2 < 12) ? p2 : 23 - p2;
            if (shown < n1) {
                mat_put(c1 + shown, 17, 0x60, 1, 1);
                tw_push(c1 + shown, 17, tri * 256 / 11);
            } else {
                mat_put(2 + (shown - n1), 19, 0x60, 1, 1);
                tw_push(2 + (shown - n1), 19, tri * 256 / 11);
            }
        }
        /* No fade out - the TV glitch IS the transition: the last 62
         * outro frames roll/glitch the old image (phase A), then
         * the new level slides in (phase B). Sample plays 2x. */
        if (g.flag_outro == 62) {
            g.glitcht = 124;
            sound_play(SND_GLITCH);
        }
        if (g.flag_outro <= 0) {
            g.state = ST_MISSION_CLEAR;
            g.statetimer = 1;             /* seamlessly into phase B */
        }
        return;
    }

    shield_flicker();
    shield_draw(1);                       /* dotted decorative arc */
    text_stars();                         /* white stars with sparkle */
    flag_bigboom();

    g.flagx += g.flagdir;                 /* ~50 px/s (from longplay) */
    if (g.flagx < 24 || g.flagx > 232) {
        g.flagdir = -g.flagdir;
        g.flag_face = !g.flag_face;
        g.flagy += g.flagdy;
        if (g.flagy < FLAG_YMIN || g.flagy > FLAG_YMAX) g.flagdy = -g.flagdy;
    }
    if (g.flaghp > 0) flag_draw_ship();

    /* $8EAA-$8EB8: rock rate grows over the mission course (moderate) */
    if (g.flaghp > 0 && (g.frame % 300) == 0 && g.flag_aggro < 5)
        ++g.flag_aggro;
    {
        int iv = 75 - g.rank * 8 - g.flag_aggro * 5;
        if (iv < 38) iv = 38;
        if (g.flaghp > 0 && (g.frame % iv) == 0)
            stone_spawn(g.flagx + 48, g.flagy + 24,
                        ((g.frame & wmis_rate[g.rank]) == wmis_rate[g.rank]),
                        2);               /* target $9267: rank 0 = 1/8 */
    }
    stones_update(stone_sp, 2);
    /* Rocks do NOT damage the shield arc - only the player shot does. */

    if (g.piece_on) {
        g.piece_x += g.piece_vx; g.piece_y += g.piece_vy;
        if (g.piece_y > 250 || g.piece_x < 8 || g.piece_x > 344) {
            g.piece_on = 0; sprite_off(SP_PIECE);
        } else set_sprite_data(SP_PIECE, piece_spr, g.piece_x, g.piece_y,
                               g.piece_col);
    }

    /* The player shot also knocks gaps into the shield arc -
     * an intact arc blocks the shot (shoot a hole first, then ship) */
    if (g.shot == 2 && vic_sprite_hits_code(SP_SHOT, 0x19, 0x1D)) {
        shield_hit_col((g.shotx - 24) >> 3);
        g.shot = -1; sprite_off(SP_SHOT);
        sound_play(SND_HIT);
    }

    /* Shot vs flying fragment (150, $94E8) and rocks (100, $9510) */
    if (g.shot == 2) {
        int s2, dxp, dyp;
        if (g.piece_on) {
            dxp = g.piece_x - g.shotx; if (dxp < 0) dxp = -dxp;
            dyp = g.piece_y - g.shoty; if (dyp < 0) dyp = -dyp;
            if (dxp < 8 && dyp < 10) {
                g.piece_on = 0; sprite_off(SP_PIECE);
                boom_at(g.piece_x, g.piece_y);
                add_score(3);              /* 150 */
                g.shot = -1; sprite_off(SP_SHOT);
            }
        }
        for (s2 = 0; s2 < 2 && g.shot == 2; ++s2) {
            if (!g.stone_on[s2]) continue;
            dxp = g.stone_x[s2] - g.shotx; if (dxp < 0) dxp = -dxp;
            dyp = g.stone_y[s2] - g.shoty; if (dyp < 0) dyp = -dyp;
            if (dxp < 12 && dyp < 12) {
                g.stone_on[s2] = 0; sprite_off(stone_sp[s2]);
                boom_at(g.stone_x[s2], g.stone_y[s2]);
                add_score(2);              /* 100 */
                g.shot = -1; sprite_off(SP_SHOT);
            }
        }
    }

    /* Shot: strip the shell until the red core (tank) is exposed. */
    if (g.flaghp > 0 && g.shot == 2) {
        hit = 0;
        /* HW-true collision: %01 shell is transparent - the shot
         * penetrates to %10/%11 material (thick bites like original) */
        if (vic_sprites_overlap_hw(SP_SHOT, 2))      hit = flag_hit_half(2);
        else if (vic_sprites_overlap_hw(SP_SHOT, 3)) hit = flag_hit_half(3);
        if (hit == 2) {                    /* %11 hit -> instant destruction */
            g.shot = -1; sprite_off(SP_SHOT);
            flag_kill();
        } else if (hit == 1) {             /* shell stripped -> shards */
            g.shot = -1; sprite_off(SP_SHOT);
            if (!g.piece_on) {
                g.piece_on = 1;
                g.piece_x = g.shotx; g.piece_y = g.shoty;
                g.piece_vx = (signed char)(rnd(3) - 1);   /* slow */
                g.piece_vy = 1;
                /* Color of the material shot away: %01 yellow, %10 blue,
                 * %11 red */
                g.piece_col = (flag_hitv == 1) ? 7 : (flag_hitv == 2) ? 6 : 2;
            }
            add_score(0);                  /* 20 */
        }
        /* hit==0: hole -> shot flies on */
    }
    if (g.eleft < 0 && g.flag_boomt <= 0 && g.flag_outro <= 0)
        { g.state = ST_MISSION_CLEAR; g.statetimer = 120; }
}

/* ===================================================================== */
/*  Player and shot ($838C / $8423 / $8486)                              */
/* ===================================================================== */

static const int ymin_tab[4] = { 0xCB, 0x78, 0xA8, 0xA8 };

static void player_update(void)
{
    if (g.pdead) { sprite_off(SP_PLAYER); return; }

    if (g.mission == 1) {
        /* Blitter ship: 1 MC pixel X, 2/-1 pixel Y ($83F8).
         * Only on movement: erase old position, move, redraw -
         * otherwise the ship stays put (no smearing). */
        int nx = g.bx, ny = g.by;
        if (g.in_left)  --nx;
        if (g.in_right) ++nx;
        if (g.in_up)    --ny;
        if (g.in_down)  ny += 2;
        if (nx < 4 || nx >= 0x94 - 16) nx = g.bx;
        if (ny < 0x78 || ny >= 0xA9)   ny = g.by;
        if (nx != g.bx || ny != g.by) {
            laser_draw_player(1);        /* erase old position */
            g.bx = nx; g.by = ny;
            laser_draw_player(0);        /* draw new position */
        }
    } else {
        if (g.in_left)  g.px -= 2;
        if (g.in_right) g.px += 2;
        if (g.in_up)    g.py -= 1;
        if (g.in_down)  g.py += 1;
        if (g.px < 0x1F)  g.px = 0x1F;
        if (g.px > 0x13A) g.px = 0x13A;
        if (g.py < ymin_tab[g.mission]) g.py = ymin_tab[g.mission];
        if (g.py > 0xDF) g.py = 0xDF;
        set_sprite(SP_PLAYER, BLK_PLAYER, g.px, g.py,
                   mis_spcol[g.mission][SP_PLAYER], 1);  /* $826A: red in M1, else per mission */
        vic.spyexp = (unsigned short)(vic.spyexp | 1);   /* $D017 bit 0 */
    }
}

static void shot_update(void)
{
    /* Trigger ($8423): only on a fresh press; resets an existing one */
    if (g.fire_edge && !g.pdead && g.state == ST_PLAY) {
        if (g.mission == 1) {
            if (g.shot == 1) shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 1);
            g.shotx = g.bx + 5;
            g.shoty = g.by - 8;
            g.shot = 1;
            shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 0);
        } else {
            g.shotx = g.px + 12;
            g.shoty = g.py - 7;
            g.shot = 2;
        }
        sound_play(SND_SHOOT);
    }

    if (g.shot == 2) {
        g.shoty -= 3;                     /* $9AEB: 3 px/frame */
        if (g.shoty < 0x1D) { g.shot = -1; sprite_off(SP_SHOT); }
        else set_sprite(SP_SHOT, BLK_SHOT, g.shotx, g.shoty, 1, 0);
    } else if (g.shot == 1) {
        if (g.frame & 1) return;          /* only every 2nd frame */
        shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 1);
        g.shoty -= 8;
        if (g.shoty < 8) g.shot = -1;
        else shape_blit(0x1E, g.shotx, g.shoty, 8, 1, 0);
    }
}

/* ===================================================================== */
/*  Frame                                                                */
/* ===================================================================== */

void game_start_mission(void)
{
    int c;
    g.pop_t[0] = g.pop_t[1] = 0;
    vic_shake_x = vic_shake_y = 0;        /* if title screen ends mid-shake */
    sound_wind(0);                        /* title wind off */
    vic.mode   = mis_mode[g.mission];
    vic.border = mis_border[g.mission];
    vic.bg     = mis_bg[g.mission];
    vic.bg1    = mis_bg1[g.mission];
    vic.bg2    = mis_bg2[g.mission];
    vic.spmc0  = 7;
    vic.spmc1  = 2;
    for (c = 0; c < NUM_SPRITES; ++c) {
        /* Mission color tables cover the 8 original sprites;
         * the extra ones (8-15) start white */
        vic.spcol[c] = (c < 8) ? mis_spcol[g.mission][c] : 1;
        sprite_off(c);
    }
    vic.spxexp = vic.spyexp = 0;

    if (vic.mode == VIC_MODE_TEXT) {
        memset(vic.screen, CH_EMPTY, VIC_SCREEN_SZ);
        memset(vic.color,  1, VIC_SCREEN_SZ);
        memset(vic.color,        0x0F, VIC_COLS);         /* header  */
        memset(vic.color + 960,  0x0F, VIC_COLS);         /* footer  */
    }

    g.px = 0xAA; g.py = 0xDA;             /* $9FA9/$9FAE */
    g.pdead = 0;
    g.shot = -1;
    g.boomtimer = 0;
    g.fade = 256;                         /* full brightness */
    g.flag_outro = 0;
    ++g.level;

    switch (g.mission) {
    case 0: astro_init(); break;
    case 1: laser_init(); break;
    case 2: warp_init(); break;
    default: flag_init(); break;
    }

    g.state = ST_MISSION_INTRO;
    g.statetimer = 120;
    sound_play(SND_MISSION);
}

void game_init(void)
{
    long hs = g.hiscore;                  /* hiscore survives the reset */
    memset(&g, 0, sizeof(g));
    g.hiscore = hs;
    sound_wind(0);                        /* key 5 mid-orbit */
    vic_reset();
    shape_init();
    memcpy(vic.charset, gd_font, sizeof(vic.charset));
    g.lives = 5;                          /* $C0 ($8145) */
    g.shot  = -1;
    g.fade  = 256;
    g.state = ST_TITLE;
    vic.mode = VIC_MODE_TEXT;
    vic.bg = 0; vic.border = 0;
    vic.bg1 = 7; vic.bg2 = 7;   /* title: MC status rows + stars YELLOW */
    memset(vic.screen, CH_EMPTY, VIC_SCREEN_SZ);
    memset(vic.color, 1, VIC_SCREEN_SZ);
}

/* Return from attract mode: title starts BEHIND all one-shot
 * events (typewriter done, no explosion, no speech) -
 * text is up, robot circles, starfield keeps turning */
void game_title_return(void)
{
    game_init();
    g.statetimer = 750;
}

/* Attract mode: starts after title inactivity; a simple autopilot
 * plays without scoring, any key returns (main.c) */
static void demo_start(void)
{
    long hs = g.hiscore;
    memset(&g, 0, sizeof(g));
    g.hiscore = hs;
    g.lives = 5;
    g.shot = -1;
    g.demo = 1;
    /* 50/50 Astro/Warp: strictly alternating (rnd in the attract
     * loop would be deterministic and always stuck on Astro) */
    {
        static int demo_alt;
        demo_alt ^= 1;
        if (demo_alt) g.mission = 2;
    }
    game_start_mission();
}

static void demo_ai(void)
{
    int i, best = 999, d;
    g.in_left = g.in_right = g.in_up = g.in_down = 0;
    g.in_fire = 0; g.fire_edge = 0;
    if (g.state != ST_PLAY || g.pdead) return;
    if (g.mission == 0) {
        /* HOLD the target until it is dead (else flipflop between
         * equally near aliens = jitter); aim at the cell center
         * of the pair with lead in march direction (formation moves
         * while the shot flies) - hit window is ex/ex+1, 8px each */
        static int tgt = -1;
        for (i = 0; i < 2; ++i)               /* bomb above us: dodge */
            if (g.bomb_on[i] && g.bomby[i] > 150) {
                d = g.bombx[i] - g.px;
                if (d > -20 && d < 20) {
                    if (d >= 0) g.in_left = 1; else g.in_right = 1;
                    return;
                }
            }
        if (tgt >= 0 && (g.etype[tgt] == 0xFF || !mat_placed(tgt)))
            tgt = -1;
        if (tgt < 0)
            for (i = 0; i < NUM_ENEMIES; ++i) {
                if (g.etype[i] == 0xFF || !mat_placed(i)) continue;
                d = (24 + g.ex[i] * 8 + 8) - (g.px + 12);
                if (d < 0) d = -d;
                if (d < best) { best = d; tgt = i; }
            }
        if (tgt >= 0) {
            /* Where does the target march to until the shot arrives up top?
             * flight time (3px/frame) -> march steps (1 cell per
             * stepdelay, next in steptimer) -> future column,
             * reflected at the turn edges (1/0x25) */
            int fut = g.ex[tgt], sx = g.px + 12;
            if (g.matdone) {
                int fly = (g.py - 7 - (50 + (g.ey[tgt] + 1) * 8)) / 3;
                int steps = 0;
                if (fly >= g.steptimer && g.stepdelay > 0)
                    steps = 1 + (fly - g.steptimer) / g.stepdelay;
                fut += g.xdir * steps;
                if (fut < 1)  fut = 2 - fut;
                if (fut > 37) fut = 74 - fut;
                if (fut < 1)  fut = 1;
                if (fut > 37) fut = 37;
            }
            /* hit window = cells fut/fut+1 (16px); fire only with
             * 2px safety margin INSIDE - never in the gap again */
            d = (24 + fut * 8 + 8) - sx;
            if (d < -3)     g.in_left = 1;
            else if (d > 3) g.in_right = 1;
            if (g.shot < 0 &&
                sx >= 24 + fut * 8 + 2 && sx <= 24 + fut * 8 + 13)
                { g.in_fire = 1; g.fire_edge = 1; }
        }
    } else if (g.mission == 2) {              /* Space Warp: ONLY dodge
                                               * and shoot the object */
        /* Dodge in a FIXED direction until the danger has passed
         * (else flipflop in/out = odd moves) */
        static int dodge;
        int threat = 0;
        for (i = 0; i < 4; ++i)
            if (g.stone_on[i]) {
                int dy = g.py - g.stone_y[i];
                d = g.stone_x[i] - g.px;
                if (d > -30 && d < 30 && dy > -20 && dy < 90) {
                    threat = 1;
                    if (!dodge) dodge = (d >= 0) ? -1 : 1;
                }
            }
        if (g.wobj_on) {
            int dyw = g.py - g.wobj_y;
            d = g.wobj_x - g.px;
            if (dyw < 48 && d > -40 && d < 40) {   /* object coming down */
                threat = 1;
                if (!dodge) dodge = (d >= 0) ? -1 : 1;
            }
        }
        if (threat) {
            if (g.px <= 0x24 && dodge < 0) dodge = 1;    /* edge */
            if (g.px >= 0x135 && dodge > 0) dodge = -1;
            if (dodge < 0) g.in_left = 1; else g.in_right = 1;
        } else {
            dodge = 0;
            if (g.wobj_on) {
                int dir = 0;
                d = g.wobj_x - g.px;
                if (d < -10) dir = -1; else if (d > 10) dir = 1;
                /* never run into an incoming rock */
                for (i = 0; dir && i < 4; ++i)
                    if (g.stone_on[i]) {
                        int sdx = g.stone_x[i] - g.px;
                        int dy  = g.py - g.stone_y[i];
                        int adx = sdx < 0 ? -sdx : sdx;
                        if (dy > -20 && dy < 110 &&
                            sdx * dir > 0 && adx < 56) dir = 0;
                    }
                if (dir < 0)      g.in_left = 1;
                else if (dir > 0) g.in_right = 1;
            }
        }
        if (g.shot < 0 && g.wobj_on) {            /* fire independent
                                                   * of the movement */
            d = g.wobj_x - g.px;
            if (d > -14 && d < 14) { g.in_fire = 1; g.fire_edge = 1; }
        }
    } else {                                  /* other missions: oscillate */
        if ((g.frame >> 5) & 1) g.in_right = 1; else g.in_left = 1;
        if (g.shot < 0) { g.in_fire = 1; g.fire_edge = 1; }
    }
}

void game_frame(void)
{
    ++g.frame;
    if (g.demo) {
        demo_ai();
        if (++g.demot > 1700) { game_title_return(); return; }
    }
    g.tw_n = 0;                           /* typewriter alpha per frame */
    if (g.state != ST_BOOT && g.bootzoom > 0) {   /* boot crossfade rest */
        ++g.bootzoom;
        if (g.bootzoom > 190) g.bootzoom = 0;
    }
    if (g.glitcht > 0) --g.glitcht;

    switch (g.state) {
    case ST_CREDITS:                      /* credits_render draws it (main.c) */
        ++g.statetimer;
        if (g.statetimer == 60) sound_play(SND_CREDBOOM);   /* logo forms */
        return;
    case ST_BOOT:
        ++g.statetimer;
        if (g.bootzoom == 0) {
            /* keyboard clack per typed character (18 keystrokes) */
            if (g.statetimer >= 50 && g.statetimer < 50 + 18 * 4 &&
                ((g.statetimer - 50) & 3) == 0)
                sound_play(SND_KEY);
            g.fade = (g.statetimer * 6 > 256) ? 256 : g.statetimer * 6;
            if (g.fire_edge && g.statetimer > 30) {
                g.bootzoom = 1;
                sound_play(SND_TAKEOVER);   /* quiet transition sound */
            }
        } else {
            ++g.bootzoom;
            g.fade = 256;                 /* crossfade handles brightness */
            if (g.bootzoom >= 20) {       /* switch to title early -
                                           * boot fades out as overlay */
                g.state = ST_TITLE;
                g.statetimer = 0;
                g.fade = 0;
            }
        }
        return;
    case ST_TITLE:
        ++g.statetimer;
        if (g.statetimer >= 1500) { demo_start(); return; }
        if (g.fire_edge) {
            long hs = g.hiscore;
            memset(&g, 0, sizeof(g));
            g.hiscore = hs;
            g.lives = 5;
            g.shot = -1;
            game_start_mission();
        }
        return;
    case ST_MISSION_INTRO:
        if (--g.statetimer <= 0) {
            g.state = ST_PLAY;
            /* Remove mission name again */
            if (vic.mode == VIC_MODE_TEXT) {
                int c;
                for (c = 0; c < VIC_COLS; ++c)
                    mat_put(c, 12, CH_EMPTY, 1, 1);
            } else {
                int c, y;
                for (c = 0; c < VIC_COLS; ++c)
                    for (y = 0; y < 8; ++y)
                        vic.bitmap[(12 * VIC_COLS + c) * 8 + y] = 0;
            }
        }
        return;
    case ST_PLAYER_HIT:
        boom_update();
        if (g.mission == 1) laser_booms();
        if (g.mission == 3) flag_bigboom();   /* Explosion keeps running */
        if (--g.statetimer <= 0) {
            /* Demo loses no lives (endless until timeout) */
            if (!g.demo && --g.lives <= 0)
                { g.state = ST_GAME_OVER; g.statetimer = 250; }
            else {
                g.pdead = 0;
                g.px = 0xAA; g.py = 0xDA;
                g.bx = 72;   g.by = 168;
                if (g.mission == 1) {
                    bitmap_clear();
                    stars_bitmap_all();
                    g.laser_on[0] = g.laser_on[1] = 0;
                    laser_draw_static();     /* Redraw cannons + ship */
                }
                g.state = ST_PLAY;
            }
        }
        return;
    case ST_MISSION_CLEAR:
        boom_update();
        if (g.mission == 1) laser_booms();
        if (--g.statetimer <= 0) {
            int wasflag = (g.mission == 3);
            ++g.mission;
            if (g.mission >= NUM_MISSIONS) {
                g.mission = 0;
                if (g.rank < MAX_RANK) ++g.rank;
                if (g.rank == 1 && g.lives < 6) ++g.lives;   /* Extra life $9009 */
            }
            game_start_mission();
            /* Fallback (debug key N etc.): glitch had not started yet */
            if (wasflag && g.glitcht <= 0) {
                g.glitcht = 62;
                sound_play(SND_GLITCH);
            }
        }
        return;
    case ST_GAME_OVER:
        if (--g.statetimer <= 0) game_title_return();
        return;
    default:
        break;
    }

    if (g.mission == 0) astro_collide();

    player_update();
    shot_update();
    boom_update();

    switch (g.mission) {
    case 0: astro_update(); break;
    case 1: laser_update(); break;
    case 2: warp_update(); break;
    default: flag_update(); break;
    }
}

void game_draw(void)
{
    if (g.state == ST_BOOT || g.state == ST_CREDITS) {
        vic.mode = VIC_MODE_TEXT;
        vic.bg = 0; vic.border = 0;
        memset(vic.screen, CH_EMPTY, VIC_SCREEN_SZ);
        memset(vic.color, 1, VIC_SCREEN_SZ);
        vic.spenable = 0;
        return;                           /* boot/credits paints it (main.c) */
    }
    if (g.state == ST_TITLE) {
        int i;
        vic.mode = VIC_MODE_TEXT;
        vic.bg = 0; vic.border = 0;
        memset(vic.screen, CH_EMPTY, VIC_SCREEN_SZ);
        memset(vic.color, 1, VIC_SCREEN_SZ);
        status_lines();                   /* Title = MS:00 (as original) */
        {
            /* First ~4s slow fade in (only stars + status), THEN
             * the intro starts - everything shifted by 200 frames */
            int st = g.statetimer - 200;
            int t = st - 180;             /* Logo animation starts after intro */
            vic.spenable = 0;
            /* Stars come as a rotating pixel starfield (post-pass
             * title_starfield), no longer from the matrix */
            /* quadratic curve: stays dark long, picks up at the end */
            g.fade = g.statetimer * g.statetimer / 156;
            if (g.fade > 256) g.fade = 256;
            /* Intro typewriter (user request): presentation line ABOVE
             * the logo, types with quiet ticks and STAYS put */
            {
                /* Character schedule LIP-SYNCED to presents sample
                 * (envelope: word bounds frame 37/78, end 122) */
                static const char *pl = "SYNTHETIC DEVELOPMENT PRESENTS";
                static const unsigned short pct[30] = {
                     28, 31, 34, 38, 41, 44, 48, 51, 54, 62,
                     62, 65, 68, 72, 75, 78, 82, 85, 88, 92,
                     95,103,103,108,114,119,125,130,136,141 };
                int shown = 0;
                if (st == 25) sound_play(SND_PRESENTS);   /* speech clip */
                for (i = 0; i < 30; ++i) {
                    int age, col;
                    if (st < (int)pct[i]) break;
                    shown = i + 1;
                    age = st - (int)pct[i];
                    /* stepless: draw white, RGB alpha via overlay */
                    col = 1;
                    mat_put(5 + i, 5, char_hires(pl[i]), col, 1);
                    tw_push(5 + i, 5, age * 256 / 10);
                }
                if (st >= 25 && st < 175) {   /* Cursor pulses by RGB-alpha */
                    int p2 = st % 24, tri = (p2 < 12) ? p2 : 23 - p2;
                    mat_put(5 + shown, 5, 0x60, 1, 1);
                    tw_push(5 + shown, 5, tri * 256 / 11);
                }
            }
            /* Start animation like original (longplay t005-t081, no (C)
             * lines): letters one by one, robot descends from top left
             * and then bobs next to the logo. */
            if (t >= 0) {
                /* PUSH THE SPACE BUTTON: typewriter with key clack,
                 * starts only AFTER the GORPH voice (t=210) */
                static const char *pb = "PUSH THE SPACE BUTTON";
                int shown2 = (t < 210) ? 0 : (t - 210) / 4;
                if (shown2 > 21) shown2 = 21;
                if (t >= 210 && t < 210 + 21 * 4 && ((t - 210) & 3) == 0)
                    sound_play(SND_KEY);
                for (i = 0; i < shown2; ++i) {
                    int age = t - 210 - i * 4;
                    mat_put(9 + i, 17, char_hires(pb[i]), 1, 1);
                    tw_push(9 + i, 17, age * 256 / 10);
                }
                if (t >= 210 && t < 210 + 21 * 4 + 72) {
                    int p2 = t % 24, tri = (p2 < 12) ? p2 : 23 - p2;
                    int ca = (shown2 < 21) ? 256 : tri * 256 / 11;
                    mat_put(9 + shown2, 17, 0x60, 1, 1);
                    tw_push(9 + shown2, 17, ca);
                }
                /* subtle hint bottom right: C = CREDITS (fades in after
                 * the PUSH text, stays dimmed) */
                if (t >= 300) {
                    static const char *pc2 = "C = CREDITS";
                    int p = (t - 300) % 128;            /* pulse 2.5s */
                    int a = 12 + ((p < 64) ? p : 127 - p) * 2;   /* 12..138 */
                    if (a > (t - 300) * 8) a = (t - 300) * 8;   /* fade in */
                    for (i = 0; pc2[i]; ++i) {   /* bottom center, row 22 */
                        mat_put(14 + i, 22, char_hires(pc2[i]), 1, 1);
                        tw_push(14 + i, 22, a);
                    }
                }
                /* Letters fly in as a WAVE from the right (w003-w053):
                 * train moves left 3px/frame, swings vertically
                 * (sine +-10px) and locks into the target position. */
                for (i = 0; i < 5; ++i) {
                    static const signed char twave[16] =
                        { 0,4,7,9,10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4 };
                    /* GORPH: G O R from original, P/H new (34/35) */
                    static const int lblk[5] = { 27, 28, 29, 34, 35 };
                    int fx = 68 + i * 48;
                    int x  = 344 + i * 56 - t * 3;
                    int y  = 120;
                    if (x <= fx) x = fx;
                    else y = 120 + twave[(t + i * 4) & 15];
                    /* Letters on sprite 1-5: sprite 0 stays free for
                     * the robot IN FRONT of logo (VIC priority) */
                    set_sprite(1 + i, lblk[i], x, y, 8, 1);
                    /* At the GORPH voice (t=151): letters flash one
                     * after another COMPLETELY white and fade via light
                     * gray/gray back to the base color */
                    {
                        /* Envelope GORPH.mp3: onset frame 2, core up to
                         * ~45 -> 5 flashes spaced 8 from t=153 */
                        int bt = t - (153 + i * 8);
                        if (bt >= 0 && bt < 10) {
                            sprite_solidify(1 + i);
                            vic.spcol[1 + i] = (bt < 4) ? 1
                                             : (bt < 7) ? 15 : 12;
                        }
                    }
                }
                vic.spxexp = 0x3E;        /* Letters double width */
                vic.spyexp = 0x3E;        /* Letters double height */
                /* Logo locked in (t=103): THUD - deep double rumble,
                 * decaying screenshake (~0.8s), double white flash */
                if (t == 103) sound_play(SND_BIGBOOM);
                if (t == 151) sound_play(SND_GORPHVOICE);   /* "GORPH" after
                                                            * the shake */
                if (t >= 103 && t < 151) {
                    int amp = (t < 115) ? 11 : (t < 127) ? 7
                            : (t < 139) ? 4 : 2;
                    vic_shake_x = rnd(amp * 2 + 1) - amp;
                    vic_shake_y = rnd(amp * 2 + 1) - amp;
                } else {
                    vic_shake_x = vic_shake_y = 0;
                }
                if ((t >= 103 && t < 106) || (t >= 108 && t < 110))
                    vic.bg = vic.border = 1;   /* whiteflash */
                /* Robot: fly in from above, then CIRCLING the logo -
                 * back (top) small and hidden by the letters,
                 * front (bottom) double size (user request) */
                {
                    int rx, ry;
                    if (t < 70) {             /* Fly-in from top (large) */
                        rx = 42; ry = 50 + t; if (ry > 110) ry = 110;
                        vic.spxexp |= 0x01; vic.spyexp |= 0x01;
                        set_sprite(0, BLK_GORPH, rx, ry, 10, 1);
                        sound_wind(0);
                    } else {                  /* Logo orbit: interpolated
                                               * 1/4-step path (judder-free),
                                               * size smoothly scaled 24-48px;
                                               * front sprite 0, back sprite 7,
                                               * blur trail dithered 8+9
                                               * (no more 8-sprite limit). */
                        int t70 = t - 70, k;
                        vic.spxexp |= 0x0381; vic.spyexp |= 0x0381;
                        for (k = 2; k >= 0; --k) {
                            int tv = t70 - k * 2, q, s, c, w_p, hs, spr;
                            if (tv < 0) tv = 0;
                            q  = 48 + 96000 - (tv + tsin_q(tv / 2) / 12);
                            s  = tsin_q(q);
                            c  = tsin_q(q + 24);
                            rx = 188 + c * 120 / 64
                                     + tsin_q(tv / 2 + 32) * 3 / 64;
                            ry = 131 + s * 12 / 64
                                     + tsin_q(tv / 2) * 3 / 64;
                            w_p = 6 + (s + 64) * 6 / 128;
                            hs  = 10 + (s + 64) * 11 / 128;
                            spr = k ? 7 + k : (s > 0 ? 0 : 7);
                            set_sprite(spr, BLK_GORPH,
                                       rx - w_p * 2, ry - hs, k ? 9 : 10, 1);
                            robot_scaled(spr, w_p, hs);
                            if (k) sprite_dither(spr, k - 1);
                            /* soft wind, more present up front */
                            else sound_wind(28 + s * 12 / 64);
                        }
                    }
                }
            }
        }
        return;
    }

    status_lines();

    if (g.state == ST_MISSION_INTRO) {
        const char *mn = mission_name[g.mission];
        if (vic.mode == VIC_MODE_TEXT)
            mat_text((40 - (int)strlen(mn)) / 2, 12, mn, 1);
        else
            bm_text((40 - (int)strlen(mn)) / 2, 12, mn);
    }
    if (g.state == ST_GAME_OVER) {
        if (vic.mode == VIC_MODE_TEXT)
            mat_text(15, 12, "GAME OVER", 1);
        else
            bm_text(15, 12, "GAME OVER");
    }
}

/* Title orbit: sine with 24 nodes, linearly interpolated (q = phase in
 * quarter steps) - for judder-free path and smooth size steps */
static const signed char tsin[24] = {
      0, 16, 31, 45, 55, 62, 64, 62, 55, 45, 31, 16,
      0,-16,-31,-45,-55,-62,-64,-62,-55,-45,-31,-16 };

static int tsin_q(int q)
{
    int i = (q >> 2) % 24, f = q & 3;
    int a = tsin[i], b = tsin[(i + 1) % 24];
    return a + (b - a) * f / 4;
}

/* Draw robot block smoothly scaled into the sprite data: w_p MC pairs
 * wide (6..12), hs rows high (10..21); sprite is X+Y-expanded ->
 * display 24..48 x 20..42 px in fine steps */
static void robot_scaled(int spr, int w_p, int hs)
{
    const unsigned char *src = gd_sprites + BLK_GORPH * 64;
    unsigned char d[64];
    int x, y;
    memset(d, 0, 64);
    for (y = 0; y < hs; ++y)
        for (x = 0; x < w_p; ++x) {
            int sx = x * 12 / w_p, sy = y * 21 / hs;
            int v = (src[sy * 3 + (sx >> 2)] >> ((3 - (sx & 3)) * 2)) & 3;
            if (v) d[y * 3 + (x >> 2)] =
                (unsigned char)(d[y * 3 + (x >> 2)] | (v << ((3 - (x & 3)) * 2)));
        }
    memcpy(vic.spdata[spr], d, 64);
}

/* Fine sine for the starfield rotation: lap = 1536 units
 * (1/16 quarter step) - so edge motion is judder-free */
static int tsin_f(int w)
{
    int i, f, a, b;
    w %= 1536; if (w < 0) w += 1536;
    i = w / 64; f = w % 64;
    a = tsin[i]; b = tsin[(i + 1) % 24];
    return a + (b - a) * f / 64;
}


/* Title starfield: rotates slowly about the left axis (0,100), pixel-
 * exact as post-pass into final image (only background pixels ->
 * stays behind logo/text); dark trailing pixel = slight blur */
static void title_starfield(unsigned char *fr)
{
    int i, k, w0 = -(g.statetimer * 2) % 1536;   /* negative = leftwards */
    for (i = 0; i < 40; ++i) {
        int ph = (i * 197) % 1536;
        int r  = 20 + (i * i * 13) % 170;        /* about MID-SCREEN */
        for (k = 1; k >= 0; --k) {        /* k=1: blur (3 frames old) */
            int w  = w0 + ph + k * 6;
            int sx = 160 + r * tsin_f(w + 384) / 64;
            int sy = 100 + r * tsin_f(w) / 64;
            unsigned char col = k ? 11 : ((i % 3) ? 1 : 7);
            if (sx < 0 || sx >= VIC_W - 1 || sy < 0 || sy >= VIC_H) continue;
            if (fr[sy * VIC_W + sx] == 0) fr[sy * VIC_W + sx] = col;
            if (!k && (i % 4) == 0 && fr[sy * VIC_W + sx + 1] == 0)
                fr[sy * VIC_W + sx + 1] = col;
        }
    }
}

/* Position of title star k (0..39) - for the seamless morph into
 * the credits starfield (main.c takes over the stars on C press) */
void game_title_star(int k, int *sx, int *sy)
{
    int w0 = -(g.statetimer * 2) % 1536;
    int ph = (k * 197) % 1536;
    int r  = 20 + (k * k * 13) % 170;
    int w  = w0 + ph;
    *sx = 160 + r * tsin_f(w + 384) / 64;
    *sy = 100 + r * tsin_f(w) / 64;
}

/* Rotating explosion rays (flagship death): 16 spark rays
 * spin as a firewheel around the ship center, grow and are
 * redrawn fresh every frame - nothing stays put */
static void flagboom_fx(unsigned char *fr)
{
    static const unsigned char bc[4] = { 7, 2, 10, 1 };
    int d, s, e = 240 - g.flag_boomt;
    int cx = g.flag_bx * 8 + 4, cy = g.flag_by * 8 + 4;
    int len = e * 3;
    /* Rotation accelerates at the finale - the wheel SPINS out */
    int rot = e * 6 + ((e > 180) ? (e - 180) * (e - 180) / 6 : 0);
    int r0  = (e > 180) ? (e - 180) * 8 : 0;
    if (len > 260) len = 260;
    for (d = 0; d < 16; ++d) {
        for (s = 8 + r0; s < len + r0; s += 5 + (d & 3)) {
            /* Angle lags with the radius -> spiral arms */
            int w = rot + d * 96 - s / 2;
            int x = cx + tsin_f(w + 384) * s / 64;
            int y = cy + tsin_f(w) * s / 64;
            if (x < 0 || x >= VIC_W || y < 8 || y >= 192) continue;
            fr[y * VIC_W + x] = bc[(d + (s >> 4)) & 3];
        }
    }
    /* Spark burst: from the bang single pixels fly radially at
     * different speeds outward (two waves) */
    for (d = 0; d < 96; ++d) {
        int e0 = (d < 48) ? 0 : 40;
        int w0, v, r, x, y;
        if (e <= e0) continue;
        w0 = (d * 197 + d * d * 31) % 1536;
        v  = 2 + (d * 5) % 5;
        r  = 8 + (e - e0) * v;
        x = cx + tsin_f(w0 + 384) * r / 64;
        y = cy + tsin_f(w0) * r / 64;
        if (x >= 0 && x < VIC_W && y >= 8 && y < 192)
            fr[y * VIC_W + x] = bc[d & 3];
    }
}

/* Shield debris: the arc cells trickle down as red pixels,
 * with motion-blur trail, then fade out */
static void outro_arcfall(unsigned char *fr)
{
    int i, e = 422 - g.flag_outro;
    unsigned char col = (e < 90) ? 2 : (e < 130) ? 11 : 0;
    if (!col || g.ap_n <= 0) return;
    for (i = 0; i < g.ap_n; ++i) {
        int x = g.ap_x[i], y = g.ap_y8[i] >> 3, yb = y - 5;
        if (y >= 8 && y < 192) {
            fr[y * VIC_W + x] = col;
            if (x + 1 < VIC_W) fr[y * VIC_W + x + 1] = col;
        }
        if (e < 90 && yb >= 8 && yb < 192 && fr[yb * VIC_W + x] == 0)
            fr[yb * VIC_W + x] = 11;      /* blur trail */
    }
}

/* Finale starfield (outro mission 4): the explosion puts the
 * starfield into a short, decaying rotation; the stars also
 * sink away downward individually */
static void outro_starfield(unsigned char *fr)
{
    int i, e = 422 - g.flag_outro;
    long rot = (e < 150) ? 3L * e - (long)e * e / 100 : 225L;
    for (i = 0; i < 40; ++i) {
        int ph = (i * 197) % 1536;
        int r  = 20 + (i * i * 13) % 170;
        int w  = ph - (int)(rot % 1536);
        int fall = e * (2 + (i % 3)) / 6;
        int sx = 160 + r * tsin_f(w + 384) / 64;
        int sy = 100 + r * tsin_f(w) / 64 + fall;
        unsigned char col = (i % 3) ? 1 : 15;
        if (sx < 0 || sx >= VIC_W || sy < 0 || sy >= VIC_H) continue;
        if (fr[sy * VIC_W + sx] == 0) fr[sy * VIC_W + sx] = col;
    }
}

/* TV glitch transition after mission 4 (user request, reference: dark
 * VHS static video): the new level pushes into the frame
 * from below (done after 40 frames), plus line tears, noise bars and
 * speckle noise that decays with the glitch sample (~1.2s). Acts on
 * the fully rendered index image, after vic_render. */
void game_glitch(unsigned char *fr)
{
    int e, off, i, n;
    if (g.state == ST_TITLE) title_starfield(fr);   /* Title post-pass */
    else if (g.mission == 3 && g.flag_boomt > 0)
        flagboom_fx(fr);                            /* Explosion firewheel */
    else if (g.state == ST_PLAY && g.mission == 3 && g.flag_outro > 0) {
        outro_starfield(fr);                        /* Finale post-pass */
        outro_arcfall(fr);
    }
    if (g.glitcht <= 0) return;
    if (g.glitcht > 62) {                 /* Phase A: old image ROLLS
                                           * vertically, with jitter */
        static unsigned char tmp[VIC_W * VIC_H];
        int ea = 124 - g.glitcht;
        int roff = (ea * 7 + rnd(16)) % VIC_H;
        if (roff > 0) {
            memcpy(tmp, fr, VIC_W * VIC_H);
            memcpy(fr, tmp + roff * VIC_W, (VIC_H - roff) * VIC_W);
            memcpy(fr + (VIC_H - roff) * VIC_W, tmp, roff * VIC_W);
        }
    } else {                              /* Phase B: new level pushes
                                           * into frame from below */
        e = 62 - g.glitcht;
        off = 200 - e * 5;
        if (g.glitcht > 20) off += rnd(9) - 4;   /* V-hold jitter */
        if (off < 0) off = 0;
        if (off >= VIC_H) {                      /* at the bottom: all black */
            memset(fr, 0, VIC_W * VIC_H);
        } else if (off > 0) {
            memmove(fr + off * VIC_W, fr, (VIC_H - off) * VIC_W);
            memset(fr, 0, off * VIC_W);
        }
    }
    n = (g.glitcht > 62) ? 8 : (g.glitcht > 20) ? 5 : 2;   /* line tears */
    for (i = 0; i < n; ++i) {
        int y = rnd(VIC_H - 6), h = 1 + rnd(5), dx = rnd(61) - 30, r, x;
        unsigned char row[VIC_W];
        for (r = y; r < y + h; ++r) {
            memcpy(row, fr + r * VIC_W, VIC_W);
            for (x = 0; x < VIC_W; ++x)
                fr[r * VIC_W + x] = row[(x + dx + VIC_W) % VIC_W];
        }
    }
    if (g.glitcht > 16 && rnd(2) == 0) {  /* black noise bars */
        int y = rnd(VIC_H - 8), h = 2 + rnd(5);
        memset(fr + y * VIC_W, 0, h * VIC_W);
    }
    /* Flutter snow bands (reference video 0:37): thick horizontal
     * clouds of short snow streaks, jumping every frame */
    n = (g.glitcht > 16) ? 3 + rnd(3) : 1;
    for (i = 0; i < n; ++i) {
        static const unsigned char spk[8] = { 1, 1, 15, 15, 12, 12, 11, 0 };
        int by = rnd(VIC_H), bh = 12 + rnd(40), r;
        for (r = by; r < by + bh && r < VIC_H; ++r) {
            int x = rnd(8);
            while (x < VIC_W) {
                int len = 1 + rnd(9), gap = 1 + rnd(12), x2;
                if (rnd(3)) {
                    unsigned char c = spk[rnd(8)];
                    for (x2 = x; x2 < x + len && x2 < VIC_W; ++x2)
                        fr[r * VIC_W + x2] = c;
                }
                x += len + gap;
            }
        }
    }
    n = 40 + g.glitcht * 2;               /* rest speckle all over */
    for (i = 0; i < n; ++i) {
        static const unsigned char spk[4] = { 1, 15, 12, 11 };
        int x = rnd(VIC_W - 1), y = rnd(VIC_H);
        unsigned char c = spk[rnd(4)];
        fr[y * VIC_W + x] = c;
        fr[y * VIC_W + x + 1] = c;
    }
}
