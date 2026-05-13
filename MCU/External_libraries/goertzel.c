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
 * Per-band right-shift applied to the SUM of two consecutive frame
 * pseudo-magnitudes
 * to produce a uint8 feature value (0-255).
 *
 * Calibrated for int16 speech from a 10-bit ADC (typical amplitude 50-300).
 * Low-frequency bins accumulate more magnitude → need a larger shift.
 * If a band is always 0:  decrease that shift by 2-3.
 * If a band is always 255: increase that shift by 2-3.
 */
const uint8_t GOERTZEL_SHIFTS[GOERTZEL_NUM_BINS] = {10, 8, 7, 6, 5};

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

static uint32_t abs32(int32_t value)
{
    if (value >= 0) return (uint32_t)value;
    return (uint32_t)(-(value + 1)) + 1u;
}

uint16_t goertzel_pseudo_magnitude(const GoertzelState *state, uint8_t bin)
{
    uint32_t a = abs32(state->s1[bin]);
    uint32_t b = abs32(state->s2[bin]);
    uint32_t mx = (a > b) ? a : b;
    uint32_t mn = (a > b) ? b : a;
    uint32_t mag = mx + (mn >> 1); /* alpha-max-plus-beta-min, beta≈0.5 */

    if (mag > UINT16_MAX) mag = UINT16_MAX;
    return (uint16_t)mag;
}
