#ifndef EXT_INTERRUPT_H
#define EXT_INTERRUPT_H

#include <avr/io.h>
#include <stdint.h>

/* Interrupt trigger modes */
#define LOW_LEVEL      0
#define ANY_CHANGE     1
#define FALLING_EDGE   2
#define RISING_EDGE    3

void EXT_INT0_init(uint8_t mode);
void EXT_INT1_init(uint8_t mode);
void EXT_INT2_init(uint8_t mode);

#endif