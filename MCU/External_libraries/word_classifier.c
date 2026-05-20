#include "word_classifier.h"
#include "dtw.h"
#include <avr/pgmspace.h>

uint8_t classify_word(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    uint8_t query_len
)
{
    uint32_t dist_by_tmpl[WORD_COUNT][MAX_TEMPLATES_PER_WORD];

    /* 1. Compute all DTW distances; pad unused slots with UINT32_MAX. */
    for (uint8_t w = 0; w < WORD_COUNT; w++)
    {
        uint8_t n = pgm_read_byte(&WORD_TEMPLATE_COUNTS[w]);
        for (uint8_t t = 0; t < MAX_TEMPLATES_PER_WORD; t++)
        {
            if (t >= n)
            {
                dist_by_tmpl[w][t] = UINT32_MAX;
                continue;
            }
            uint8_t tmpl_len = pgm_read_byte(&WORD_TEMPLATE_LENGTHS[w][t]);
            const uint8_t *tmpl = &WORD_TEMPLATES[w][t][0][0];
            dist_by_tmpl[w][t] = dtw_distance(ste, zce, goertzel, query_len, tmpl, tmpl_len);
        }
    }

    /* 2. For each word, insertion-sort distances ascending (n <= 7). */
    for (uint8_t w = 0; w < WORD_COUNT; w++)
    {
        for (uint8_t i = 1; i < MAX_TEMPLATES_PER_WORD; i++)
        {
            uint32_t key = dist_by_tmpl[w][i];
            int8_t j = (int8_t)i - 1;
            while (j >= 0 && dist_by_tmpl[w][j] > key)
            {
                dist_by_tmpl[w][j + 1] = dist_by_tmpl[w][j];
                j--;
            }
            dist_by_tmpl[w][j + 1] = key;
        }
    }

    /* 3. Score = mean of two smallest finite distances. */
    uint32_t best_score = UINT32_MAX;
    uint8_t  best_word  = 0;
    for (uint8_t w = 0; w < WORD_COUNT; w++)
    {
        uint32_t d0 = dist_by_tmpl[w][0];
        uint32_t d1 = dist_by_tmpl[w][1];
        uint32_t score;
        if (d0 == UINT32_MAX)       score = UINT32_MAX;
        else if (d1 == UINT32_MAX)  score = d0;
        else                        score = (d0 + d1) >> 1;

        if (score < best_score)
        {
            best_score = score;
            best_word  = w;
        }
    }
    return best_word;
}

const char *word_label_from_index(uint8_t word_index)
{
    if (word_index >= WORD_COUNT)
        return "UNKNOWN";
    return WORD_LABELS[word_index];
}
