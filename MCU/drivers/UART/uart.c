#include "uart.h"
#include <avr/io.h>

#define F_CPU 11059200LU

void UART_init(long BAUD_RATE){
    //we need to work with 5 register 
    //first UBRR reg for settting disered BAUD rate
    uint16_t UBRR = ( F_CPU/(16*BAUD_RATE) ) - 1;

    UBRRL = (uint8_t)UBRR ; 
    UBRRH = (uint8_t)(UBRR >> 8);

    //enable RX and TX
    UCSRB |= (1<<RXEN);
    UCSRB |= (1<<TXEN);

    //8-bit data
    UCSRC |= (1<<URSEL) | (1<<UCSZ1) | (1<<UCSZ0);
}


/* Blocking getchar */
int UART_getChar(FILE *stream){
    while( (UCSRA & (1<<RXC))  == 0);
    return UDR;
}


/* Put char to UART */
int UART_putChar(char c , FILE *stream){
    while ( (UCSRA & (1<<UDRE)) ==0);
    UDR = c;
    return 0 ;
}


/* Non-blocking receive */
int UART_getChar_nonBlocking(void){
    if(UCSRA & (1<<RXC)){
        return UDR;
    }
    else{
        return -1 ;
    }
}


/* FILE stream used by printf / scanf */
static FILE uart_str = FDEV_SETUP_STREAM(UART_putChar, UART_getChar, _FDEV_SETUP_RW);


/* Redirect stdin and stdout */
void UART_stdio_init(void){
    stdout = &uart_str;
    stdin  = &uart_str;
}