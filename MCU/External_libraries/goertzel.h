#ifndef GOERTZEL_H
#define GOERTZEL_H

#include <stdint.h>

/*
 * 5 formant-aligned Goertzel bins at 8 kHz.
 * coeff = round(2 * cos(2*pi*f/8000) * 16384)  [Q14]
 *
 * Bin 0:  350 Hz → 31539  (F1 low: nasal murmur, closed vowels)
 * Bin 1:  900 Hz → 25093  (F1 high: open vowels, back vowels)
 * Bin 2: 1700 Hz →  7852  (F2: mid vowels)
 * Bin 3: 2700 Hz → -16854 (F3: front vowels, laterals /l/ /r/)
 * Bin 4: 3500 Hz → -30274 (Fricatives /s/ /f/, plosive bursts /t/ /p/)
 *
 * GOERTZEL_SHIFTS[b]: right-shift for the TWO-FRAME power sum to uint8.
 * Low-freq bins have more energy → larger shift.
 * Tune down by 2-3 if a band is always near zero on real speech.
 */
#define GOERTZEL_NUM_BINS 5

extern const int16_t GOERTZEL_COEFFS[GOERTZEL_NUM_BINS];
extern const uint8_t GOERTZEL_SHIFTS[GOERTZEL_NUM_BINS];

typedef struct
{
    int32_t s1[GOERTZEL_NUM_BINS];
    int32_t s2[GOERTZEL_NUM_BINS];
} GoertzelState;

void     goertzel_reset(GoertzelState *state);
void     goertzel_update(GoertzelState *state, int16_t sample);
uint32_t goertzel_power(const GoertzelState *state, uint8_t bin);

#endif
