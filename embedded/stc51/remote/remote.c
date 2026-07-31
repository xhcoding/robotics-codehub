#include <mcs51/8051.h>

#include "lcd1602.h"

// 定时器 0 用于计数
void timer0Init() {
    TMOD &= 0xF0;
    TMOD |= 0x01;
    TH0 = 0;
    TL0 = 0;
    TF0 = 0;
    TR0 = 0;
}

void timer0SetCounter(unsigned int value) {
    TH0 = value / 256;
    TL0 = value % 256;
}

unsigned int timer0GetCounter(void) {
    return (TH0 << 8) | TL0;
}

void timer0Run(unsigned char flag) {
    TR0 = flag;
}

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

// 解码
unsigned int g_ir_time = 0;
unsigned char g_ir_state = 0;
unsigned char g_ir_data[4];
unsigned char g_ir_pdata;
unsigned char g_ir_data_flag;
unsigned char g_ir_repeat_flag;

unsigned char g_ir_address;
unsigned char g_ir_command;

unsigned int g_start_time;
unsigned int g_end_time;

void int0Routine() __interrupt(0) {
    if (g_ir_state == 0) {
        timer0SetCounter(0);
        timer0Run(1);
        g_ir_state = 1;
    } else if (g_ir_state == 1) {
        g_ir_time = timer0GetCounter();
        timer0SetCounter(0);
        if (g_ir_time > 12300 && g_ir_time < 13300) {
            g_ir_state = 2;
        } else if (g_ir_time > 10750 && g_ir_time < 111750) {
            g_ir_repeat_flag = 1;
            timer0Run(0);
            g_ir_state = 0;
        } else {
            g_ir_state =  1;
        }
    } else if (g_ir_state == 2) {
        g_ir_time = timer0GetCounter();

        timer0SetCounter(0);
        if (g_ir_time > 650 && g_ir_time < 1550) {
            g_ir_data[g_ir_pdata / 8] &= ~(0x01 << (g_ir_pdata % 8));
            g_ir_pdata++;
        } else if (g_ir_time > 1700 && g_ir_time < 2700) {
            g_ir_data[g_ir_pdata / 8] |= (0x01 << (g_ir_pdata % 8));
            g_ir_pdata++;
        } else {
            g_ir_pdata = 0;
            g_ir_state = 1;
            g_start_time = g_ir_time;
        }

        if (g_ir_pdata >= 32) {
            g_ir_pdata = 0;
            g_ir_address = g_ir_data[0];
            g_ir_command = g_ir_data[2];
            g_ir_data_flag = 1;
            timer0Run(0);
            g_ir_state = 0;
        }
    }
}



void main() {

    LCD_init();

    LCD_showString(1, 1, "ADR  CMD");

    // 外部中断初始化下降沿触发
    IT0 = 1;
    IE0 = 0;
    EX0 = 1;
    EA = 1;
    PX0 = 1;

    timer0Init();

    while (1) {
        if (g_ir_data_flag == 1) {
            LCD_showFixNumber(2, 1, g_ir_address, 10, 3);
            LCD_showFixNumber(2, 6, g_ir_command, 10, 3);
        }
        if (g_start_time != 0) {
            LCD_showNumber(2, 11, g_start_time, 10);
        }
    }
    
}
