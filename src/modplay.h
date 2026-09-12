/* modplay.h - kleiner ProTracker-MOD-Player (4 Kanaele, M.K.) fuer die
 * Credits.  Reines C89, keine Fremdbibliothek; rendert mono in double.
 *
 * Unterstuetzte Effekte: 0 Arpeggio, 1/2 Portamento, 3 Tonportamento,
 * 4 Vibrato, 9 Offset, A Volumeslide, B/D Sprung/Break, C Volume,
 * E1/E2/EA/EB/EC/ED Feinbefehle, F Speed/Tempo.  Reicht fuer typische
 * Chiptunes; Unbekanntes wird ignoriert.
 */
#ifndef MODPLAY_H
#define MODPLAY_H

int  mod_load(const unsigned char *data, unsigned long len, int rate);
void mod_start(void);                 /* von vorn, Zustand geloescht */
void mod_stop(void);
void mod_seek(double seconds);   /* von vorn still bis zur Position vorspulen */
int  mod_playing(void);
void mod_render(double *out, int n);  /* n Mono-Samples, -1..1, ueberschreibt */

#endif
