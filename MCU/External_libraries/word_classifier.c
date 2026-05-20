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
    uint32_t min_distance = UINT32_MAX;
    uint8_t  best_word    = 0;

    for (uint8_t word_idx = 0; word_idx < WORD_COUNT; word_idx++)
    {

        for (uint8_t tmpl_idx = 0; tmpl_idx < TEMPLATES_PER_WORD; tmpl_idx++)
        {
            uint8_t tmpl_len = pgm_read_byte(&WORD_TEMPLATE_LENGTHS[word_idx][tmpl_idx]);
            const uint8_t *tmpl = &WORD_TEMPLATES[word_idx][tmpl_idx][0][0];
            uint32_t dist = dtw_distance(ste, zce, goertzel, query_len, tmpl, tmpl_len);

            if (dist < min_distance)
            {
                min_distance = dist;
                best_word    = word_idx;
            }
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
