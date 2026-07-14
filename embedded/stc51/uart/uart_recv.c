#include <mcs51/8051.h>

#include "uart.h"
#include "lcd1602.h"

void main() {
    UART_init();
    LCD_init();
    LCD_showString(1, 1, "uart_recv");
    while (1) {

    }
}

void UART_Routine() __interrupt(4) {
    if (RI == 1) { // 接收中断
        unsigned char n = SBUF;

        P2 = ~SBUF;

        UART_sendByte(n);
        RI = 0;
    }
    
}
