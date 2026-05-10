#include "goertzel.h"

/* round(2 * cos(2*pi*f/8000) * 16384) for f = 350,900,1700,2700,3500 Hz */
const int16_t GOERTZEL_COEFFS[GOERTZEL_NUM_BINS] = {
     31539,   /* 350 Hz  */
     25093,   /* 900 Hz  */
      7852,   /* 1700 Hz */
    -16854,   /* 2700 Hz */
    -30274    /* 3500 Hz */
};

/*
 * Per-band right-shift applied to the SUM of two consecutive frame powers
 * to produce a uint8 feature value (0-255).
 *
 * Calibrated for int16 speech from a 10-bit ADC (typical amplitude 50-300).
 * Low-frequency bins accumulate far more power → need a larger shift.
 * If a band is always 0:  decrease that shift by 2-3.
 * If a band is always 255: increase that shift by 2-3.
 */
const uint8_t GOERTZEL_SHIFTS[GOERTZEL_NUM_BINS] = {20, 16, 14, 12, 10};

void goertzel_reset(GoertzelState *state)
{
    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
    {
        state->s1[b] = 0;
        state->s2[b] = 0;
    }
}

void goertzel_update(GoertzelState *state, int16_t sample)
{
    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
    {
        int32_t s0 = (int32_t)sample
                   + (((int32_t)GOERTZEL_COEFFS[b] * state->s1[b]) >> 14)
                   - state->s2[b];
        state->s2[b] = state->s1[b];
        state->s1[b] = s0;
    }
}

uint32_t goertzel_power(const GoertzelState *state, uint8_t bin)
{
    int32_t s1 = state->s1[bin];
    int32_t s2 = state->s2[bin];
    int16_t c  = GOERTZEL_COEFFS[bin];

    int64_t cross = ((int64_t)c * s1 * s2) >> 14;
    int64_t power = (int64_t)s1 * s1 + (int64_t)s2 * s2 - cross;

    return (power < 0) ? 0u : (uint32_t)power;
}
