#define F_CPU 11059200LU
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "External_libraries/uart.h"

#define SPEECH_STE_THRESHOLD 100u
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

    uint32_t dc_ema = 512ul * 1024ul; // EMA of ADC values, init to mid-scale (512 << 10)
    uint16_t state_frame = FRAME_SIZE;
    uint16_t state_frame_ste = 0;
    uint8_t state_frame_zce = 0;
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

        // Update DC EMA only during silence (not recording)
        if (!is_recording)
        {
            dc_ema += (uint32_t)adc_val;
            dc_ema -= (dc_ema >> 10);
        }

        int16_t centered = (int16_t)adc_val - (int16_t)(dc_ema >> 10);

        if (!is_recording)
        {
            state_frame_ste += abs(centered);

            uint8_t current_sign = (centered > 0) ? 1U : 0U;
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
                uint16_t avg_ste = (state_frame_ste >> 7);
                uint8_t avg_zce = state_frame_zce;

                state_frame = FRAME_SIZE;
                state_frame_ste = 0;
                state_frame_zce = 0;
                state_first_sample = 1;

                if (avg_ste > SPEECH_STE_THRESHOLD || avg_zce > ZCE_THRESHOLD)
                {
                    is_recording = 1;
                    remaining_samples = RECORD_SAMPLES;
                    printf("START\r\n");
                    // Send dc_bias as 2 bytes (big-endian) so PC can center the samples
                    uint16_t dc_bias_val = (uint16_t)(dc_ema >> 10);
                    UART_putChar((char)((dc_bias_val >> 8) & 0xFF), stdout);
                    UART_putChar((char)(dc_bias_val & 0xFF), stdout);
                }
            }
        }
        else
        {
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
                state_frame_ste = 0;
                state_frame_zce = 0;
                state_first_sample = 1;
            }
        }
    }
}