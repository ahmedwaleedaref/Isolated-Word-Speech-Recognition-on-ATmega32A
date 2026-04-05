#include "timer.h"

void TIMER1_CONFIG_CTC_8HZ_Square_wave(){
    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11); // CTC, prescaler 8

    OCR1A = 172;
    OCR1B = 172;
}