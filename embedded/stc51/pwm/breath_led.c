#include <mcs51/8051.h>

#define LED P2_0

void delay(unsigned int t) {
    while (t--) {

    }
}

void main() {
    unsigned char time, i;
    while (1) {
        for (time = 0; time < 100; time++) {
            for (i = 0; i < 20; i++) {
                LED = 0;
                delay(time);
                LED = 1;
                delay(100 - time);
            }
        }
        for (time = 100; time > 0; time--) {
            for (i = 0; i < 20; i++) {
                LED = 0;
                delay(time);
                LED = 1;
                delay(100 - time);
            }
        }
    }
}
