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

#define SPEECH_STE_THRESHOLD 100u   // max possible avg_ste ~511; 100 sits above silence (~87) and below speech
#define ZCE_THRESHOLD 30u           // high ZCE = unvoiced speech (/s/, /f/)

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
    uint32_t dc_ema = 256ul * 1024ul; // EMA of ADC values, init to mid-scale (512 << 10)

    // IDLE state — frame-level VAD
    unsigned int state_frame = 125;
    uint16_t state_frame_ste = 0;
    uint8_t state_frame_zce = 0; // now actually used
    unsigned char state_last_sign = 0;
    unsigned char state_first_sample = 1;

    // Feature extraction
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

    while (1)
    {
        if (!flag)
            continue;
        flag = 0;

        // Update DC EMA only during silence (IDLE), not while recording
        if (state == IDLE)
        {
            dc_ema += (uint32_t)adc_val;
            dc_ema -= (dc_ema >> 10);
        }

        centered = (int16_t)adc_val - (int16_t)(dc_ema >> 10);

        // ── IDLE: accumulate one 125-sample frame, check for speech ──────────
        if (state == IDLE)
        {
            state_frame_ste += abs(centered);

            // ZCE for the VAD frame
            unsigned char s = (centered > 0) ? 1 : 0;
            if (state_first_sample)
            {
                state_first_sample = 0;
                state_last_sign = s;
            }
            if (s != state_last_sign)
            {
                state_frame_zce++;
                state_last_sign = s;
            }

            state_frame--;

            if (state_frame == 0)
            {
                uint16_t avg_ste = (state_frame_ste >> 7);

                state_frame_ste = 0;
                state_frame_zce = 0;
                state_frame = 125;
                state_first_sample = 1;

                if (avg_ste > SPEECH_STE_THRESHOLD)
                {
                    // FIX 1: was == (comparison), must be = (assignment)
                    // FIX 5: reset all feature extraction state cleanly on entry
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

                    curr_125_ste += abs(centered);
                    unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                    if (curr_sample_sign != last_sample_sign)
                    {
                        curr_125_zce++;
                        last_sample_sign = curr_sample_sign;
                    }
                }
                else
                {
                    STE[buffer_index] = (uint8_t)((curr_125_ste + prev_125_ste) >> 8);
                    ZCE[buffer_index] = (curr_125_zce + prev_125_zce);

                    if ((curr_125_ste >> 7) <= SPEECH_STE_THRESHOLD)
                        consecutive_silent_frames++;
                    else
                        consecutive_silent_frames = 0;

                    buffer_index++;

                    prev_125_ste = curr_125_ste;
                    prev_125_zce = curr_125_zce;
                    curr_125_ste = 0;
                    curr_125_zce = 0;
                    Buffer_size = 125;

                    curr_125_ste += abs(centered);
                    unsigned char curr_sample_sign = (centered > 0) ? 1 : 0;
                    if (curr_sample_sign != last_sample_sign)
                    {
                        curr_125_zce++;
                        last_sample_sign = curr_sample_sign;
                    }

                    if (buffer_index >= 10 && consecutive_silent_frames >= 8)
                        state = DONE;
                }
            }

            number_of_sample--;

            if (number_of_sample == 0)
            {
                state = DONE; // FIX 4: was just resetting, never reaching DONE
            }
        }

        // ── DONE: classify, then return to IDLE ───────────────────────────────
        if (state == DONE)
        {
            // Normalize STE by its peak so the shape (not loudness) drives classification
            uint8_t ste_peak = 0;
            for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++)
            {
                if (STE[i] > ste_peak) ste_peak = STE[i];
            }
            if (ste_peak > 0)
            {
                for (uint8_t i = 0; i < STE_FEATURE_COUNT; i++)
                {
                    STE[i] = (uint8_t)(((uint16_t)STE[i] * 255u) / ste_peak);
                }
            }

            LCD_String_xy(0, 0, "Classifying...  ");

            
            uint8_t predicted_word = classify_word_from_ste_zce(STE, ZCE);
            const char *predicted_label = word_label_from_index(predicted_word);

            LCD_String_xy(0, 0, "Detected:       ");
            LCD_String_xy(1, 0, "                ");
            LCD_String_xy(1, 0, predicted_label);
            
            
            
            /*
            printf("sample here \n\r");
            for(unsigned char i = 0 ; i < 31 ; i++){
                printf("%1u,%1u\r\n" , STE[i] , ZCE[i]);
            }
            */
            


            state = IDLE;

            // Reset VAD frame for clean IDLE restart
            state_frame = 125;
            state_frame_ste = 0;
            state_frame_zce = 0;
            state_first_sample = 1;
        }
    }

    return 0;
}