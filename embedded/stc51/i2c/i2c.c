#include "i2c.h"

#include <mcs51/8051.h>

#define I2C_SCL P2_1
#define I2C_SDA P2_0

void I2C_start() {
    I2C_SDA = 1;
    I2C_SCL = 1;
    I2C_SDA = 0;
    I2C_SCL = 0;
}

void I2C_stop() {
    I2C_SDA = 0;
    I2C_SCL = 1;
    I2C_SDA = 1;
}

void I2C_sendByte(unsigned char byte) {
    for (unsigned char i = 0; i < 8; i++) {
        I2C_SDA = (byte & (0x80 >> i));
        I2C_SCL = 1;
        I2C_SCL = 0;
    }
}

unsigned char I2C_recvByte() {
    unsigned char byte = 0;

    for (unsigned char i = 0; i < 8; i++) {
        I2C_SDA = 1;
        I2C_SCL = 1;
        if (I2C_SDA) {
            byte |= (0x80 >> i);
        }
        I2C_SCL = 0;
    }
    return byte;
}

void I2C_sendAck(unsigned char ack) {
    I2C_SDA = ack;
    I2C_SCL = 1;
    I2C_SCL = 0;
}

unsigned char I2C_recvAck() {
    unsigned char ack = 0;

    I2C_SDA = 1;
    I2C_SCL = 1;
    if (I2C_SDA) {
        ack = I2C_SDA;
    }
    I2C_SCL = 0;
    return ack;
}
