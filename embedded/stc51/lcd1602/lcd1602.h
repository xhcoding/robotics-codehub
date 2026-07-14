#ifndef LCD1602_H_
#define LCD1602_H_

void LCD_init();

void LCD_showChar(unsigned char row, unsigned char col, unsigned char ch);

void LCD_showString(unsigned char row, unsigned char col, const char* str);

void LCD_showNumber(unsigned char row, unsigned char col, int num, unsigned char base);

void LCD_showFixNumber(unsigned char row, unsigned char col, int num, unsigned char base, unsigned fix_len);

#endif
