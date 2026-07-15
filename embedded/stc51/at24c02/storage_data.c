#include <mcs51/8051.h>

#include "at24c02.h"

#include "lcd1602.h"


void delayMs(unsigned int ms) {
    unsigned char i, j;
    while (ms) {
        i = 2;
        j = 239;
        do {
            while (--j);
        } while (--i);
        ms--;
    }
}



unsigned char num = 0;
void main() {
    LCD_init();
    LCD_showString(1, 1, "I2C");
    num = AT24C02_readByte(20);
    LCD_showNumber(2, 1, num, 10);
    while (1) {
        if (P3_3 == 0) {
            delayMs(20);  // 消除抖动
            while (P3_3 == 0) {
            }
            delayMs(20);
            num++;
            LCD_showNumber(2, 1, num, 10);
            AT24C02_writeByte(20, num);
        }
    }
}
