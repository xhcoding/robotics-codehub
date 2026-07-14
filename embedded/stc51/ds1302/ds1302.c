#include <mcs51/8051.h>

#include "lcd1602.h"

#define DS1302_SCLK P3_6
#define DS1302_IO P3_4
#define DS1302_CE P3_5

#define DS1302_SECOND (0x80)
#define DS1302_MINUTE (0x82)
#define DS1302_HOUR (0x84)
#define DS1302_DATE (0x86)
#define DS1302_MONTH (0x88)
#define DS1302_DAY (0x8A)
#define DS1302_YEAR (0x8C)
#define DS1302_WP (0x8E)

void DS1302_init() {
    DS1302_CE = 0;
    DS1302_SCLK = 0;
}

void DS1302_writeByte(unsigned char command, unsigned char data) {
    DS1302_CE = 1;

    unsigned char i;
    for (i = 0; i < 8; i++) {
        DS1302_IO = command & (0x01 << i);
        DS1302_SCLK = 1;
        DS1302_SCLK = 0;
    }

    for (i = 0; i < 8; i++) {
        DS1302_IO = data & (0x01 << i);
        DS1302_SCLK = 1;
        DS1302_SCLK = 0;
    }

    DS1302_CE = 0;
}

unsigned char DS1302_readByte(unsigned char command) {
    unsigned char i, data = 0;

    command |= 0x01;

    DS1302_CE = 1;

    for (i = 0; i < 8; i++) {
        DS1302_IO = command & (0x01 << i);
        DS1302_SCLK = 0;
        DS1302_SCLK = 1;
    }

    for (i = 0; i < 8; i++) {
        DS1302_SCLK = 1;
        DS1302_SCLK = 0;
        if (DS1302_IO) {
            data = data | (0x01 << i);
        }
    }

    DS1302_CE = 0;
    DS1302_IO = 0;
    return data;
}

unsigned char bcd2dec(unsigned char bcd) {
    return bcd / 16 * 10 + bcd % 16;
}

unsigned char dec2bcd(unsigned char dec) {
    return dec / 10 * 16 + dec % 10;
}


unsigned char g_time[] = {26, 7, 12, 16, 47, 30, 7};

unsigned char DS1302_setTime() {
    DS1302_writeByte(DS1302_WP, 0x00);
    DS1302_writeByte(DS1302_YEAR, dec2bcd(g_time[0]));
    DS1302_writeByte(DS1302_MONTH, dec2bcd(g_time[1]));
    DS1302_writeByte(DS1302_DATE, dec2bcd(g_time[2]));
    DS1302_writeByte(DS1302_HOUR, dec2bcd(g_time[3]));
    DS1302_writeByte(DS1302_MINUTE, dec2bcd(g_time[4]));
    DS1302_writeByte(DS1302_SECOND, dec2bcd(g_time[5]));
    DS1302_writeByte(DS1302_DAY, dec2bcd(g_time[6]));
    DS1302_writeByte(DS1302_WP, 0x80);
}

unsigned char DS1302_readTime() {
    g_time[0] = bcd2dec(DS1302_readByte(DS1302_YEAR));
    g_time[1] = bcd2dec(DS1302_readByte(DS1302_MONTH));
    g_time[2] = bcd2dec(DS1302_readByte(DS1302_DATE));
    g_time[3] = bcd2dec(DS1302_readByte(DS1302_HOUR));
    g_time[4] = bcd2dec(DS1302_readByte(DS1302_MINUTE));
    g_time[5] = bcd2dec(DS1302_readByte(DS1302_SECOND));
    g_time[6] = bcd2dec(DS1302_readByte(DS1302_DAY));
}

void main() {
    LCD_init();
    DS1302_init();
    LCD_showString(1, 1, "RTC: ");

    DS1302_setTime();

    while (1) {
        DS1302_readTime();

        LCD_showFixNumber(1, 1, g_time[0], 10, 2);
        LCD_showChar(1, 3, '-');
        LCD_showFixNumber(1, 4, g_time[1], 10, 2);
        LCD_showChar(1, 6, '-');
        LCD_showFixNumber(1, 7, g_time[2], 10, 2);
        
        LCD_showFixNumber(2, 1, g_time[3], 10, 2);
        LCD_showChar(2, 3, ':');
        LCD_showFixNumber(2, 4, g_time[4], 10, 2);
        LCD_showChar(2, 6, ':');
        LCD_showFixNumber(2, 7, g_time[5], 10, 2);
        LCD_showChar(2, 9, '/');
        LCD_showFixNumber(2, 10, g_time[6], 10, 2);
    }
}
