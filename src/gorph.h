/* gorph.h - Spielzustand des C64-Gorph-Ports.
 *
 * Die Datenhaltung folgt der disassemblierten Vorlage:
 *   - 24 Formationsgegner in parallelen Feldern (Zeropage $48/$60/$78),
 *     Koordinaten in Matrixzellen, Basiszeichencode als "Typ",
 *   - ein Schusszustand ($96), zwei Gegnerbomben ($97/$98),
 *   - Schild-Schadensnibbles ($1F-$30),
 *   - Missionsindex $0C: 0 Astro Battles, 1 Laser Attack,
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
    int  level;               /* $0D  - fortlaufend, Anzeige MS:xx */
    long score, hiscore;      /* $B1-$B3 / $B4-$B6 */
    int  frame;               /* $04  */

    /* Spieler */
    int  px, py;              /* Sprite-Koordinaten ($D000/$D001)   */
    int  bx, by;              /* Blitter-Position in Mission 1 ($4C/$4D) */
    int  pdead;               /* $09 >= 0 : stirbt gerade */

    /* Schuss: -1 = keiner, 1 = Bitmap-Laser, 2 = Sprite-Schuss ($96) */
    int  shot, shotx, shoty;  /* $4E/$4F bzw. Sprite 1 */

    /* Formation (Astro Battles) */
    unsigned char etype[NUM_ENEMIES];   /* $48+i, 0xFF = tot   */
    signed char   ex[NUM_ENEMIES];      /* $60+i, Spalte       */
    signed char   ey[NUM_ENEMIES];      /* $78+i, Zeile        */
    int  eleft;               /* $31 - verbleibende minus eins  */
    int  steptimer;           /* $02 */
    int  stepdelay;           /* $07 */
    int  animdir;             /* $90 = +4/-4 */
    int  xdir;                /* $91 = +-1   */
    int  matcount;            /* $34 - Materialisierungszaehler */
    int  matdone;             /* $35 < 0 = Einflug fertig       */
    int  gorphx, gorphdir, gorphtype, gorphtimer;  /* Sprite-7-Schiff */

    /* Bomben: Sprite 2 und 3 ($97/$98), x/y in Spritekoordinaten */
    int  bomb_on[2], bombx[2], bomby[2];

    /* Schild */
    unsigned char shield[18]; /* $1F-$30: Hi-Nibble links, Lo rechts */

    /* Explosionssprite 4 ($0A/$0B) */
    int  boomtimer, boomx, boomy;

    /* Laser Attack: Kanone = Anker; 8 Member mit Zustand wie $0058
     * (0=Formation, 1=Sturzflug, 2=Rueckkehr, 0xFF=tot) */
    unsigned char lm_state[8];
    int  lm_x[8], lm_y[8];              /* Sprite-Koordinaten */
    signed char lm_dx[8], lm_dy[8];
    int  lm_boom[8];
    int  lspawn;                        /* Spawn-Takt ($02/$07) */
    /* je Kanone ein eigener Strahl (Video: beide feuern gleichzeitig) */
    int  laser_on[2], laserx[2], lasery[2], lasertimer[2];
    /* zwei Laserkanonen, fliegen im Verbund mit */
    int  turret[2];
    int  tur_x[2], tur_y[2], tur_state[2], tur_timer[2], tur_anim[2];
    signed char tur_vx[2], tur_vy[2];

    /* Space Warp: 24 Driftobjekte (Radialspuren aus dem Fluchtpunkt) */
    int  wx[NUM_WARPOBJ], wy[NUM_WARPOBJ];      /* 1/8-Pixel */
    unsigned char wflags[NUM_WARPOBJ], wtype[NUM_WARPOBJ];
    signed char   wsub[NUM_WARPOBJ];
    int  warptimer;
    /* Space Warp: EIN Warp-Objekt (Sprite 2): 3 Typen x 3 Wachstumsstufen,
     * rotierender Vektor $8DC3/$8DE3; feuert Missiles (Sprites 4-6). */
    int  wobj_on, wobj_type, wobj_frame, wobj_angle, wobj_timer;
    int  wobj_x, wobj_y, wobj_boom;
    unsigned char wobj_quad, wobj_swap;
    unsigned char warp_shape[64];       /* Arbeitskopie Ring-Sprite (Block 21) */

    /* Steine (Space Warp + Flag Ship), bis 4 Slots */
    int  wmis_cool;                     /* Warp: Pause zwischen Steinwuerfen */
    int  stone_on[4], stone_x[4], stone_y[4];
    signed char stone_vx[4], stone_vy[4];
    int  stone_life[4], stone_frame[4];

    /* Flag Ship: grosses Schiff, bankt + steigt ab, Teile brechen ab */
    int  flagx, flagy, flagdir, flagdy, flag_face, flaghp;
    unsigned char flagdata[2][64];       /* erodierbare Arbeitskopie (Rumpf/Bug) */
    int  piece_on, piece_x, piece_y, piece_col;
    signed char piece_vx, piece_vy;
    int  flag_boomt, flag_bx, flag_by;  /* riesige Todes-Explosion (Strahlen) */
    int  flag_outro;                    /* Sterne fallen + Fade nach Explosion */
    int  flag_aggro;                    /* $8EB8: Steinrate steigt im Verlauf */
    int  fade;                          /* globale Helligkeit 0..256 */
    int  glitcht;                       /* TV-Glitch-Uebergang, Frames restlich */

    /* Punkte-Popup bei Abschuss der oberen Astro-Objekte (Arcade-Optik) */
    int  pop_t[2], pop_x[2], pop_y[2];
    char pop_s[2][4];

    /* Flag-Ship-Zerfall: Schiffspixel werden ballistisch geschleudert
     * (x/y in Achtelzellen, Geschwindigkeit + Gravitation) */
    int  fp_n;
    int  fp_x8[160], fp_y8[160];
    signed char   fp_vx[160], fp_vy[160];
    unsigned char fp_c[160];

    /* Schutzschirm-Rieseln im Finale (Pixel + Motion Blur + Fade) */
    int  ap_n;
    int  ap_x[48], ap_y8[48];
    signed char ap_v[48];

    int  robot_a;                       /* Outro-Watermark-Robot: Alpha 0..64 */
    int  bootzoom;                      /* Boot-Screen: 0 wartend, >0 Zoomphase */
    int  demo, demot;                   /* Attract-Mode: Flag + Frame-Zaehler */
    /* Typewriter-Alphablending: Zellen, die der RGB-Overlay stufenlos
     * dimmt (Einblend-Fade der Buchstaben + Cursor-Puls) */
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
void game_glitch(unsigned char *fr);   /* TV-Uebergang, nach vic_render */
void game_title_return(void);          /* Titel ohne Einmal-Events (Demo) */
void game_title_star(int k, int *sx, int *sy);  /* Credits-Sternmorph */
void game_debug_win(void);             /* TEMP Taste 6: Flagship-Kill */

#endif
