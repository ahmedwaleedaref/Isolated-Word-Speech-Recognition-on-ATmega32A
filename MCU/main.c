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
#include "External_libraries/goertzel.h"
#include "External_libraries/word_classifier.h"

/* STE VAD: mean |sample| over 128-sample block */
#define SPEECH_STE_THRESHOLD 70u

/* ZCE VAD: dead-zone crossings per 128-sample block.
 * Dead zone = 3 * EMA(|centered|) during IDLE, adapts like dc_ema. */
#define ZCE_VAD_THRESHOLD 15u

/* RECORDING end-of-speech Goertzel silence check (feature-scale units) */
#define FRICATIVE_GOERTZEL_THRESHOLD 2u
#define MAX_RECORD_SAMPLES 7000u
#define MAX_RECORD_BLOCKS  (MAX_RECORD_SAMPLES / 128u) /* 54 blocks = 6912 samples */

typedef enum { IDLE, RECORDING, DONE } State;

/* ── Ping-pong buffers: written by ISR, read by main ─────────────────────── */
volatile int16_t adc_buf[2][128];
volatile uint8_t buffer_ready = 0;  /* 1 = adc_buf[0] ready, 2 = adc_buf[1] ready */

volatile State state = IDLE;

/* ─────────────────────────────────────────────────────────────────────────── */
ISR(ADC_vect)
{
    static uint32_t dc_ema   = 256ul * 1024ul;
    static uint8_t  buf_idx  = 0;
    static uint8_t  samp_idx = 0;

    uint16_t raw = ADC;
    TIFR |= (1 << OCF1B);   /* clear flag so next Compare B match generates a new edge */
    dc_ema += raw;
    dc_ema -= (dc_ema >> 10);

    adc_buf[buf_idx][samp_idx] = (int16_t)raw - (int16_t)(dc_ema >> 10);

    if (++samp_idx == 128) {
        samp_idx     = 0;
        buffer_ready = buf_idx + 1;   /* signal which buffer is complete */
        buf_idx     ^= 1;             /* switch to the other buffer */
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UART_init(9600);
    UART_stdio_init();

    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11);
    OCR1A  = 172;
    OCR1B  = 172;

    ADMUX  = (1 << MUX1) | (1 << MUX0);
    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1);
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |=  (1 << ADTS2) | (1 << ADTS0);

    sei();
    LCD_Init();
    LCD_Gotoxy(0, 0);
    sei();

    // ── IDLE VAD state ────────────────────────────────────────────────────────
    uint32_t noise_floor_ema = 3ul * 1024ul;
    uint8_t  vad_sign        = 0;

    // ── RECORDING state ───────────────────────────────────────────────────────
    uint32_t prev_ste_sum                    = 0;
    uint8_t  prev_zce                        = 0;
    uint16_t prev_goertzel[GOERTZEL_NUM_BINS] = {0};
    uint8_t  block_count                     = 0;
    uint8_t  first_block                     = 1;
    uint8_t  consec_silent                   = 0;
    uint8_t  recorded_blocks                 = 0;

    // ── Feature arrays ────────────────────────────────────────────────────────
    uint8_t STE[STE_FEATURE_COUNT]                              = {0};
    uint8_t ZCE[ZCE_FEATURE_COUNT]                              = {0};
    uint8_t G[GOERTZEL_NUM_BINS][GOERTZEL_FEATURE_COUNT_PER_BIN] = {{0}};

    while (1)
    {
        if (!buffer_ready) continue;

        uint8_t ready = buffer_ready;
        buffer_ready  = 0;
        const volatile int16_t *buf = adc_buf[ready - 1];

        // ── IDLE: block-level VAD ─────────────────────────────────────────────
        if (state == IDLE)
        {
            uint32_t sum_abs = 0;
            uint8_t  zce_cnt = 0;
            int16_t  dz      = (int16_t)((noise_floor_ema * 3ul) >> 10);

            for (uint8_t k = 0; k < 128; k++)
            {
                int16_t  s = buf[k];
                uint16_t a = (s < 0) ? (uint16_t)(-s) : (uint16_t)s;
                sum_abs += a;

                noise_floor_ema += a;
                noise_floor_ema -= (noise_floor_ema >> 10);

                if      (s >  dz && vad_sign == 0) { vad_sign = 1; zce_cnt++; }
                else if (s < -dz && vad_sign == 1) { vad_sign = 0; zce_cnt++; }
            }

            uint8_t avg_ste = (uint8_t)(sum_abs >> 7);   /* sum / 128 */
            if (avg_ste > SPEECH_STE_THRESHOLD || zce_cnt > ZCE_VAD_THRESHOLD)
            {
                state         = RECORDING;
                block_count   = 0;
                first_block   = 1;
                consec_silent = 0;
                recorded_blocks = 0;
                vad_sign      = 0;

                for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++) { STE[i] = 0; ZCE[i] = 0; }
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++) {
                    prev_goertzel[b] = 0;
                    for (uint8_t i = 0; i < GOERTZEL_FEATURE_COUNT_PER_BIN; i++)
                        G[b][i] = 0;
                }
                LCD_String_xy(0, 0, "Recording...    ");
                /* Fall through: this trigger buffer is F0 for RECORDING processing. */
            }
        }

        // ── RECORDING: block feature extraction + overlap ─────────────────────
        if (state == RECORDING)
        {
            /* 1. Compute current block: STE sum, ZCE count, Goertzel pseudo-magnitudes. */
            uint32_t    sum_abs  = 0;
            uint8_t     zce_cnt  = 0;
            uint8_t     last_sgn = (buf[0] > 0) ? 1 : 0;
            GoertzelState gs;
            goertzel_reset(&gs);

            for (uint8_t k = 0; k < 128; k++)
            {
                int16_t  s = buf[k];
                uint16_t a = (s < 0) ? (uint16_t)(-s) : (uint16_t)s;
                sum_abs += a;

                uint8_t sgn = (s > 0) ? 1 : 0;
                if (sgn != last_sgn) { zce_cnt++; last_sgn = sgn; }

                goertzel_update(&gs, s);
            }

            uint16_t curr_goertzel[GOERTZEL_NUM_BINS];
            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                curr_goertzel[b] = goertzel_pseudo_magnitude(&gs, b);

            if (first_block)
            {
                /* F0 (the trigger frame): direct STE/ZCE/Goertzel are sum_abs/zce_cnt/curr_goertzel.
                 * Cache F0 only; first written feature must be overlap(F0, F1). */
                prev_ste_sum = sum_abs;
                prev_zce     = zce_cnt;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    prev_goertzel[b] = curr_goertzel[b];
                first_block = 0;
            }
            else
            {
                /* 2. Overlap-combine previous and current frames:
                 *    feature[0]=overlap(F0,F1), feature[1]=overlap(F1,F2), ... */
                uint32_t ste_ov = (sum_abs + prev_ste_sum) >> 8;
                STE[block_count] = (ste_ov > 255u) ? 255u : (uint8_t)ste_ov;
                ZCE[block_count] = zce_cnt + prev_zce;

                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    uint32_t ov = ((uint32_t)curr_goertzel[b] + (uint32_t)prev_goertzel[b]) >> GOERTZEL_SHIFTS[b];
                    G[b][block_count] = (ov > 255u) ? 255u : (uint8_t)ov;
                }

                /* 3. Silence check for early stop. */
                uint8_t ste_silent = ((sum_abs >> 7) <= SPEECH_STE_THRESHOLD);
                uint8_t g_silent   = 1;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    if ((curr_goertzel[b] >> GOERTZEL_SHIFTS[b]) > FRICATIVE_GOERTZEL_THRESHOLD)
                    {
                        g_silent = 0;
                        break;
                    }
                }
                if (ste_silent && g_silent) consec_silent++;
                else                        consec_silent = 0;

                /* 4. Advance sliding window. */
                prev_ste_sum = sum_abs;
                prev_zce     = zce_cnt;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    prev_goertzel[b] = curr_goertzel[b];

                block_count++;

                if ((block_count >= 10 && consec_silent >= 12) ||
                     block_count >= STE_FEATURE_COUNT)
                    state = DONE;
            }

            recorded_blocks++;
            if (recorded_blocks >= MAX_RECORD_BLOCKS)
                state = DONE;
        }

        // ── DONE: normalize, classify, display ────────────────────────────────
        if (state == DONE)
        {
            uint8_t ste_peak = 0;
            for (uint8_t i = 0; i < block_count; i++)
                if (STE[i] > ste_peak) ste_peak = STE[i];
            if (ste_peak > 0)
                for (uint8_t i = 0; i < block_count; i++)
                    STE[i] = (uint8_t)(((uint16_t)STE[i] * 255u) / ste_peak);

            uint8_t g_peak = 0;
            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                for (uint8_t i = 0; i < block_count; i++)
                    if (G[b][i] > g_peak) g_peak = G[b][i];
            if (g_peak > 0)
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                    for (uint8_t i = 0; i < block_count; i++)
                        G[b][i] = (uint8_t)(((uint16_t)G[b][i] * 255u) / g_peak);

            LCD_String_xy(0, 0, "Classifying...  ");

            uint8_t predicted = classify_word(STE, ZCE, G, block_count);
            LCD_String_xy(0, 0, "Detected:       ");
            LCD_String_xy(1, 0, "                ");
            LCD_String_xy(1, 0, word_label_from_index(predicted));

            /* ── UART feature dump ─────────────────────────────────────────── */
            /*
            static const uint16_t GOERTZEL_FREQS[GOERTZEL_NUM_BINS] = {350, 900, 1700, 2700, 3500};

            printf("STE\r\n");
            if (block_count > 0) {
                printf("%u", STE[0]);
                for (uint8_t i = 1; i < block_count; i++) printf(",%u", STE[i]);
            } else {
                printf("0");
            }
            printf("\r\n");

            printf("ZCE\r\n");
            if (block_count > 0) {
                printf("%u", ZCE[0]);
                for (uint8_t i = 1; i < block_count; i++) printf(",%u", ZCE[i]);
            } else {
                printf("0");
            }
            printf("\r\n");

            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
            {
                printf("G%u\r\n", GOERTZEL_FREQS[b]);
                if (block_count > 0) {
                    printf("%u", G[b][0]);
                    for (uint8_t i = 1; i < block_count; i++)
                        printf(",%u", G[b][i]);
                } else {
                    printf("0");
                }
                printf("\r\n");
            }
            /* ─────────────────────────────────────────────────────────────── */

            state    = IDLE;
            vad_sign = 0;
            block_count = 0;
            recorded_blocks = 0;
        }
    }

    return 0;
}
