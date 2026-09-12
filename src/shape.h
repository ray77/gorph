/* shape.h - port of the software blitter from $9953/$9A49.
 *
 * Shapes are columns 4 multicolor pixels wide (1 byte per bitmap row).
 * The original code keeps four copies of the shape table, each shifted
 * by one MC pixel; the bits pushed out to the right end up in a carry
 * column 24 bytes further on.  When drawing, two adjacent bitmap
 * columns are therefore combined with OR or AND.
 */
#ifndef SHAPE_H
#define SHAPE_H

#define SHAPE_TAB_SIZE 1280      /* $1800-$1CFF */
#define SHAPE_SHIFT    312       /* $138 - stride of shift variants */
#define SHAPE_CARRY     24       /* $18  - stride of carry column */
#define SHAPE_LASER     30       /* index of the laser beam ($18F0)     */

extern unsigned char shape_tab[SHAPE_TAB_SIZE];

void shape_init(void);
/* x in multicolor pixels (0..159), y = base row (0..199), h = height,
 * rowoff = offset in character rows, erase != 0 clears instead of sets. */
void shape_blit(int index, int x, int y, int h, int rowoff, int erase);

#endif
