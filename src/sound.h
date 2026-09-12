/* sound.h - three-voice tone generation modeled on the SID.
 *
 * The original uses all three SID voices with pulse and noise
 * waveforms ($D400 ff., volume $D418 = $0F).  Here the voices
 * are generated in software and played back through SDL.
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
void sound_wind(int strength);   /* soft steady wind 0..100 (title orbit) */
void sound_mod_start(void);      /* Credits chiptune (embedded MOD) */
void sound_mod_stop(void);
void sound_mod_seek(double seconds);  /* Credits chiptune at time position */
void sound_wipe(int level);           /* very quiet wipe noise 0..100 */
void sound_mod_fade(int frames);  /* fade out MOD in N logic frames (50 Hz) */

#endif
