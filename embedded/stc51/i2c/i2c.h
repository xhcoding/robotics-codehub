#ifndef I2C_H_
#define I2C_H_

void I2C_start();

void I2C_stop();

void I2C_sendByte(unsigned char byte);

unsigned char I2C_recvByte();

void I2C_sendAck(unsigned char ack);

unsigned char I2C_recvAck();

#endif
