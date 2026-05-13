#ifndef DTW_H
#define DTW_H

#include <stdint.h>
#include "word_templates_data.h"

/*
 * Sakoe-Chiba band width.
 * For variable-length sequences, the effective band is:
 *   DTW_BAND + abs(query_len - template_len)
 * so valid end-to-end paths remain reachable.
 */
#define DTW_BAND 5

/*
 * Length-ratio guard to skip obviously mismatched templates:
 * if max(len_q, len_t) > DTW_LEN_RATIO_SKIP * min(len_q, len_t), return INF.
 */
#define DTW_LEN_RATIO_SKIP 2u

/*
 * Compute the DTW distance between a query (split SRAM arrays) and one
 * template in PROGMEM (layout per template:
 * STE[MAX_FEATURE_FRAMES], ZCE[MAX_FEATURE_FRAMES], G*[MAX_FEATURE_FRAMES]).
 *
 * Lower return value = closer match.
 */
uint32_t dtw_distance(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    uint8_t query_len,
    const uint8_t *tmpl_pgm,
    uint8_t tmpl_len
);

#endif
