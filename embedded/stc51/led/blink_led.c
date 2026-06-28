#include <mcs51/8051.h>
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


void main() {
    while (1) {
        P2 = 0xFE;  // 1111 1110
        delay500Ms();
        P2 = 0xFF;  // 1111 1111
        delay500Ms();
    };
    
}
