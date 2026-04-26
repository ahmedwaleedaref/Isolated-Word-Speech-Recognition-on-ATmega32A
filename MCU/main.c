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

#define SPEECH_STE_THRESHOLD  0.3f   // tune this per your environment
#define ZCE_THRESHOLD         0.4f   // high ZCE = unvoiced speech (/s/, /f/)

typedef enum
{
    IDLE,
    RECORDING,
    DONE
} State;

volatile State state = IDLE;

volatile int adc_val = 0;
volatile unsigned char flag = 0;
char msg[17];

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

    float analog_val = 0.0f;
    float centered   = 0.0f;

    // IDLE state — frame-level VAD
    unsigned int  state_frame     = 125;
    float         state_frame_ste = 0.0f;
    float         state_frame_zce = 0.0f;   // now actually used
    unsigned char state_last_sign = 0;
    unsigned char state_first_sample = 1;

    // Feature extraction
    unsigned int  number_of_sample = 8000;
    unsigned int  Buffer_size      = 125;
    unsigned int  buffer_index     = 0;

    float         curr_125_ste     = 0.0f;
    float         prev_125_ste     = 0.0f;
    unsigned int  curr_125_zce     = 0;
    unsigned int  prev_125_zce     = 0;
    unsigned char first_125_window = 1;

    float         STE[64]          = {0};
    float         ZCE[64]          = {0};
    unsigned char last_sample_sign = 0;
    unsigned char first_sample     = 1;

    while (1)
    {
        if (!flag) continue;
        flag = 0;


        /*
        int16_t centered_signal = (int16_t)adc_val - 256;
        analog_val = (adc_val / 1024.0f) * 5.0f;
        centered   = analog_val - 1.25f;
        
        */
        

        int16_t centered_signal = (int16_t)adc_val - 256;
        //normalize to [1 , -1]
        centered = (centered_signal / 256.0f);

        // ── IDLE: accumulate one 125-sample frame, check for speech ──────────
        if (state == IDLE)
        {
            state_frame_ste += centered * centered;

            // ZCE for the VAD frame
            unsigned char s = (centered > 0) ? 1 : 0;
            if (state_first_sample)
            {
                state_first_sample = 0;
                state_last_sign    = s;
            }
            if (s != state_last_sign)
            {
                state_frame_zce++;
                state_last_sign = s;
            }

            state_frame--;

            if (state_frame == 0)
            {
                float avg_ste = state_frame_ste / 125.0f;
                float avg_zce = state_frame_zce / 125.0f;

                // FIX 2+3: reset both accumulators, use ZCE as second condition
                state_frame_ste    = 0.0f;
                state_frame_zce    = 0.0f;
                state_frame        = 125;
                state_first_sample = 1;

                // Speech detected if STE is high OR ZCE is high (catches unvoiced)
                if (avg_ste > SPEECH_STE_THRESHOLD || avg_zce > ZCE_THRESHOLD)
                {
                    // FIX 1: was == (comparison), must be = (assignment)
                    // FIX 5: reset all feature extraction state cleanly on entry
                    state              = RECORDING;
                    number_of_sample   = 8000;
                    Buffer_size        = 125;
                    buffer_index       = 0;
                    curr_125_ste       = 0.0f;
                    prev_125_ste       = 0.0f;
                    curr_125_zce       = 0;
                    prev_125_zce       = 0;
                    first_125_window   = 1;
                    first_sample       = 1;
                    last_sample_sign   = 0;

                    LCD_String_xy(0, 0, "Recording...    ");
                }
            }
        }

        // ── RECORDING: feature extraction (your fixed logic, unchanged) ───────
        if (state == RECORDING)
        {
            Buffer_size--;

            if (Buffer_size > 0)
            {
                curr_125_ste += centered * centered;

                unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                if (first_sample)
                {
                    first_sample     = 0;
                    last_sample_sign = curr_sample_sign;
                }
                if (curr_sample_sign != last_sample_sign)
                {
                    curr_125_zce++;
                    last_sample_sign = curr_sample_sign;
                }
            }
            else
            {
                if (first_125_window)
                {
                    prev_125_ste     = curr_125_ste;
                    prev_125_zce     = curr_125_zce;
                    curr_125_ste     = 0.0f;
                    curr_125_zce     = 0;
                    Buffer_size      = 125;
                    first_125_window = 0;

                    curr_125_ste += centered * centered;
                    unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                    if (curr_sample_sign != last_sample_sign)
                    {
                        curr_125_zce++;
                        last_sample_sign = curr_sample_sign;
                    }

                    if (buffer_index > 0)
                    {
                        sprintf(msg, "E:%1.3f", STE[buffer_index - 1]);
                        LCD_String_xy(0, 0, msg);
                    }
                }
                else
                {
                    STE[buffer_index] = (curr_125_ste + prev_125_ste) / 250.0f;
                    ZCE[buffer_index] = ((float)(curr_125_zce + prev_125_zce)) / 250.0f;
                    buffer_index++;

                    prev_125_ste = curr_125_ste;
                    prev_125_zce = curr_125_zce;
                    curr_125_ste = 0.0f;
                    curr_125_zce = 0;
                    Buffer_size  = 125;

                    curr_125_ste += centered * centered;
                    unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                    if (curr_sample_sign != last_sample_sign)
                    {
                        curr_125_zce++;
                        last_sample_sign = curr_sample_sign;
                    }
                }
            }

            number_of_sample--;

            if (number_of_sample == 0)
            {
                state = DONE;   // FIX 4: was just resetting, never reaching DONE
            }
        }

        // ── DONE: classify, then return to IDLE ───────────────────────────────
        if (state == DONE)
        {
            // STE[] and ZCE[] are valid here — classify
            LCD_String_xy(0, 0, "Classifying...  ");
            printf("NEW sample");
            for (unsigned char i = 0; i < 64; i++)
            {
                printf("%1.6f,%1.6f\r\n", STE[i], ZCE[i]);
            }

            state = IDLE;

            // Reset VAD frame for clean IDLE restart
            state_frame        = 125;
            state_frame_ste    = 0.0f;
            state_frame_zce    = 0.0f;
            state_first_sample = 1;
        }
    }

    return 0;
}