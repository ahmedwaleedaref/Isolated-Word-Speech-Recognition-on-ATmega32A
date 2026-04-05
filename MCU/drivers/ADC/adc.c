#include "adc.h"

void ADC_CONFIG_ADC3_TIMER1_B_MATCH(){
    ADMUX = (1 << MUX1) | (1 << MUX0); // ADC3

    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS1);

    // Auto trigger source
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |= (1 << ADTS2) | (1 << ADTS0); // your working setup

}