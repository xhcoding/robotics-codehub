#include "stm32f1xx.h"

void delay() {
    for (int32_t j = 0; j < 2400000; j++) {
        __NOP(); // 空操作，防止被优化
    }
}

int main() {

    // 1. 使能GPIOC的时钟
    // APB2ENR寄存器的第4位（PCEN）置1，使能GPIOC外设时钟（位4对应GPIOC）
    RCC->APB2ENR = 0x00000010;

    // 2. 配置GPIOC的引脚13为推挽输出模式，最大输出速度50MHz
    // CRH控制高8位引脚（PC8~PC15），每4位控制一个引脚：
    //   PC13对应CRH的位[23:20]（即第20~23位）
    //   0x3表示：CNF[1:0]=00（通用推挽输出），MODE[1:0]=11（输出模式，50MHz）
    //   将0x3写入位[23:20]，即 CRH = 0x00300000（高8位中，PC13对应的4位为0011）
    GPIOC->CRH = 0x00300000;

    // 3. 设置PC13输出低电平
    // ODR寄存器对应位13置0，输出低电平
    GPIOC->ODR = 0x00000000;  // 所有引脚输出低电平，此处主要使PC13输出0 ，打开 led

    // GPIOC->ODR = 0x00002000;  // 所有引脚输出低电平，此处主要使PC13输出 1，关闭 led

    while (true) {
        delay();
        GPIOC->ODR = 0x00002000;
        delay();
        GPIOC->ODR = 0x00000000;
    }
    return 0;
}
