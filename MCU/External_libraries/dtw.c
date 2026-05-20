#include "dtw.h"
#include <avr/pgmspace.h>

/* Per-channel weights for squared-distance contributions in DTW cost.
 * Compensates for (a) raw-scale mismatch (ZCE is uint8 counts in ~10-80
 * range while other channels are peak-normalized to 0-255) and (b)
 * empirical Fisher-discriminability ranking on this vocabulary. */
#define W_STE   1u
#define W_ZCE   6u
#define W_G350  1u
#define W_G900  1u
#define W_G1700 2u
#define W_G2700 1u
#define W_G3500 1u

/* Overflow sanity check:
 * Max per-cell cost = 255^2 * 9 * 7 channels ≈ 4.1M.
 * Max path length ≈ 53. Max total ≈ 217M. Well within UINT32_MAX (4.29B). Safe. */
static const uint8_t GOERTZEL_WEIGHTS[GOERTZEL_NUM_BINS] = {
    W_G350, W_G900, W_G1700, W_G2700, W_G3500
};

/* Two-row rolling buffer: only the previous and current rows are live at once.
 * Static so it is in fixed SRAM, not on the call stack. */
static uint32_t _row[2][STE_FEATURE_COUNT];

/*
 * Squared-Euclidean distance across all 7 feature channels for a single
 * (query frame qi, template frame ti) pair.
 *
 * Template channel offsets in PROGMEM template data:
 *   STE  : [0 .. MAX_FEATURE_FRAMES-1]
 *   ZCE  : [MAX_FEATURE_FRAMES .. 2*MAX_FEATURE_FRAMES-1]
 *   G[b] : [(2+b)*MAX_FEATURE_FRAMES .. (3+b)*MAX_FEATURE_FRAMES-1]
 */
static uint32_t step_cost(
    const uint8_t *ste_q,
    const uint8_t *zce_q,
    const uint8_t goertzel_q[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    const uint8_t *tmpl,
    uint8_t qi,
    uint8_t ti)
{
    int16_t  d;
    uint32_t cost;

    d    = (int16_t)ste_q[qi]
         - (int16_t)pgm_read_byte(&tmpl[ti]);
    cost = (uint32_t)((int32_t)d * d) * W_STE;

    d     = (int16_t)zce_q[qi]
          - (int16_t)pgm_read_byte(&tmpl[MAX_FEATURE_FRAMES + ti]);
    cost += (uint32_t)((int32_t)d * d) * W_ZCE;

    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
    {
        uint16_t off = (uint16_t)(2u + b) * MAX_FEATURE_FRAMES + ti;
        d     = (int16_t)goertzel_q[b][qi]
              - (int16_t)pgm_read_byte(&tmpl[off]);
        cost += (uint32_t)((int32_t)d * d) * GOERTZEL_WEIGHTS[b];
    }

    return cost;
}

uint32_t dtw_distance(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    uint8_t query_len,
    const uint8_t *tmpl_pgm,
    uint8_t tmpl_len)
{
    if (query_len == 0 || tmpl_len == 0) return UINT32_MAX;

    if ((uint16_t)query_len > (uint16_t)tmpl_len * DTW_LEN_RATIO_SKIP ||
        (uint16_t)tmpl_len > (uint16_t)query_len * DTW_LEN_RATIO_SKIP)
        return UINT32_MAX;

    uint8_t prv = 0, cur = 1;
    uint8_t len_diff = (query_len > tmpl_len) ? (query_len - tmpl_len) : (tmpl_len - query_len);
    uint8_t band = DTW_BAND + len_diff;
    uint8_t max_len = (query_len > tmpl_len) ? query_len : tmpl_len;
    if (band >= max_len) band = max_len - 1;

    /* Initialise both rows to infinity */
    for (uint8_t j = 0; j < tmpl_len; j++)
    {
        _row[0][j] = UINT32_MAX;
        _row[1][j] = UINT32_MAX;
    }

    for (uint8_t i = 0; i < query_len; i++)
    {
        uint8_t j_lo = (i > band)                ? (i - band) : 0;
        uint8_t j_hi = (i + band < tmpl_len)     ? (i + band) : (tmpl_len - 1);

        /* Poison the cells just outside the band so a neighbour lookup in the
         * next column never accidentally reads a stale finite value. */
        if (j_lo > 0)           _row[cur][j_lo - 1] = UINT32_MAX;
        if (j_hi < tmpl_len - 1) _row[cur][j_hi + 1] = UINT32_MAX;

        for (uint8_t j = j_lo; j <= j_hi; j++)
        {
            uint32_t sc = step_cost(ste, zce, goertzel, tmpl_pgm, i, j);

            uint32_t best;
            if (i == 0 && j == 0)
            {
                best = 0;
            }
            else
            {
                /* Three predecessors: above (insert), diagonal, left (delete) */
                best                 = _row[prv][j];                       /* above    */
                uint32_t from_diag   = (j > 0) ? _row[prv][j - 1] : UINT32_MAX;
                uint32_t from_left   = (j > 0) ? _row[cur][j - 1] : UINT32_MAX;
                if (from_diag < best) best = from_diag;
                if (from_left < best) best = from_left;
            }

            _row[cur][j] = (best == UINT32_MAX) ? UINT32_MAX : best + sc;
        }

        /* Roll: current becomes previous for the next query frame */
        uint8_t tmp = prv; prv = cur; cur = tmp;
    }

    uint32_t raw = _row[prv][tmpl_len - 1];
    if (raw == UINT32_MAX) return UINT32_MAX;
    /* max(query_len, tmpl_len) is a tight lower bound on path length for a
     * symmetric step pattern in a Sakoe-Chiba band. Using it as the divisor
     * makes distances comparable across templates of different lengths. */
    uint8_t denom = (query_len > tmpl_len) ? query_len : tmpl_len;
    return raw / denom;
}
