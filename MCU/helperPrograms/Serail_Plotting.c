#define F_CPU 11059200LU
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <inttypes.h>
#include "External_libraries/uart.h"

#define SPEECH_STE_THRESHOLD 0.3f
#define ZCE_THRESHOLD 0.4f
#define FRAME_SIZE 125U       // ~4000/63 ≈ 63 frames/sec at 4 kHz
#define RECORD_SAMPLES 8000U // 1 second at 4 kHz

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
    UART_init(230400); // Changed: 9600 → 115200
    UART_stdio_init();

    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11);
    OCR1A = 172; // Changed: 172 → 345  (4 kHz at F_CPU/8)
    OCR1B = 172; // Must match OCR1A for ADC trigger

    ADMUX = (1 << MUX1) | (1 << MUX0);
    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS1);
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |= (1 << ADTS2) | (1 << ADTS0);
    sei();

    uint16_t state_frame = FRAME_SIZE;
    float state_frame_ste = 0.0f;
    uint16_t state_frame_zce = 0;
    uint8_t state_last_sign = 0;
    uint8_t state_first_sample = 1;
    uint8_t is_recording = 0;
    uint16_t remaining_samples = 0;

    printf("START_READY\r\n");

    while (1)
    {
        if (!sample_ready)
            continue;

        sample_ready = 0;

        int16_t centered_signal = (int16_t)adc_val - 256;
        float centered = centered_signal / 256.0f;

        if (!is_recording)
        {
            // ----- NO printf here anymore -----
            state_frame_ste += centered * centered;

            uint8_t current_sign = (centered > 0.0f) ? 1U : 0U;
            if (state_first_sample)
            {
                state_first_sample = 0;
                state_last_sign = current_sign;
            }
            if (current_sign != state_last_sign)
            {
                state_frame_zce++;
                state_last_sign = current_sign;
            }

            state_frame--;

            if (state_frame == 0)
            {
                float avg_ste = state_frame_ste / (float)FRAME_SIZE;
                float avg_zce = (float)state_frame_zce / (float)FRAME_SIZE;

                // Safe to print — happens once every 63 samples (~15 ms)
                //printf("STE:%.4f ZCE:%.4f\r\n", avg_ste, avg_zce);

                state_frame = FRAME_SIZE;
                state_frame_ste = 0.0f;
                state_frame_zce = 0;
                state_first_sample = 1;

                if (avg_ste > SPEECH_STE_THRESHOLD || avg_zce > ZCE_THRESHOLD)
                {
                    is_recording = 1;
                    remaining_samples = RECORD_SAMPLES;
                    printf("START\r\n");
                }
            }
        }
        else
        {
            // WRONG — UART_putc does not exist in your library
            // UART_putc(hi);
            // UART_putc(lo);

            // CORRECT — matches your library signature
            uint8_t lo = adc_val & 0xFF;
            uint8_t hi = (adc_val >> 8) & 0xFF;
            UART_putChar((char)hi, stdout);
            UART_putChar((char)lo, stdout);

            remaining_samples--;
            if (remaining_samples == 0)
            {
                printf("END\r\n");
                is_recording = 0;
                state_frame = FRAME_SIZE;
                state_frame_ste = 0.0f;
                state_frame_zce = 0;
                state_first_sample = 1;
            }
        }
    }
}