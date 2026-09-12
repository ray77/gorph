/* vic.h - VIC-II-Anzeigemodell.
 *
 * Gorph benutzt zwei Modi, die pro Mission umgeschaltet werden
 * (Registertabelle $826A im Original):
 *   - Multicolor-TEXT  (Astro Battles, Flag Ship): Matrix $4000 traegt
 *     Zeichencodes, Zeichensatz liegt bei $4800.
 *   - Multicolor-BITMAP (Laser Attack, Space Warp): Bitmap $6000,
 *     Matrix und Farb-RAM liefern nur die Farben.
 * Dazu die Sprites - GORPH laeuft nativ, das 8er-Hardware-Limit ist
 * aufgehoben (16 Software-Sprites, Prioritaet weiter: 0 = vorn).
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
    unsigned char charset[1024];          /* $4800 - im RAM, wird animiert */
    unsigned char spdata[NUM_SPRITES][64];
    short         spx[NUM_SPRITES];
    short         spy[NUM_SPRITES];
    unsigned char spcol[NUM_SPRITES];
    unsigned short spenable;              /* $D015 (16 Sprites) */
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

/* Bildversatz (Screenshake), von der Spiellogik gesetzt */
extern int vic_shake_x, vic_shake_y;

/* Kollisionsabfragen nach VIC-Regeln (Multicolor-Bitpaar 01 ist
 * durchsichtig, 10/11 und Hires-Bits kollidieren): */
int vic_sprites_overlap(int a, int b);   /* $D01E: Sprite gegen Sprite  */
int vic_sprites_overlap_hw(int a, int b); /* dito, aber MC %01 transparent */
int vic_sprite_bg(int i);                /* $D01F: Sprite gegen Anzeige */
int vic_sprite_hits_code(int i, int lo, int hi);

#endif
