#include "uart.h"

#include <mcs51/8051.h>

void UART_init() {
    SCON = 0x50;
    PCON |= 0x80;
    // 定时器 T1 配置
    TMOD = TMOD & 0x0F;
    TMOD = TMOD | 0x20;
    TL1 = 0xF3;  // 定时器初始值, 波特率 4800
    TH1 = 0xF3;
    ET1 = 0;  // 禁止定时器中断
    TR1 = 1;  // 启动定时器
    EA = 1;   // 打开串口中断
    ES = 1;
}

void UART_sendByte(unsigned char byte) {
    SBUF = byte;
    while (TI == 0) {  // 检测是否完成
        continue;
    }
    TI == 0;
}
