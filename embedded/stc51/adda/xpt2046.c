#include "xpt2046.h"

#include <mcs51/8051.h>

#define XPT2046_CS P3_5
#define XPT2046_DCLK P3_6
#define XPT2046_DIN P3_4
#define XPT2046_DOUT P3_7

extern void delayMs(unsigned int ms);

unsigned int XPT2046_readAd(unsigned char command) {
    unsigned int ad_value = 0;

    XPT2046_DCLK = 0;
    XPT2046_CS = 0;

    for (unsigned char i = 0; i < 8; i++) {
        XPT2046_DIN = command & (0x80 >> i);
        XPT2046_DCLK = 1;
        XPT2046_DCLK = 0;
    }

    for (unsigned char i = 0; i < 16; i++) {
        XPT2046_DCLK = 1;
        /* delayMs(1); */
        XPT2046_DCLK = 0;
        if (XPT2046_DOUT) {
            ad_value |= (0x8000 >> i);
        }
    }

    XPT2046_CS = 1;

    return ad_value >> 8;
}
