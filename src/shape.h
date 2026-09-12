/* shape.h - Portierung des Software-Blitters aus $9953/$9A49.
 *
 * Shapes sind 4 Multicolor-Pixel breite Spalten (1 Byte je Bitmap-Zeile).
 * Der Original-Code haelt vier um je ein MC-Pixel verschobene Kopien der
 * Shape-Tabelle vor; die dabei rechts herausgeschobenen Bits landen in
 * einer Uebertragsspalte 24 Byte weiter.  Beim Zeichnen werden daher
 * immer zwei benachbarte Bitmap-Spalten mit OR bzw. AND verknuepft.
 */
#ifndef SHAPE_H
#define SHAPE_H

#define SHAPE_TAB_SIZE 1280      /* $1800-$1CFF */
#define SHAPE_SHIFT    312       /* $138 - Abstand der Shift-Varianten */
#define SHAPE_CARRY     24       /* $18  - Abstand der Uebertragsspalte */
#define SHAPE_LASER     30       /* Index des Laserstrahls ($18F0)      */

extern unsigned char shape_tab[SHAPE_TAB_SIZE];

void shape_init(void);
/* x in Multicolor-Pixeln (0..159), y = Basiszeile (0..199), h = Hoehe,
 * rowoff = Offset in Zeichenzeilen, erase != 0 loescht statt zu setzen. */
void shape_blit(int index, int x, int y, int h, int rowoff, int erase);

#endif
