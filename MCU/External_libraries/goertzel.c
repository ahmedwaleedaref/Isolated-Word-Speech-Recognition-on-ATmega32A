#include "goertzel.h"

/*
 * Q14 coefficients: round(2 * cos(2*pi*k/125) * 16384)
 *   bin 0: k=6  → 31289
 *   bin 1: k=16 → 22730
 *   bin 2: k=31 → 412
 *   bin 3: k=52 → -28309
 */
const int16_t GOERTZEL_COEFFS[GOERTZEL_NUM_BINS] = {31289, 22730, 412, -28309};

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

    /* power = s1^2 + s2^2 - coeff*s1*s2/16384   (uses int64_t once per frame) */
    int64_t cross = ((int64_t)c * s1 * s2) >> 14;
    int64_t power = (int64_t)s1 * s1 + (int64_t)s2 * s2 - cross;

    return (power < 0) ? 0u : (uint32_t)power;
}
