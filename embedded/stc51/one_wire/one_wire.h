#ifndef ONE_WIRE_H_
#define ONE_WIRE_H_

unsigned char ONEWIRE_init();

void ONEWIRE_sendBit(unsigned char bit);

unsigned char ONEWIRE_recvBit();

void ONEWIRE_sendByte(unsigned char byte);

unsigned char ONEWIRE_recvByte();

#endif
