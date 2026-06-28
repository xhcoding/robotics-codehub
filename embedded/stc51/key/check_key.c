#include <mcs51/8051.h>

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

void main() {
    while (1) {
        if (P3_0 == 0) {  // P3 为低电平，键被按下
            P2_0 = 0;
        } else {
            P2_0 = 1;
        }

        if (P3_1 == 0) {
            P2_1 = 0;
        } else {
            P2_1 = 1;
        }

        if (P3_2 == 0) {
            P2_2 = 0;
        } else {
            P2_2 = 1;
        }

        // 3按一下
        if (P3_3 == 0) {
            delayMs(20);  // 消除抖动
            while (P3_3 == 0) {
            }
            delayMs(20);
            if (P2_3) {
                P2_3 = 0;
            } else {
                P2_3 = 1;
            }
        }
    };
}
