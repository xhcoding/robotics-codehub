#include "at24c02.h"

#include <mcs51/8051.h>

#include "i2c.h"

#define AT24C02_I2C_ADDR 0xA0

void AT24C02_writeByte(unsigned char address, unsigned char data) {
    I2C_start();
    I2C_sendByte(AT24C02_I2C_ADDR);
    I2C_recvAck();
    I2C_sendByte(address);
    I2C_recvAck();
    I2C_sendByte(data);
    I2C_recvAck();
    I2C_stop();
}

unsigned char AT24C02_readByte(unsigned char address) {
    unsigned char data = 0xFF;
    I2C_start();
    I2C_sendByte(AT24C02_I2C_ADDR);
    I2C_recvAck();
    I2C_sendByte(address);
    I2C_recvAck();
    I2C_start();
    I2C_sendByte(AT24C02_I2C_ADDR | 0x01);
    I2C_recvAck();

    data = I2C_recvByte();
    I2C_sendAck(1);
    I2C_stop();

    return data;
}
