#ifndef _MY_LCD
#define _MY_LCD

// Alphanumeric LCD functions
#define LCD_Dir  DDRC             /* Define LCD data port direction */
#define LCD_Port PORTC            /* Define LCD data port */
#define LCD_Pin  PINC            /* Define LCD data port */

#define RS PORTC2                 /* Define Register Select pin */
#define RW PORTC1                 /* Define RW pin */
#define EN PORTC0                 /* Define Enable signal pin */


void LCD_Command( unsigned char cmnd );
void LCD_Char( unsigned char data );;
void LCD_Init (void);            /* LCD Initialize function */
void LCD_String (char *str);        /* Send string to LCD function */
void LCD_String_xy (char row, char pos, char *str);    /* Send string to LCD with xy position */;
void LCD_Clear();
void lcd_create_char(unsigned char address, unsigned char pattern[]);
void LCD_Gotoxy(char row, char pos);
unsigned char LCD_Read_Char(unsigned char address );

#endif
