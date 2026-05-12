#ifndef DTW_H
#define DTW_H

#include <stdint.h>
#include "word_templates_data.h"

/*
 * Sakoe-Chiba band width.
 * Only template indices j in [i-DTW_BAND, i+DTW_BAND] are evaluated when
 * aligning query frame i.  For 31-frame sequences (15.6 ms / frame) a band
 * of 5 allows ±78 ms of timing flex — enough for natural speech variation
 * without letting unrelated words match.
 */
#define DTW_BAND 5

/*
 * Compute the DTW distance between a query (split SRAM arrays) and one flat
 * PROGMEM template (layout: STE×31 | ZCE×31 | G350×31 | … | G3500×31).
 *
 * Optimisations applied:
 *   Sakoe-Chiba band       — skips ~70 % of the cost matrix
 *   Two-row rolling buffer — 248 B static, no N×M heap allocation
 *   Squared-Euclidean step — no sqrt; order-preserving for classification
 *   uint32_t accumulator   — safe: realistic max ≈ 14 M, well below 2^32
 *
 * Lower return value = closer match.
 */
uint32_t dtw_distance(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    const uint8_t *tmpl_pgm
);

#endif
