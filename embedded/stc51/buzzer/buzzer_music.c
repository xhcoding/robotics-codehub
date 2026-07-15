#include <mcs51/8051.h>

void timer0Init() {
    TMOD = TMOD & 0xF0;   // 0xF0 = 1111 0000，屏蔽低4位
    TMOD = TMOD | 0x01;   // 与上面清零后的值做或运算，置位最低位
    TF0 = 0;
    TR0 = 1;
    TH0 = 64655 / 256;    // 装载高8位初值
    TL0 = 64655 % 256;    // 装载低8位初值
    ET0 = 1;
    EA = 1;
    PT0 = 0;  // 设为低优先级（默认值，此处显式设置确保明确）
}

void timer0Routine() __interrupt(1) {
    // 中断触发，重新设置定时器初始值
    TH0 = 64655 / 256;
    TL0 = 64655 % 256;
    P2_5 = !P2_5; // 蜂鸣器一次振荡
}

void main() {
    timer0Init();

    while (1) {
    }
    
}
