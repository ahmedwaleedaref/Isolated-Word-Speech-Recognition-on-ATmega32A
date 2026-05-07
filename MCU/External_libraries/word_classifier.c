#include "word_classifier.h"

static uint32_t squared_diff_u8(uint8_t a, uint8_t b)
{
    int16_t diff = (int16_t)a - (int16_t)b;
    return (uint32_t)(diff * diff);
}

uint8_t classify_word_from_features(const uint8_t features[FEATURE_COUNT])
{
    uint32_t min_distance = UINT32_MAX;
    uint8_t best_word = 0;

    for (uint8_t word_idx = 0; word_idx < WORD_COUNT; word_idx++)
    {
        for (uint8_t template_idx = 0; template_idx < TEMPLATES_PER_WORD; template_idx++)
        {
            uint32_t distance = 0;

            for (uint8_t feature_idx = 0; feature_idx < FEATURE_COUNT; feature_idx++)
            {
                uint32_t feature_distance = squared_diff_u8(
                    features[feature_idx],
                    WORD_TEMPLATES[word_idx][template_idx][feature_idx]
                );
                if (feature_idx >= STE_FEATURE_COUNT)
                {
                    feature_distance *= 3u;
                }
                distance += feature_distance;
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

uint8_t classify_word_from_ste_zce(
    const uint8_t *ste,
    const uint8_t *zce
)
{
    return classify_word_from_ste_zce_count(
        ste, zce, STE_FEATURE_COUNT, ZCE_FEATURE_COUNT
    );
}

uint8_t classify_word_from_ste_zce_count(
    const uint8_t *ste,
    const uint8_t *zce,
    uint8_t ste_count,
    uint8_t zce_count
)
{
    if (ste_count > STE_FEATURE_COUNT)
    {
        ste_count = STE_FEATURE_COUNT;
    }
    if (zce_count > ZCE_FEATURE_COUNT)
    {
        zce_count = ZCE_FEATURE_COUNT;
    }

    if (ste_count == 0 && zce_count == 0)
    {
        return WORD_COUNT;
    }

    uint32_t min_distance = UINT32_MAX;
    uint8_t best_word = 0;

    for (uint8_t word_idx = 0; word_idx < WORD_COUNT; word_idx++)
    {
        for (uint8_t template_idx = 0; template_idx < TEMPLATES_PER_WORD; template_idx++)
        {
            uint32_t distance = 0;

            for (uint8_t feature_idx = 0; feature_idx < ste_count; feature_idx++)
            {
                distance += squared_diff_u8(
                    ste[feature_idx],
                    WORD_TEMPLATES[word_idx][template_idx][feature_idx]
                );
            }

            for (uint8_t feature_idx = 0; feature_idx < zce_count; feature_idx++)
            {
                distance +=  squared_diff_u8(
                    zce[feature_idx],
                    WORD_TEMPLATES[word_idx][template_idx][STE_FEATURE_COUNT + feature_idx]
                );
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
    {
        return "UNKNOWN";
    }
    return WORD_LABELS[word_index];
}
