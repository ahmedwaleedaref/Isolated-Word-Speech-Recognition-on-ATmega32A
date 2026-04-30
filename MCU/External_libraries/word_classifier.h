#ifndef WORD_CLASSIFIER_H
#define WORD_CLASSIFIER_H

#include <stdint.h>

#include "word_templates_data.h"

uint8_t classify_word_from_features(const uint8_t features[FEATURE_COUNT]);
uint8_t classify_word_from_ste_zce(
    const uint8_t *ste,
    const uint8_t *zce
);
uint8_t classify_word_from_ste_zce_count(
    const uint8_t *ste,
    const uint8_t *zce,
    uint8_t ste_count,
    uint8_t zce_count
);
const char *word_label_from_index(uint8_t word_index);

#endif
