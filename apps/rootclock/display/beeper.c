/* rootclock -- Beeper: the time-limit "double pip". Contract: beeper.h.
 * Pattern: external/wiliOGbsp/apps/bench/display/main.c (cmd_tone). */
#include "beeper.h"

#include <math.h>
#include <stdint.h>

#include "fwog_display.h"
#include "hardware/pio.h"

/* i2s_audio_init() fixes the sample rate at 8000 Hz (Nyquist 4 kHz), so the
 * tone must stay well below that; 1 kHz is exactly 8 samples per period. */
#define BEEP_RATE_HZ    8000u
#define BEEP_TONE_HZ    1000u
#define BEEP_PIP_MS     150u
#define BEEP_GAP_MS     100u
#define BEEP_FADE_MS    5u      /* linear ramp at each pip edge: no clicks */
#define BEEP_AMPLITUDE  12000.0f

#define BEEP_PIP_N   (BEEP_RATE_HZ * BEEP_PIP_MS / 1000u)    /* 1200 */
#define BEEP_GAP_N   (BEEP_RATE_HZ * BEEP_GAP_MS / 1000u)    /*  800 */
#define BEEP_FADE_N  (BEEP_RATE_HZ * BEEP_FADE_MS / 1000u)   /*   40 */
#define BEEP_N       (2u * BEEP_PIP_N + BEEP_GAP_N)          /* 3200 */

#define BEEP_PIO_SM  2u   /* pio0: sm0 is the WS2812 bar (beeper.h) */

#define BEEP_TWO_PI  6.28318530718f

/* i2s_audio_start() does not copy: the refill path reads this for the whole
 * note, so it must be static. 3200 int16 = 6.4 KB of .bss. */
static int16_t s_beep[BEEP_N];
static bool s_ready;

static void synth_pip(int16_t *out) {
    for (unsigned i = 0u; i < BEEP_PIP_N; i++) {
        float env = 1.0f;
        if (i < BEEP_FADE_N) {
            env = (float)i / (float)BEEP_FADE_N;
        } else if (i >= BEEP_PIP_N - BEEP_FADE_N) {
            env = (float)(BEEP_PIP_N - 1u - i) / (float)BEEP_FADE_N;
        }
        const float phase = BEEP_TWO_PI * (float)BEEP_TONE_HZ * (float)i /
                            (float)BEEP_RATE_HZ;
        out[i] = (int16_t)(BEEP_AMPLITUDE * env * sinf(phase));
    }
}

bool beeper_init(void) {
    if (s_ready) return true;
    if (!i2s_audio_init(pio0, BEEP_PIO_SM)) return false;

    synth_pip(&s_beep[0]);
    for (unsigned i = 0u; i < BEEP_GAP_N; i++) s_beep[BEEP_PIP_N + i] = 0;
    synth_pip(&s_beep[BEEP_PIP_N + BEEP_GAP_N]);

    s_ready = true;
    return true;
}

void beeper_beep(void) {
    if (!s_ready || !i2s_audio_is_idle()) return;
    (void)i2s_audio_start(s_beep, BEEP_N, true, false);
}

void beeper_process(void) {
    if (s_ready) i2s_audio_process();
}
