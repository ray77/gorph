/* modplay.h - small ProTracker MOD player (4 channels, M.K.) for the
 * credits.  Pure C89, no third-party library; renders mono in double.
 *
 * Supported effects: 0 arpeggio, 1/2 portamento, 3 tone portamento,
 * 4 vibrato, 9 offset, A volumeslide, B/D jump/break, C volume,
 * E1/E2/EA/EB/EC/ED fine commands, F speed/tempo.  Enough for typical
 * chiptunes; unknown ones are ignored.
 */
#ifndef MODPLAY_H
#define MODPLAY_H

int  mod_load(const unsigned char *data, unsigned long len, int rate);
void mod_start(void);                 /* restart, state cleared */
void mod_stop(void);
void mod_seek(double seconds);   /* from start, silently seek to position */
int  mod_playing(void);
void mod_render(double *out, int n);  /* n mono samples, -1..1, overwrites */

#endif
