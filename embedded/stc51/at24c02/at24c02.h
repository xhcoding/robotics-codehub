#ifndef AT24C02_H_
#define AT24C02_H_

void AT24C02_writeByte(unsigned char address, unsigned char data);

unsigned char AT24C02_readByte(unsigned char address);

#endif
