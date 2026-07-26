#include <mcs51/8051.h>

#define DC_MOTOR P1_0

// 100 us
void timer0Init() {
    TMOD = TMOD & 0xF0;
    TMOD = TMOD | 0x01;
    TF0 = 0;
    TR0 = 1;
    TH0 = 0x9C;  // 装载高8位初值
    TL0 = 0xFF;  // 装载低8位初值
    ET0 = 1;
    EA = 1;
    PT0 = 0;
}

unsigned char g_counter, g_compare;

void timer0Routine() __interrupt(1) {
    TH0 = 0x9C;
    TL0 = 0xFF;
    g_counter++;
    if (g_counter == 100) {
        g_counter = 0;
    }
    if (g_counter < g_compare) {
        DC_MOTOR = 1;
    } else {
        DC_MOTOR = 0;
    }
}

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
    timer0Init();

    unsigned char speed = 0;
    while (1) {
        // 3按一下
        if (P3_3 == 0) {
            delayMs(20);  // 消除抖动
            while (P3_3 == 0) {
            }
            delayMs(20);
            speed++;
            speed %= 5;
            showNum(7, speed);
            if (speed == 0) {
                g_compare = 0;
            }
            if (speed == 1) {
                g_compare = 50;
            }
            if (speed == 2) {
                g_compare = 75;
            }
            if (speed == 3) {
                g_compare = 100;
            }
        }
    }
}
