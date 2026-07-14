#include <mcs51/8051.h>

// SER
#define SER P3_4
// SRCLK
#define SCK P3_6
// RCLK
#define RCK P3_5

void MatrixLED_74hc595WriteByte(unsigned char byte) {
    unsigned char i;
    for (i = 0; i < 8; i++) {
        SER = byte & (0x80 >> i);
        SCK = 1;
        SCK = 0;
    }
    RCK = 1;
    RCK = 0;
}

void MatrixLED_showColumn(unsigned char column, unsigned char data) {
    MatrixLED_74hc595WriteByte(data);
    P0 = ~(0x80 >> column);
}

void main() {
    SCK = 0;
    RCK = 0;
    while (1) {
        for (unsigned char i = 0; i < 8; i++) {
            MatrixLED_showColumn(i, (1 << i));
        }
    }
}
