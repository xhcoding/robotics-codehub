#include "lcd1602.h"
#include "ds18b20.h"

float temp;
void main() {
    LCD_init();
    LCD_showString(1, 1, "Temp:");

    while (1) {
        DS18B20_convertTemp();
        temp = DS18B20_readTemp();
        if (temp < 0) {
            LCD_showChar(2, 1, '-');
            temp = -temp;
        } else {
            LCD_showChar(2, 1, '+');
        }
        LCD_showFixNumber(2, 2, temp, 10, 3);
        LCD_showChar(2, 5, '.');
        LCD_showNumber(2, 6, (unsigned long)(temp * 10000) % 10000, 10);
    }
}
