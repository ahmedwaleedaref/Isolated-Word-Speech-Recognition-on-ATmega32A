#include "word_classifier.h"

static uint32_t squared_diff_u8(uint8_t a, uint8_t b)
{
    int16_t diff = (int16_t)a - (int16_t)b;
    return (uint32_t)(diff * diff);
}

uint8_t classify_word(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN]
)
{
    uint32_t min_distance = UINT32_MAX;
    uint8_t best_word = 0;

    for (uint8_t word_idx = 0; word_idx < WORD_COUNT; word_idx++)
    {
        for (uint8_t template_idx = 0; template_idx < TEMPLATES_PER_WORD; template_idx++)
        {
            uint32_t distance = 0;

            for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++)
            {
                uint8_t tmpl = pgm_read_byte(
                    &WORD_TEMPLATES[word_idx][template_idx][i]);
                distance += squared_diff_u8(ste[i], tmpl);
            }

            for (uint8_t i = 0; i < ZCE_FEATURE_COUNT; i++)
            {
                uint8_t tmpl = pgm_read_byte(
                    &WORD_TEMPLATES[word_idx][template_idx][STE_FEATURE_COUNT + i]);
                distance += squared_diff_u8(zce[i], tmpl);
            }

            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
            {
                uint8_t offset = STE_FEATURE_COUNT + ZCE_FEATURE_COUNT
                               + b * GOERTZEL_FEATURE_COUNT_PER_BIN;
                for (uint8_t i = 0; i < GOERTZEL_FEATURE_COUNT_PER_BIN; i++)
                {
                    uint8_t tmpl = pgm_read_byte(
                        &WORD_TEMPLATES[word_idx][template_idx][offset + i]);
                    distance += squared_diff_u8(goertzel[b][i], tmpl);
                }
            }

            if (distance < min_distance)
            {
                min_distance = distance;
                best_word = word_idx;
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
