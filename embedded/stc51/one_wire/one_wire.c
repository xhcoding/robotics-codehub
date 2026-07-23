#include <mcs51/8051.h>

#include "one_wire.h"

#define ONE_WIRE_IO P3_7

unsigned char ONEWIRE_init() {
    ONE_WIRE_IO = 1;
    ONE_WIRE_IO = 0;

    // 延时 480us
    unsigned char i = 247;
    while (--i) {
    }

    ONE_WIRE_IO = 1;
    // 延时 70 us
    i = 32;
    while (--i) {
    }

    unsigned char ack = 0;
    ack = ONE_WIRE_IO;
    // 延时 500 us
    i = 247;
    while (--i) {
    }

    return ack;
}

void ONEWIRE_sendBit(unsigned char bit) {
    ONE_WIRE_IO = 0;

    // 延时 10us
    unsigned char i = 4;
    while (--i) {}

    ONE_WIRE_IO = bit;

    // 延时 50 us
    i = 24;
    while (--i) {
    }

    ONE_WIRE_IO = 1;
}

unsigned char ONEWIRE_recvBit() {
    unsigned char bit;
    ONE_WIRE_IO = 0;

    // 5us 延时
    unsigned char i = 2;
    while (--i) {
    }

    ONE_WIRE_IO = 1;

    // 5us 延时
    i = 2;
    while (--i) {
    }

    bit = ONE_WIRE_IO;
    
    // 延时 50 us
    i = 24;
    while (--i) {
    }


    return bit;
}

void ONEWIRE_sendByte(unsigned char byte) {
    for (unsigned char i = 0; i < 8; i++) {
        ONEWIRE_sendBit((byte & (0x01 << i)));
    }
}

unsigned char ONEWIRE_recvByte() {
    unsigned char ret = 0;

    for (unsigned char i = 0; i < 8; i++) {
        if (ONEWIRE_recvBit()) {
            ret |= (0x01 << i);
        }
    }
    return ret;
}
