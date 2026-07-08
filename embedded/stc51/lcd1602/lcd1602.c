#include <mcs51/8051.h>

#define LCD_DATA_PORT P0

static void LCD_setRS() {
    P2 |= (1 << 6);
}

static void LCD_unsetRS() {
    P2 &= ~(1 << 6);
}

static void LCD_setRW() {
    P2 |= (1 << 5);
}

static void LCD_unsetRW() {
    P2 &= ~(1 << 5);
}

static void LCD_setE() {
    P2 |= (1 << 7);
}

static void LCD_unsetE() {
    P2 &= ~(1 << 7);
}

static void LCD_delay() {
    unsigned char i, j;
    i = 2;
    j = 239;
    do {
        while (--j);
    } while (--i);
}

static void LCD_writeCommand(unsigned char cmd) {
    LCD_unsetRS();
    LCD_unsetRW();
    LCD_DATA_PORT = cmd;
    LCD_setE();
    LCD_delay();
    LCD_unsetE();
    LCD_delay();
}

static void LCD_writeData(unsigned char data) {
    LCD_setRS();
    LCD_unsetRW();
    LCD_DATA_PORT = data;
    LCD_setE();
    LCD_delay();
    LCD_unsetE();
    LCD_delay();
}

void LCD_init() {
    LCD_writeCommand(0x38);
    LCD_writeCommand(0x0c);
    LCD_writeCommand(0x06);
    LCD_writeCommand(0x01);
}

static void LCD_setCursor(unsigned char row, unsigned char col) {
    unsigned char addr;
    if (row == 1) {
        addr = col - 1;
    } else {
        addr = col - 1 + 0x40;
    }

    LCD_writeCommand(0x80 | addr);
}

void LCD_showChar(unsigned char row, unsigned char col, unsigned char ch) {
    LCD_setCursor(row, col);
    LCD_writeData(ch);
}

void LCD_showString(unsigned char row, unsigned char col, const char* str) {
    LCD_setCursor(row, col);

    for (unsigned char i = 0; i < 16; i++) {
        if (str[i] == '\0') {
            break;
        }
        LCD_writeData(str[i]);
    }
}

static int LCD_pow(int  x, int y) {
    int result = 1;
    for (unsigned char i = 0; i < y; i++) {
        result *= x;
    }
    return result;
}

void LCD_showNumber(unsigned char row, unsigned char col, int num, unsigned char base) {
    LCD_setCursor(row, col);
    if (num < 0) {
        LCD_writeData('-');
        num = -num;
    }

    unsigned char len = 0;
    int num_len = num;
    do {
        num_len /= base;
        len++;
    } while (num_len != 0);

    for (unsigned char i = len; i > 0; i--) {
        unsigned char n = (num / LCD_pow(base, i - 1) % base);
        if (n < 10) {
            LCD_writeData(n + '0');
        } else {
            LCD_writeData(n - 10 + 'A');
        }
    }
}
