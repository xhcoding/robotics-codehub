#include <mcs51/8051.h>

#include "lcd1602.h"
#include "xpt2046.h"

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


unsigned int g_ad = 0;

void main() {

    LCD_init();

    LCD_showString(1, 1, "ADJ  NTC  RG");
    
    while (1) {
        g_ad = XPT2046_readAd(XPT2046_XP);
        LCD_showFixNumber(2, 1, g_ad, 10, 3);
        delayMs(10);
        g_ad = XPT2046_readAd(XPT2046_YP);
        LCD_showFixNumber(2, 6, g_ad, 10, 3);
        delayMs(10);
        g_ad = XPT2046_readAd(XPT2046_VBAT);
        LCD_showFixNumber(2, 11, g_ad, 10, 3);
        delayMs(10);
    }
    
}
