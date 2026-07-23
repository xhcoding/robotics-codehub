#include "ds18b20.h"

#include "one_wire.h"

void DS18B20_convertTemp() {
    ONEWIRE_init();
    ONEWIRE_sendByte(0xCC);  // SKIP ROM
    ONEWIRE_sendByte(0x44); // ConvertT
}

float DS18B20_readTemp() {
    ONEWIRE_init();
    ONEWIRE_sendByte(0xCC);

    ONEWIRE_sendByte(0xBE);  // read reg

    unsigned char tlsb = ONEWIRE_recvByte();
    unsigned char tmsb = ONEWIRE_recvByte();

    int temp = (tmsb << 8) | tlsb;  // 转为 16 位

    return (temp / 16.0);
}
