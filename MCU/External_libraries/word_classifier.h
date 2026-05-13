#ifndef WORD_CLASSIFIER_H
#define WORD_CLASSIFIER_H

#include <stdint.h>

#include "word_templates_data.h"

uint8_t classify_word(
    const uint8_t ste[STE_FEATURE_COUNT],
    const uint8_t zce[ZCE_FEATURE_COUNT],
    const uint8_t goertzel[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN],
    uint8_t query_len
);

const char *word_label_from_index(uint8_t word_index);

#endif
