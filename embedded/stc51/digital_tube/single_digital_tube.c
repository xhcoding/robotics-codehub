#include <mcs51/8051.h>
#include <stdbool.h>

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

static unsigned char g_digitTab[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F,
                                     0x6F, 0x77, 0X7C, 0x39, 0x5E, 0x79, 0x71, 0x00};

void showNum(unsigned char location, unsigned char n) {
    unsigned char num = P2;
    num = (num & ~0x1C) | ((location & 0x07) << 2);
    P2 = num;

    P0 = g_digitTab[n];
}

void main() {
    while (1) {
        for (unsigned char j = 0; j < 8; j++) {
            for (unsigned char i = 0; i <= 16; i++) {
                showNum(j, i);
                delayMs(1000);
            }
        }
    }
}
