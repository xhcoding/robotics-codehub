#include <mcs51/8051.h>

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

void UART_init() {  // 波特率：4800
    SCON = 0x40; // 只发不收
    PCON |= 0x80;
    // 定时器 T1 配置
    TMOD = TMOD & 0x0F;
    TMOD = TMOD | 0x20;
    TL1 = 0xF3;  // 定时器初始值
    TH1 = 0xF3;
    ET1 = 0;  // 禁止定时器中断
    TR1 = 1;  // 启动定时器
}

void UART_sendByte(unsigned char byte) {
    SBUF = byte;
    while (TI == 0) {  // 检测是否完成
        continue;
    }
    TI == 0;
}

unsigned char num = 0;
void main() {
    UART_init();
    while (1) {
        UART_sendByte(num);
        num++;
        delayMs(1000);
    }
}
