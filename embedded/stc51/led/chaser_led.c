#include <mcs51/8051.h>
#include <stdbool.h>
#include <mcs51/compiler.h>

void delay500Ms() { // 12.000Mhz 软件延时 500 ms
    unsigned char i, j, k;
    NOP();
    i = 4;
    j = 205;
    k = 187;
    do {
        do {
            while (--k);
        } while (--j);

    } while (--i);
}

// 开关指定位置的灯
void toggleLed(unsigned int i, bool is_on) {
    unsigned char o = P2;
    if (is_on) {
        P2 = (unsigned char) (o & ~(1 << i));
    } else {
        P2 = (unsigned char)(o | (1 << i));
    }
}

void main() {
    while (1) {
        for (int i = 0; i < 8; i++) {
            toggleLed(i, true);
            delay500Ms();
            toggleLed(i, false);
        }
    }

}
