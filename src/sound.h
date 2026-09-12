/* sound.h - dreistimmige Tonerzeugung nach Vorbild des SID.
 *
 * Das Original benutzt alle drei SID-Stimmen mit Rechteck- und
 * Rauschwellenform ($D400 ff., Lautstaerke $D418 = $0F).  Hier werden
 * die Stimmen in Software erzeugt und ueber SDL ausgegeben.
 */
#ifndef SOUND_H
#define SOUND_H

enum {
    SND_SHOOT, SND_HIT, SND_EXPLODE, SND_STEP, SND_PLAYER_DIE,
    SND_MISSION, SND_EXTRA, SND_BEAM, SND_EJECT, SND_BIGBOOM,
    SND_SIREN, SND_WON, SND_GLITCH, SND_TICK,
    SND_PRESENTS, SND_GORPHVOICE, SND_WON2, SND_KEY, SND_TAKEOVER,
    SND_CREDBOOM, SND_GLASS, SND_HAHA, SND_IEHH, SND_OUH,
    SND_COUNT
};

int  sound_init(void);
void sound_shutdown(void);
void sound_play(int id);
void sound_wind(int strength);   /* leiser Dauerwind 0..100 (Titel-Orbit) */
void sound_mod_start(void);      /* Credits-Chiptune (eingebettetes MOD) */
void sound_mod_stop(void);
void sound_mod_seek(double seconds);  /* Credits-Chiptune auf Zeitposition */
void sound_wipe(int level);           /* sehr leises Wischgeraeusch 0..100 */
void sound_mod_fade(int frames);  /* MOD in N Logikframes (50 Hz) ausblenden */

#endif
