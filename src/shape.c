#include "shape.h"
#include "vic.h"
#include "gorph_data.h"
#include <string.h>

unsigned char shape_tab[SHAPE_TAB_SIZE];

/* entspricht $9A49: Tabelle loeschen, Master-Shapes einsetzen,
 * danach kaskadierend die drei verschobenen Kopien erzeugen. */
void shape_init(void)
{
    int i, page, y;

    memset(shape_tab, 0, sizeof(shape_tab));
    memcpy(shape_tab + 0x004, gd_shapes1, sizeof(gd_shapes1));
    memcpy(shape_tab + 0x07A, gd_shapes2, sizeof(gd_shapes2));
    for (i = 0; i < 8; ++i)
        shape_tab[0x0F0 + i] = 0x60;          /* Laserstrahl */

    for (page = 0; page < 5; ++page) {
        for (y = 0; y < 256; ++y) {
            int src = page * 256 + y;
            int d1  = src + SHAPE_SHIFT;
            int d2  = src + SHAPE_SHIFT + SHAPE_CARRY;
            unsigned char a;
            if (d2 >= SHAPE_TAB_SIZE) continue;
            a = shape_tab[src];
            shape_tab[d1] = (unsigned char)(shape_tab[d1] | (a >> 2));
            shape_tab[d2] = (unsigned char)(shape_tab[d2] | ((a << 6) & 0xFF));
        }
    }
}

/* entspricht $9967: zeichnet h Zeilen nach oben, ab Zeile y-1 */
void shape_blit(int index, int x, int y, int h, int rowoff, int erase)
{
    int variant = x & 3;
    int src     = variant * SHAPE_SHIFT + index * 8;
    int col     = (x & ~3) >> 2;            /* Bitmap-Spalte in Bytes  */
    int base    = y + rowoff * 8;
    int k;

    if (src < 0 || src + 8 > SHAPE_TAB_SIZE) return;

    for (k = 0; k < h; ++k) {
        int si = h - 1 - k;
        int dy = base - 1 - k;
        int part;
        if (dy < 0 || dy >= VIC_H) continue;
        for (part = 0; part < 2; ++part) {
            int sidx = src + si + part * SHAPE_CARRY;
            int cx   = col + part;
            unsigned char v;
            int off;
            if (cx < 0 || cx >= VIC_COLS) continue;
            if (sidx >= SHAPE_TAB_SIZE) continue;
            v = shape_tab[sidx];
            if (!v) continue;
            off = (dy >> 3) * (VIC_COLS * 8) + cx * 8 + (dy & 7);
            if (off < 0 || off >= VIC_BITMAP_SZ) continue;
            if (erase)
                vic.bitmap[off] = (unsigned char)(vic.bitmap[off] & ~v);
            else
                vic.bitmap[off] = (unsigned char)(vic.bitmap[off] | v);
        }
    }
}
