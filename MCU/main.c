#define F_CPU 11059200LU

#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include "External_libraries/my_lcd.h"
#include "External_libraries/ext_interrupt.h"
#include "External_libraries/uart.h"
#include "External_libraries/word_classifier.h"

/* STE-based VAD: triggers on voiced speech, plosive bursts, nasals */
#define SPEECH_STE_THRESHOLD 100u

/* Goertzel VAD: triggers on voiceless fricatives (/s/ /f/) that have low STE.
 * Scaling: goertzel_power(frame) >> 20.  With amplitude ~20 at 3328 Hz → value ~1.
 * Broadband noise (random) gives 0 after >>20 because Goertzel is a narrow-band filter.
 * Raise this value if you get false triggers in noisy environments. */
#define FRICATIVE_GOERTZEL_THRESHOLD 2u

/* Early-stop silence guard: 8 consecutive silent frames after minimum 10 recorded */
/* (defined implicitly in RECORDING logic below) */

typedef enum
{
    IDLE,
    RECORDING,
    DONE
} State;

volatile State state = IDLE;

volatile int adc_val = 0;
volatile unsigned char flag = 0;

ISR(ADC_vect)
{
    adc_val = ADC;
    TIFR |= (1 << OCF1B);
    flag = 1;
}

int main(void)
{
    UART_init(9600);
    UART_stdio_init();

    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11);
    OCR1A = 172;
    OCR1B = 172;

    ADMUX = (1 << MUX1) | (1 << MUX0);
    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS1);
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |= (1 << ADTS2) | (1 << ADTS0);

    sei();

    LCD_Init();
    LCD_Gotoxy(0, 0);
    sei();

    int16_t centered = 0;
    uint32_t dc_ema = 256ul * 1024ul;

    // ── IDLE state: STE + Goertzel VAD ────────────────────────────────────────
    unsigned int state_frame = 125;
    uint16_t state_frame_ste = 0;
    // ZCE removed from IDLE — computed only during RECORDING

    // ── RECORDING: feature extraction ─────────────────────────────────────────
    unsigned int number_of_sample = 8000;
    unsigned int Buffer_size = 125;
    unsigned int buffer_index = 0;

    uint16_t curr_125_ste = 0;
    uint16_t prev_125_ste = 0;
    uint8_t curr_125_zce = 0;
    uint8_t prev_125_zce = 0;
    unsigned char first_125_window = 1;

    uint8_t STE[31] = {0};
    uint8_t ZCE[31] = {0};
    unsigned char last_sample_sign = 0;
    unsigned char first_sample = 1;
    uint8_t consecutive_silent_frames = 0;

    // ── Goertzel: shared between IDLE (VAD) and RECORDING (features) ──────────
    GoertzelState curr_goertzel;
    uint32_t prev_goertzel_power[GOERTZEL_NUM_BINS];
    uint8_t G[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN];
    goertzel_reset(&curr_goertzel);
    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++) prev_goertzel_power[b] = 0;

    while (1)
    {
        if (!flag)
            continue;
        flag = 0;

        if (state == IDLE)
            dc_ema += (uint32_t)adc_val, dc_ema -= (dc_ema >> 10);

        centered = (int16_t)adc_val - (int16_t)(dc_ema >> 10);

        // ── IDLE: STE + Goertzel per-sample accumulation ──────────────────────
        if (state == IDLE)
        {
            state_frame_ste += abs(centered);
            goertzel_update(&curr_goertzel, centered);

            state_frame--;

            if (state_frame == 0)
            {
                uint16_t avg_ste = (state_frame_ste >> 7);
                state_frame_ste = 0;
                state_frame = 125;

                // Max Goertzel power across all bins (>>20 scale for single-frame VAD)
                uint8_t goertzel_vad = 0;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    uint32_t pwr = goertzel_power(&curr_goertzel, b) >> 20;
                    if (pwr > 255u) pwr = 255u;
                    if ((uint8_t)pwr > goertzel_vad) goertzel_vad = (uint8_t)pwr;
                }
                goertzel_reset(&curr_goertzel);

                if (avg_ste > SPEECH_STE_THRESHOLD
                    || goertzel_vad > FRICATIVE_GOERTZEL_THRESHOLD)
                {
                    state = RECORDING;
                    number_of_sample = 4000;
                    Buffer_size = 125;
                    buffer_index = 0;
                    curr_125_ste = 0;
                    prev_125_ste = 0;
                    curr_125_zce = 0;
                    prev_125_zce = 0;
                    first_125_window = 1;
                    first_sample = 1;
                    last_sample_sign = 0;
                    consecutive_silent_frames = 0;
                    for (uint8_t idx = 0; idx < 31; idx++) { STE[idx] = 0; ZCE[idx] = 0; }

                    goertzel_reset(&curr_goertzel);
                    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    {
                        prev_goertzel_power[b] = 0;
                        for (uint8_t i = 0; i < GOERTZEL_FEATURE_COUNT_PER_BIN; i++)
                            G[b][i] = 0;
                    }

                    LCD_String_xy(0, 0, "Recording...    ");
                }
            }
        }

        // ── RECORDING: STE + ZCE + Goertzel feature extraction ───────────────
        if (state == RECORDING)
        {
            Buffer_size--;

            if (Buffer_size > 0)
            {
                curr_125_ste += abs(centered);

                unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                if (first_sample)
                {
                    first_sample = 0;
                    last_sample_sign = curr_sample_sign;
                }
                if (curr_sample_sign != last_sample_sign)
                {
                    curr_125_zce++;
                    last_sample_sign = curr_sample_sign;
                }

                goertzel_update(&curr_goertzel, centered);
            }
            else
            {
                if (first_125_window)
                {
                    prev_125_ste = curr_125_ste;
                    prev_125_zce = curr_125_zce;
                    curr_125_ste = 0;
                    curr_125_zce = 0;
                    Buffer_size = 125;
                    first_125_window = 0;

                    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                        prev_goertzel_power[b] = goertzel_power(&curr_goertzel, b);
                    goertzel_reset(&curr_goertzel);

                    curr_125_ste += abs(centered);
                    last_sample_sign = (centered > 0) ? 1 : 0;
                    goertzel_update(&curr_goertzel, centered);
                }
                else
                {
                    STE[buffer_index] = (uint8_t)((curr_125_ste + prev_125_ste) >> 8);
                    ZCE[buffer_index] = (curr_125_zce + prev_125_zce);

                    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    {
                        uint32_t curr_power = goertzel_power(&curr_goertzel, b);
                        uint32_t overlap = (curr_power + prev_goertzel_power[b]) >> 22;
                        G[b][buffer_index] = (overlap > 255u) ? 255u : (uint8_t)overlap;
                        prev_goertzel_power[b] = curr_power;
                    }

                    uint8_t ste_silent = ((curr_125_ste >> 7) <= SPEECH_STE_THRESHOLD);
                    uint8_t goertzel_silent = 1;
                    for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    {
                        if ((prev_goertzel_power[b] >> 20) > FRICATIVE_GOERTZEL_THRESHOLD)
                        {
                            goertzel_silent = 0;
                            break;
                        }
                    }
                    if (ste_silent && goertzel_silent)
                        consecutive_silent_frames++;
                    else
                        consecutive_silent_frames = 0;

                    buffer_index++;

                    prev_125_ste = curr_125_ste;
                    prev_125_zce = curr_125_zce;
                    curr_125_ste = 0;
                    curr_125_zce = 0;
                    Buffer_size = 125;
                    goertzel_reset(&curr_goertzel);

                    curr_125_ste += abs(centered);
                    last_sample_sign = (centered > 0) ? 1 : 0;
                    goertzel_update(&curr_goertzel, centered);

                    if (buffer_index >= 10 && consecutive_silent_frames >= 8)
                        state = DONE;
                }
            }

            number_of_sample--;
            if (number_of_sample == 0)
                state = DONE;
        }

        // ── DONE: normalize, classify, return to IDLE ─────────────────────────
        if (state == DONE)
        {
            uint8_t ste_peak = 0;
            for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++)
                if (STE[i] > ste_peak) ste_peak = STE[i];
            if (ste_peak > 0)
                for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++)
                    STE[i] = (uint8_t)(((uint16_t)STE[i] * 255u) / ste_peak);

            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
            {
                uint8_t g_peak = 0;
                for (uint8_t i = 0; i < GOERTZEL_FEATURE_COUNT_PER_BIN; i++)
                    if (G[b][i] > g_peak) g_peak = G[b][i];
                if (g_peak > 0)
                    for (uint8_t i = 0; i < GOERTZEL_FEATURE_COUNT_PER_BIN; i++)
                        G[b][i] = (uint8_t)(((uint16_t)G[b][i] * 255u) / g_peak);
            }

            LCD_String_xy(0, 0, "Classifying...  ");

            uint8_t predicted_word = classify_word_from_ste_zce_goertzel(STE, ZCE, G);
            const char *predicted_label = word_label_from_index(predicted_word);

            LCD_String_xy(0, 0, "Detected:       ");
            LCD_String_xy(1, 0, "                ");
            LCD_String_xy(1, 0, predicted_label);

            state = IDLE;

            state_frame = 125;
            state_frame_ste = 0;
            goertzel_reset(&curr_goertzel);
        }
    }

    return 0;
}
