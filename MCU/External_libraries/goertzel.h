#ifndef GOERTZEL_H
#define GOERTZEL_H

#include <stdint.h>

/*
 * Goertzel filter for 4 frequency bins, N=125 samples per frame at 8 kHz.
 *   bin 0 → k=6  → 384 Hz
 *   bin 1 → k=16 → 1024 Hz
 *   bin 2 → k=31 → 1984 Hz
 *   bin 3 → k=52 → 3328 Hz
 *
 * Coefficients: coeff_Q14[b] = round(2 * cos(2*pi*k/125) * 16384)
 */
#define GOERTZEL_NUM_BINS 4

/* Q14 fixed-point coefficients, one per bin */
extern const int16_t GOERTZEL_COEFFS[GOERTZEL_NUM_BINS];

typedef struct
{
    int32_t s1[GOERTZEL_NUM_BINS];
    int32_t s2[GOERTZEL_NUM_BINS];
} GoertzelState;

void     goertzel_reset(GoertzelState *state);
void     goertzel_update(GoertzelState *state, int16_t sample);
uint32_t goertzel_power(const GoertzelState *state, uint8_t bin);

#endif
