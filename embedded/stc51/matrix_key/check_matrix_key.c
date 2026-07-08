#include <mcs51/8051.h>
#include <stdbool.h>

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

int rowScan(unsigned char row) {
    int key = 0;
    P1 = 0xFF;
    if (row == 1) {
        P1_7 = 0;
    } else if (row == 2) {
        P1_6 = 0;
    } else if (row == 3) {
        P1_5 = 0;
    } else if (row == 4) {
        P1_4 = 0;
    }

    if (P1_3 == 0) {
        delayMs(20);
        while (P1_3 == 0) {
        }
        delayMs(20);
        key = 1;
    }
    if (P1_2 == 0) {
        delayMs(20);
        while (P1_2 == 0) {
        }
        delayMs(20);
        key = 2;
    }
    if (P1_1 == 0) {
        delayMs(20);
        while (P1_1 == 0) {
        }
        delayMs(20);
        key = 3;
    }
    if (P1_0 == 0) {
        delayMs(20);
        while (P1_0 == 0) {
        }
        delayMs(20);
        key = 4;
    }
    return key;
}

int keyDown() {
    int key  = 0;
    for (unsigned row = 1; row <= 4; row++) {
        key = rowScan(row);
        if (key != 0) {
            return key + 4 * (row - 1);
        }
    }
    return key;
}


void main() {
    LCD_init();

    while (1) {
        int key = keyDown();
        if (key != 0) {
            LCD_showString(1, 0, "  ");
            LCD_showNumber(1, 0, key, 10);
        }
    }
    
}
