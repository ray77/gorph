/* Original data of the C64 version of Gorph.
 * All tables carry the address of their source in the unpacked game.
 */
#ifndef GORPH_DATA_H
#define GORPH_DATA_H

#define SPRITE_BLOCKS 36
#define SPRITE_BASE_PTR 64

extern const unsigned char gd_sprites[SPRITE_BLOCKS*64]; /* $5000-$58FF   */
extern const unsigned char gd_font[1024];                /* $4800-$4BFF   */
extern const unsigned char gd_shapes1[66];               /* $B9EE         */
extern const unsigned char gd_shapes2[60];               /* $BA30         */
extern const unsigned char gd_shield_dome[18];           /* $9760         */
extern const unsigned char gd_shield_bowl[18];           /* $9772         */
extern const unsigned char gd_star_grp[4];               /* $961D         */
extern const unsigned char gd_star_col[33];              /* $9621         */
extern const unsigned char gd_star_row[32];              /* $9642         */
extern const unsigned char gd_matorder[24];              /* $88A5         */
extern const unsigned char gd_mattrig[8];                /* $88BD         */

#endif
