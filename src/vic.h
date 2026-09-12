/* vic.h - VIC-II display model.
 *
 * Gorph uses two modes that are switched per mission
 * (register table $826A in original):
 *   - Multicolor-TEXT  (Astro Battles, Flag Ship): matrix $4000 holds
 *     character codes, charset lives at $4800.
 *   - Multicolor-BITMAP (Laser Attack, Space Warp): bitmap $6000,
 *     matrix and color RAM only give the colors.
 * Plus sprites - GORPH runs natively, the 8-sprite hardware limit
 * is lifted (16 software sprites, priority still: 0 = front).
 */
#ifndef VIC_H_INCLUDED
#define VIC_H_INCLUDED

#define VIC_W        320
#define VIC_H        200
#define VIC_COLS      40
#define VIC_ROWS      25
#define VIC_BITMAP_SZ 8000
#define VIC_SCREEN_SZ 1000
#define NUM_SPRITES   16

#define VIC_MODE_TEXT   0
#define VIC_MODE_BITMAP 1

typedef struct {
    int           mode;                   /* $D011 Bit 5 */
    unsigned char bitmap[VIC_BITMAP_SZ];  /* $6000 */
    unsigned char screen[VIC_SCREEN_SZ];  /* $4000 */
    unsigned char color [VIC_SCREEN_SZ];  /* $D800 */
    unsigned char charset[1024];          /* $4800 - in RAM, is animated */
    unsigned char spdata[NUM_SPRITES][64];
    short         spx[NUM_SPRITES];
    short         spy[NUM_SPRITES];
    unsigned char spcol[NUM_SPRITES];
    unsigned short spenable;              /* $D015 (16 sprites) */
    unsigned short spmulti;               /* $D01C */
    unsigned short spxexp, spyexp;        /* $D01D / $D017 */
    unsigned char bg, border;             /* $D021 / $D020 */
    unsigned char bg1, bg2;               /* $D022 / $D023 */
    unsigned char spmc0, spmc1;           /* $D025 / $D026 */
} Vic;

extern Vic vic;

extern const unsigned long vic_palette[16];

void vic_reset(void);
void vic_render(unsigned char *out);

/* Image offset (screenshake), set by the game logic */
extern int vic_shake_x, vic_shake_y;

/* Collision checks per VIC rules (multicolor bit pair 01 is
 * transparent, 10/11 and hires bits collide): */
int vic_sprites_overlap(int a, int b);   /* $D01E: sprite vs. sprite    */
int vic_sprites_overlap_hw(int a, int b); /* ditto, but MC %01 transparent */
int vic_sprite_bg(int i);                /* $D01F: sprite vs. display   */
int vic_sprite_hits_code(int i, int lo, int hi);

#endif
