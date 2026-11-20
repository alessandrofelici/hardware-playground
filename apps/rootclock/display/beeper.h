/* rootclock -- Beeper: the time-limit sound.
 *
 * The BSP has no tone API: i2s_audio_start() plays a caller-owned sample
 * buffer at a fixed 8000 Hz (keep content <= 2.5 kHz to avoid aliasing) and
 * does not copy it, so the buffer must be static. This synthesises one
 * "double pip" -- two ~150 ms ~1 kHz sine bursts with a ~100 ms gap, short
 * fades to avoid clicks -- into a static int16 buffer once at init, and
 * replays it. Pattern: external/wiliOGbsp/apps/bench/display/main.c (tone).
 *
 * PIO: pio0 state machine 2 (WS2812 is pio0 sm0; same allocation as the BSP's
 * bench app). */
#ifndef BEEPER_H
#define BEEPER_H
#include <stdbool.h>

/* i2s_audio_init(pio0, 2) and synthesise the sound. Returns false if audio
 * could not be initialised; beeper_beep() is then a silent no-op. */
bool beeper_init(void);

/* Start the double pip. Ignored if a sound is already playing. Non-blocking. */
void beeper_beep(void);

/* Call every main-loop iteration (drives i2s_audio_process()). */
void beeper_process(void);

#endif
