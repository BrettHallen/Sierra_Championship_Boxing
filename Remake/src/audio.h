/* PC speaker (and PCjr sound chip) effects, synthesised from what the
 * original's sound routines do (docs/GAMEPLAY.md section 13). */
#ifndef CB_AUDIO_H
#define CB_AUDIO_H

/* Open the default playback device. Returns 0 if there is no sound. */
int  audio_init(void);
void audio_quit(void);
/* Play an SFX_* (fight.h). sound: the Sound option (0 off, 1 internal
 * speaker, 2 external = PCjr chip); bell: the Bell option (0 beep, 1 ring). */
void audio_play(int sfx, int sound, int bell);
/* The intro tune: pairs of timer divisor (0 = rest) and length in steps of
 * a 500-call pause (FGT 231F). audio_stop() cuts it (and anything queued). */
void audio_tune(const unsigned short (*tune)[2], int n, int sound);
void audio_stop(void);
/* Render an effect into a float buffer at the given rate (for tests).
 * Returns the number of samples written (at most max). */
int  audio_render(int sfx, int sound, int bell, float *out, int max, int rate);

#endif
