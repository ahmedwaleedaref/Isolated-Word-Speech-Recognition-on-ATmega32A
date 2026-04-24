#ifndef UART_H
#define UART_H

#include <stdio.h>
#include <avr/io.h>

/* UART API */
void UART_init(long BAUD_RATE);

int UART_getChar(FILE *stream);
int UART_putChar(char c , FILE *stream);

int UART_getChar_nonBlocking(void);

/* Redirect stdio to UART */
void UART_stdio_init(void);

#endif