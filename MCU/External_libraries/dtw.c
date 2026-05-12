#include "dtw.h"
#include <avr/pgmspace.h>

#define DTW_LEN STE_FEATURE_COUNT   /* 31 — both query and template time steps */

/* Two-row rolling buffer: only the previous and current rows are live at once.
 * Static so it is in fixed SRAM, not on the call stack. */
static uint32_t _row[2][DTW_LEN];

/*
 * Squared-Euclidean distance across all 7 feature channels for a single
 * (query frame qi, template frame ti) pair.
 *
 * Template channel offsets in the flat PROGMEM array:
 *   STE  : [0  .. 30]
 *   ZCE  : [31 .. 61]
 *   G[b] : [62 + b*31 .. 62 + b*31 + 30]
 */
static uint32_t step_cost(
    const uint8_t *ste_q,
    const uint8_t *zce_q,
    const uint8_t goertzel_q[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    const uint8_t *tmpl,
    uint8_t qi, uint8_t ti)
{
    int16_t  d;
    uint32_t cost;

    d    = (int16_t)ste_q[qi]
         - (int16_t)pgm_read_byte(&tmpl[ti]);
    cost = (uint32_t)((int32_t)d * d);

    d     = (int16_t)zce_q[qi]
          - (int16_t)pgm_read_byte(&tmpl[STE_FEATURE_COUNT + ti]);
    cost += (uint32_t)((int32_t)d * d);

    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
    {
        uint8_t off = STE_FEATURE_COUNT + ZCE_FEATURE_COUNT
                    + b * GOERTZEL_FEATURE_COUNT_PER_BIN + ti;
        d     = (int16_t)goertzel_q[b][qi]
              - (int16_t)pgm_read_byte(&tmpl[off]);
        cost += (uint32_t)((int32_t)d * d);
    }

    return cost;
}

uint32_t dtw_distance(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    const uint8_t *tmpl_pgm)
{
    uint8_t prv = 0, cur = 1;

    /* Initialise both rows to infinity */
    for (uint8_t j = 0; j < DTW_LEN; j++)
    {
        _row[0][j] = UINT32_MAX;
        _row[1][j] = UINT32_MAX;
    }

    for (uint8_t i = 0; i < DTW_LEN; i++)
    {
        uint8_t j_lo = (i > DTW_BAND)              ? (i - DTW_BAND) : 0;
        uint8_t j_hi = (i + DTW_BAND < DTW_LEN)    ? (i + DTW_BAND) : (DTW_LEN - 1);

        /* Poison the cells just outside the band so a neighbour lookup in the
         * next column never accidentally reads a stale finite value. */
        if (j_lo > 0)           _row[cur][j_lo - 1] = UINT32_MAX;
        if (j_hi < DTW_LEN - 1) _row[cur][j_hi + 1] = UINT32_MAX;

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

    return _row[prv][DTW_LEN - 1];
}
