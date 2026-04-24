#define F_CPU 11059200LU

#include <avr/io.h>
#include <util/delay.h>
#include "my_lcd.h"

void LCD_Command( unsigned char cmnd )
{
    LCD_Port = (LCD_Port & 0x0F) | (cmnd & 0xF0); /* sending upper nibble */
    LCD_Port &= ~ (1<<RS);        /* RS=0, command reg. */
    LCD_Port |= (1<<EN);          /* Enable pulse */
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);

    _delay_us(200);

    LCD_Port = (LCD_Port & 0x0F) | (cmnd << 4);  /* sending lower nibble */
    LCD_Port |= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);
    _delay_ms(2);
}


void LCD_Char( unsigned char data )
{
    LCD_Port = (LCD_Port & 0x0F) | (data & 0xF0); /* sending upper nibble */
    LCD_Port |= (1<<RS);        /* RS=1, data reg. */    
    
    LCD_Port|= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);

    _delay_us(200);

    LCD_Port = (LCD_Port & 0x0F) | (data << 4); /* sending lower nibble */
    LCD_Port |= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);
    _delay_ms(2);
}

void LCD_Init (void)            /* LCD Initialize function */
{
    LCD_Dir = 0xFF;            /* Make LCD port direction as o/p */
    _delay_ms(20);              /* LCD Power ON delay always >15ms */
    
    LCD_Command(0x02);        /* send for 4 bit initialization of LCD  */
    LCD_Command(0x28);        /* 2 line, 5*7 matrix in 4-bit mode */
    LCD_Command(0x0c);        /* Display on cursor off*/
    LCD_Command(0x06);        /* Increment cursor (shift cursor to right)*/
    LCD_Command(0x01);        /* Clear display screen*/
    _delay_ms(2); 
}



void LCD_String (char *str)        /* Send string to LCD function */
{
    int i;
    for(i=0;str[i]!=0;i++)        /* Send each char of string till the NULL */
    {
        LCD_Char (str[i]);
    }
}

void LCD_String_xy (char row, char pos, char *str)    /* Send string to LCD with xy position */
{
    if (row == 0 && pos<16)
    LCD_Command((pos & 0x0F)|0x80);    /* Command of first row and required position<16 */
    else if (row == 1 && pos<16)
    LCD_Command((pos & 0x0F)|0xC0);    /* Command of first row and required position<16 */
    LCD_String(str);        /* Call LCD string function */
}

void LCD_Clear()
{
    LCD_Command (0x01);        /* Clear display */
    _delay_ms(2);
    LCD_Command (0x80);        /* Cursor at home position */
}


void lcd_create_char(unsigned char address, unsigned char pattern[])
{   // to write to character graphic ran CGRAM send B'0100 0000 and increments by 8
    if(address < 64 || address > 128)  return;
    LCD_Command(address);
    LCD_String((char *)pattern);
    //_delay_ms(1);
}

void LCD_Gotoxy(char row, char pos)
{    
    if(row ==0 && pos <16 )
        LCD_Command((pos & 0x0F) | 0x80);
    else if( row ==1 && pos < 16)
        LCD_Command((pos & 0x0F) | 0xC0);
}

unsigned char LCD_Read_Char(unsigned char address )
{
    unsigned char value = 0 ; 
    LCD_Command(address);
    
    LCD_Dir = 0x0F;   
          
    LCD_Port |= (1<<RS);        /* RS = 1, data reg. */                 
    LCD_Port |= (1<<RW);        // RW = 1  for reading             
    LCD_Port |= (1<<EN); 
          
    _delay_us(20);     
    value = (LCD_Pin & 0xF0); /* reading upper nibble */               
    LCD_Port &= ~ (1<<EN);
    _delay_us(200);
        
    LCD_Port |= (1<<EN);
    _delay_us(20);
    value = value | ((LCD_Pin & 0xF0)>>4); /* reading lower nibble */ 
    LCD_Port &= ~ (1<<EN); 
    
    LCD_Dir = 0xFF;            /* Make LCD port direction as o/p */
    LCD_Port &= ~(1<<RW);
    return value;         
}