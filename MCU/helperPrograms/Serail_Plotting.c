#define F_CPU 11059200LU
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "External_libraries/uart.h"
#include "External_libraries/goertzel.h"

#define SPEECH_STE_THRESHOLD 50u
#define FRICATIVE_GOERTZEL_THRESHOLD 2u   /* feature-scale units; raise if false triggers */
#define ZCE_THRESHOLD 15u
#define FRAME_SIZE 125U
#define RECORD_SAMPLES 8000U

volatile uint16_t adc_val = 0;
volatile uint8_t sample_ready = 0;

ISR(ADC_vect)
{
    adc_val = ADC;
    TIFR |= (1 << OCF1B);
    sample_ready = 1;
}

int main(void)
{
    UART_init(230400);
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

    uint32_t dc_ema = 256ul * 1024ul;

    // ── IDLE: STE + Goertzel VAD (ZCE removed from pre-recording) ────────────
    uint16_t state_frame = FRAME_SIZE;
    uint16_t state_frame_ste = 0;
    GoertzelState idle_goertzel;
    goertzel_reset(&idle_goertzel);

    uint8_t is_recording = 0;
    uint16_t remaining_samples = 0;

    // ── RECORDING: silence detection (STE + Goertzel) ────────────────────────
    uint16_t rec_frame_ste = 0;
    uint8_t rec_frame_zce = 0;
    uint8_t rec_last_sign = 0;
    uint8_t rec_first_sample = 1;
    uint8_t rec_frame_buf = 0;
    uint8_t rec_frame_count = 0;
    uint8_t consecutive_silent_frames = 0;
    GoertzelState rec_goertzel;
    goertzel_reset(&rec_goertzel);

    printf("START_READY\r\n");

    while (1)
    {
        if (!sample_ready)
            continue;

        sample_ready = 0;

        if (!is_recording)
            dc_ema += (uint32_t)adc_val, dc_ema -= (dc_ema >> 10);

        int16_t centered = (int16_t)adc_val - (int16_t)(dc_ema >> 10);

        if (!is_recording)
        {
            state_frame_ste += abs(centered);
            goertzel_update(&idle_goertzel, centered);

            state_frame--;

            if (state_frame == 0)
            {
                uint16_t avg_ste = (state_frame_ste >> 7);
                state_frame = FRAME_SIZE;
                state_frame_ste = 0;

                uint8_t goertzel_vad = 0;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    uint32_t pwr = goertzel_pseudo_magnitude(&idle_goertzel, b) >> GOERTZEL_SHIFTS[b];
                    if (pwr > 255u) pwr = 255u;
                    if ((uint8_t)pwr > goertzel_vad) goertzel_vad = (uint8_t)pwr;
                }
                goertzel_reset(&idle_goertzel);

                if (avg_ste > SPEECH_STE_THRESHOLD
                    || goertzel_vad > FRICATIVE_GOERTZEL_THRESHOLD)
                {
                    is_recording = 1;
                    remaining_samples = RECORD_SAMPLES;
                    rec_frame_ste = 0;
                    rec_frame_zce = 0;
                    rec_last_sign = 0;
                    rec_first_sample = 1;
                    rec_frame_buf = 0;
                    rec_frame_count = 0;
                    consecutive_silent_frames = 0;
                    goertzel_reset(&rec_goertzel);
                    printf("START\r\n");
                    uint16_t dc_bias_val = (uint16_t)(dc_ema >> 10);
                    UART_putChar((char)((dc_bias_val >> 8) & 0xFF), stdout);
                    UART_putChar((char)(dc_bias_val & 0xFF), stdout);
                }
            }
        }
        else
        {
            UART_putChar((char)((adc_val >> 8) & 0xFF), stdout);
            UART_putChar((char)(adc_val & 0xFF), stdout);

            rec_frame_ste += (uint16_t)abs(centered);
            uint8_t rec_sign = (centered > 0) ? 1U : 0U;
            if (rec_first_sample) { rec_first_sample = 0; rec_last_sign = rec_sign; }
            if (rec_sign != rec_last_sign) { rec_frame_zce++; rec_last_sign = rec_sign; }
            goertzel_update(&rec_goertzel, centered);
            rec_frame_buf++;

            uint8_t done = 0;
            if (rec_frame_buf == FRAME_SIZE)
            {
                rec_frame_buf = 0;
                rec_frame_count++;

                uint8_t ste_silent = ((rec_frame_ste >> 7) <= SPEECH_STE_THRESHOLD);
                uint8_t goertzel_silent = 1;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    if ((goertzel_pseudo_magnitude(&rec_goertzel, b) >> GOERTZEL_SHIFTS[b]) > FRICATIVE_GOERTZEL_THRESHOLD)
                    {
                        goertzel_silent = 0;
                        break;
                    }
                }
                goertzel_reset(&rec_goertzel);

                if (ste_silent && goertzel_silent)
                    consecutive_silent_frames++;
                else
                    consecutive_silent_frames = 0;

                rec_frame_ste = 0;
                rec_frame_zce = 0;

                if (rec_frame_count >= 10 && consecutive_silent_frames >= 12)
                    done = 1;
            }

            if (!done)
            {
                remaining_samples--;
                if (remaining_samples == 0)
                    done = 1;
            }

            if (done)
            {
                printf("END\r\n");
                is_recording = 0;
                state_frame = FRAME_SIZE;
                state_frame_ste = 0;
                goertzel_reset(&idle_goertzel);
                goertzel_reset(&rec_goertzel);
            }
        }
    }
}
