#include "lcd1602.h"

void main() {
    LCD_init();
    LCD_showChar(2, 0, 'A');
    LCD_showString(1, 0, "Hello World!");
    LCD_showNumber(2, 1, 768, 10);
    LCD_showNumber(2, 5, 8, 2);
    LCD_showNumber(2, 10, 30, 16);

    LCD_showNumber(2, 13, -16, 16);
    while (1) {
        
    }
}
