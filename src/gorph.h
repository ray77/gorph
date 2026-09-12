/* gorph.h - game state of the C64 Gorph port.
 *
 * The data layout follows the disassembled original:
 *   - 24 formation enemies in parallel arrays (zeropage $48/$60/$78),
 *     coordinates in matrix cells, base character code as "type",
 *   - one shot state ($96), two enemy bombs ($97/$98),
 *   - shield damage nibbles ($1F-$30),
 *   - mission index $0C: 0 Astro Battles, 1 Laser Attack,
 *     2 Space Warp, 3 Flag Ship.
 */
#ifndef GORPH_H
#define GORPH_H

#define NUM_ENEMIES   24
#define NUM_MISSIONS   4
#define MAX_RANK       5
#define NUM_WARPOBJ   25

enum {
    ST_TITLE, ST_MISSION_INTRO, ST_PLAY, ST_PLAYER_HIT,
    ST_MISSION_CLEAR, ST_GAME_OVER, ST_BOOT, ST_CREDITS
};

typedef struct {
    int  state, statetimer;
    int  mission;             /* $0C  */
    int  rank;                /* $A4  */
    int  lives;               /* $C0  */
    int  level;               /* $0D  - running, display MS:xx */
    long score, hiscore;      /* $B1-$B3 / $B4-$B6 */
    int  frame;               /* $04  */

    /* Player */
    int  px, py;              /* Sprite coordinates ($D000/$D001)   */
    int  bx, by;              /* Blitter position in mission 1 ($4C/$4D) */
    int  pdead;               /* $09 >= 0 : is dying */

    /* Shot: -1 = none, 1 = bitmap laser, 2 = sprite shot ($96) */
    int  shot, shotx, shoty;  /* $4E/$4F or sprite 1 */

    /* Formation (Astro Battles) */
    unsigned char etype[NUM_ENEMIES];   /* $48+i, 0xFF = dead  */
    signed char   ex[NUM_ENEMIES];      /* $60+i, column      */
    signed char   ey[NUM_ENEMIES];      /* $78+i, row         */
    int  eleft;               /* $31 - remaining minus one     */
    int  steptimer;           /* $02 */
    int  stepdelay;           /* $07 */
    int  animdir;             /* $90 = +4/-4 */
    int  xdir;                /* $91 = +-1   */
    int  matcount;            /* $34 - materialization counter  */
    int  matdone;             /* $35 < 0 = fly-in done          */
    int  gorphx, gorphdir, gorphtype, gorphtimer;  /* Sprite-7 ship */

    /* Bombs: sprite 2 and 3 ($97/$98), x/y in sprite coordinates */
    int  bomb_on[2], bombx[2], bomby[2];

    /* Shield */
    unsigned char shield[18]; /* $1F-$30: hi nibble left, lo right */

    /* Explosion sprite 4 ($0A/$0B) */
    int  boomtimer, boomx, boomy;

    /* Laser Attack: cannon = anchor; 8 members with state as $0058
     * (0=formation, 1=dive, 2=return, 0xFF=dead) */
    unsigned char lm_state[8];
    int  lm_x[8], lm_y[8];              /* Sprite coordinates */
    signed char lm_dx[8], lm_dy[8];
    int  lm_boom[8];
    int  lspawn;                        /* Spawn tick ($02/$07) */
    /* one beam per cannon (video: both fire at the same time) */
    int  laser_on[2], laserx[2], lasery[2], lasertimer[2];
    /* two laser cannons, fly along in formation */
    int  turret[2];
    int  tur_x[2], tur_y[2], tur_state[2], tur_timer[2], tur_anim[2];
    signed char tur_vx[2], tur_vy[2];

    /* Space Warp: 24 drifters (radial trails from vanishing point) */
    int  wx[NUM_WARPOBJ], wy[NUM_WARPOBJ];      /* 1/8 pixel */
    unsigned char wflags[NUM_WARPOBJ], wtype[NUM_WARPOBJ];
    signed char   wsub[NUM_WARPOBJ];
    int  warptimer;
    /* Space Warp: ONE warp object (sprite 2): 3 types x 3 growth stages,
     * rotating vector $8DC3/$8DE3; fires missiles (sprites 4-6). */
    int  wobj_on, wobj_type, wobj_frame, wobj_angle, wobj_timer;
    int  wobj_x, wobj_y, wobj_boom;
    unsigned char wobj_quad, wobj_swap;
    unsigned char warp_shape[64];       /* working copy ring sprite (Block 21) */

    /* rocks (Space Warp + Flag Ship), max 4 slots */
    int  wmis_cool;                     /* Warp: pause between rock throws */
    int  stone_on[4], stone_x[4], stone_y[4];
    signed char stone_vx[4], stone_vy[4];
    int  stone_life[4], stone_frame[4];

    /* Flag Ship: big ship, banks + descends, parts break off */
    int  flagx, flagy, flagdir, flagdy, flag_face, flaghp;
    unsigned char flagdata[2][64];       /* erodable working copy (hull/bow) */
    int  piece_on, piece_x, piece_y, piece_col;
    signed char piece_vx, piece_vy;
    int  flag_boomt, flag_bx, flag_by;  /* huge death explosion (rays) */
    int  flag_outro;                    /* stars fall + fade after explosion */
    int  flag_aggro;                    /* $8EB8: rock rate rises over time */
    int  fade;                          /* global brightness 0..256 */
    int  glitcht;                       /* TV glitch transition, frames left */

    /* score popup when upper Astro objects are shot (arcade look) */
    int  pop_t[2], pop_x[2], pop_y[2];
    char pop_s[2][4];

    /* Flag Ship breakup: ship pixels are hurled ballistically
     * (x/y in eighth cells, velocity + gravity) */
    int  fp_n;
    int  fp_x8[160], fp_y8[160];
    signed char   fp_vx[160], fp_vy[160];
    unsigned char fp_c[160];

    /* shield trickle in the finale (pixels + motion blur + fade) */
    int  ap_n;
    int  ap_x[48], ap_y8[48];
    signed char ap_v[48];

    int  robot_a;                       /* Outro-Watermark-Robot: Alpha 0..64 */
    int  bootzoom;                      /* boot screen: 0 idle, >0 zoom phase */
    int  demo, demot;                   /* attract mode: flag + frame counter */
    /* typewriter alpha blending: cells the RGB overlay dims
     * steplessly (letter fade-in + cursor pulse) */
    int  tw_n;
    unsigned char  tw_cx[64], tw_cy[64];
    unsigned short tw_a[64];

    unsigned char in_left, in_right, in_up, in_down, in_fire;
    unsigned char fire_edge;
} Game;

extern Game g;

void game_init(void);
void game_start_mission(void);
void game_frame(void);
void game_draw(void);
void game_glitch(unsigned char *fr);   /* TV transition post vic_render */
void game_title_return(void);          /* title, no one-off events (demo) */
void game_title_star(int k, int *sx, int *sy);  /* credits star morph */
void game_debug_win(void);             /* TEMP key 6: flagship kill */

#endif
