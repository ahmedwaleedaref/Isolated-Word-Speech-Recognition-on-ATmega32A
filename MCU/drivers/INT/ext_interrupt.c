#include "ext_interrupt.h"

/*
INT0 is located on PD2
INT1 is located on PD3
INT2 is located on PB2
*/

void EXT_INT0_init(uint8_t mode)
{
    /* Configure PD2 as input */
    DDRD &= ~(1 << PD2);

    /* Configure pull-up based on mode */
    if(mode == RISING_EDGE)
        PORTD &= ~(1 << PD2);   // disable pull-up
    else
        PORTD |= (1 << PD2);    // enable pull-up

    /* Configure trigger mode */
    switch(mode)
    {
        case LOW_LEVEL:
            MCUCR &= ~(1<<ISC01);
            MCUCR &= ~(1<<ISC00);
            break;

        case ANY_CHANGE:
            MCUCR &= ~(1<<ISC01);
            MCUCR |=  (1<<ISC00);
            break;

        case FALLING_EDGE:
            MCUCR |=  (1<<ISC01);
            MCUCR &= ~(1<<ISC00);
            break;

        case RISING_EDGE:
            MCUCR |= (1<<ISC01) | (1<<ISC00);
            break;
    }

    GIFR |= (1 << INTF0);
    GICR |= (1 << INT0);
}


void EXT_INT1_init(uint8_t mode)
{
    /* Configure PD3 as input */
    DDRD &= ~(1 << PD3);

    /* Configure pull-up based on mode */
    if(mode == RISING_EDGE)
        PORTD &= ~(1 << PD3);
    else
        PORTD |= (1 << PD3);

    /* Configure trigger mode */
    switch(mode)
    {
        case LOW_LEVEL:
            MCUCR &= ~(1<<ISC11);
            MCUCR &= ~(1<<ISC10);
            break;

        case ANY_CHANGE:
            MCUCR &= ~(1<<ISC11);
            MCUCR |=  (1<<ISC10);
            break;

        case FALLING_EDGE:
            MCUCR |=  (1<<ISC11);
            MCUCR &= ~(1<<ISC10);
            break;

        case RISING_EDGE:
            MCUCR |= (1<<ISC11) | (1<<ISC10);
            break;
    }

    GIFR |= (1 << INTF1);
    GICR |= (1 << INT1);
}


void EXT_INT2_init(uint8_t mode)
{
    /* Configure PB2 as input */
    DDRB &= ~(1 << PB2);

    /* Configure pull-up based on mode */
    if(mode == RISING_EDGE)
        PORTB &= ~(1 << PB2);
    else
        PORTB |= (1 << PB2);

    /*
    INT2 ONLY supports:
    FALLING_EDGE → ISC2 = 0
    RISING_EDGE  → ISC2 = 1
    (controlled by MCUCSR, not MCUCR)
    */
    if(mode == RISING_EDGE)
        MCUCSR |= (1 << ISC2);
    else
        MCUCSR &= ~(1 << ISC2);

    /* Clear pending flag */
    GIFR |= (1 << INTF2);

    /* Enable INT2 */
    GICR |= (1 << INT2);
}