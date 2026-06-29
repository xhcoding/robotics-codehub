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
    unsigned char num = 0;
    while (1) {
        if (P3_3 == 0) {
            delayMs(20);  // 消除抖动
            while (P3_3 == 0) {
            }
            delayMs(20);
            num++;
            P2 = ~num;
        }
    };
}
