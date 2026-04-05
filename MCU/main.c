#define F_CPU 11059200LU

#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include "ext_interrupt.h"
#include "my_lcd.h"

// ================== GLOBAL VARIABLES ==================

volatile int adc_val = 0;
volatile char flag = 0;

volatile unsigned char button_pressed = 0;

char msg[17];

// DSP variables
float signal_energy = 0.0;
unsigned int zero_crossing_count = 0;
unsigned char last_sample_sign = 1;
unsigned int sample_count = 0;

//===================model parameters=================
const float E_START = 0.475;
const float Z_START = 0.08;

const float E_ON = 0.54;
const float Z_ON = 0.042;

// ================== STATE MACHINE ==================

typedef enum {
    IDLE,
    RECORDING,
    DONE
} State;

volatile State state = IDLE;

// ================== ADC ISR ==================

ISR(ADC_vect)
{
    adc_val = ADC;  

    // IMPORTANT: keep your working behavior
    TIFR |= (1 << OCF1B);  

    flag = 1;
}

// ================== BUTTON ISR ==================

ISR(INT0_vect)
{
    button_pressed = 1;   // just set flag (NO delay here!)
}

//===================classify================
char* classify(float E, float Z)
{
    float d_start = (E - E_START)*(E - E_START) + 
                    10*(Z - Z_START)*(Z - Z_START);

    float d_on = (E - E_ON)*(E - E_ON) + 
                 10*(Z - Z_ON)*(Z - Z_ON);

    if (d_start < d_on)
        return "START";
    else
        return "ON";
}

// ================== MAIN ==================

int main(void)
{
    // -------- TIMER1 CONFIG --------
    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11); // CTC, prescaler 8

    OCR1A = 172;
    OCR1B = 172;

    // -------- ADC CONFIG --------
    ADMUX = (1 << MUX1) | (1 << MUX0); // ADC3

    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS1);

    // Auto trigger source
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |= (1 << ADTS2) | (1 << ADTS0); // your working setup

    // -------- BUTTON --------
    EXT_INT0_init(FALLING_EDGE);

    sei();

    // -------- LCD --------
    LCD_Init();
    LCD_Gotoxy(0,0);

    float analog_val = 0.0;
    float centered = 0.0;

    while (1)
    {
        // ===== BUTTON HANDLING =====
        if (button_pressed)
        {
            button_pressed = 0;

            // Reset system for new recording
            signal_energy = 0;
            zero_crossing_count = 0;
            sample_count = 0;
            last_sample_sign = 1;

            state = RECORDING;

            LCD_Clear();
            LCD_String_xy(0,0,"Recording...");
        }

        // ===== SAMPLE PROCESSING =====
        if (flag)
        {
            flag = 0;

            if (state == RECORDING)
            {
                // Convert to voltage
                analog_val = (adc_val / 1024.0) * 5.0;

                // Center around mic bias (MAX9814 ~1.25V)
                centered = analog_val - 1.25;

                // -------- STE --------
                signal_energy += centered * centered;

                // -------- ZCR with threshold --------
                float TH = 0.05;

                unsigned char current_sign;

                if (centered > TH) current_sign = 1;
                else if (centered < -TH) current_sign = 0;
                else current_sign = last_sample_sign;

                if (current_sign != last_sample_sign)
                {
                    zero_crossing_count++;
                }

                last_sample_sign = current_sign;

                // -------- COUNT --------
                sample_count++;

                if (sample_count >= 8000)
                {
                    state = DONE;
                }
            }
        }

        // ===== PROCESS RESULT =====
        if (state == DONE)
        {
            float E = signal_energy / 8000.0;
            float ZCR = zero_crossing_count / 8000.0;

            char* result = classify(E, ZCR);

            LCD_Clear();

            sprintf(msg, "E:%1.3f", E);
            LCD_String_xy(0, 0, msg);
            //sprintf(msg, "Z:%1.3f", ZCR);
            LCD_String_xy(1, 0, result);

            // Back to idle
            state = IDLE;
        }
    }

    return 0;
}