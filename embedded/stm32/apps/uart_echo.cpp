/*!
 * \file uart_echo.cpp
 * \brief stm32::Uart 回显示例：USART1 / PA9-TX PA10-RX / 115200-8N1。
 *
 * 接收走 DMA + 空闲线（一帧一次事件），发送走中断（非阻塞入队）。
 * 烧录后终端连 USART1 即可看到 "stm32::Uart ready"，输入任意字符被按帧回显。
 */
#include <chrono>
#include <span>

#include "component/led.hpp"
#include "peripheral/uart.hpp"
#include "system/system.hpp"

using namespace std::chrono_literals;  // 字面量命名空间：Google 允许文件级 using-directive

using stm32::Gpio;
using stm32::Led;
using stm32::System;
using stm32::Uart;

static Led g_led(Gpio::Group::C, 13, Led::ActiveMode::LOW);

/// 帧到达（ISR 上下文：非捕获、无阻塞）：原样回显
static void onFrame(std::span<const uint8_t> data, void* ctx)
{
    static_cast<Uart*>(ctx)->write(data);
}

/// 发送队列排空（ISR 上下文）
static void onTxIdle(void*)
{
    g_led.on();
}

int main()
{
    if (!System::init()) {
        System::panic(0x5000);
    }

    // 必须在 System::init() 之后构造：波特率按当前 PCLK 计算。
    // Uart 对象较大（~1.2 KB，含收发环），用 static 存储期而非塞进栈帧。
    static Uart uart(Uart::Inst::Usart1);
    uart.onRx(onFrame, &uart);
    uart.onTx(onTxIdle);

    uart.writeAll("stm32::Uart ready\r\n");
    (void) uart.flush(100ms);

    auto last = System::now();
    while (true) {
        if (System::now() - last >= 5000ms) {
            last = System::now();
            g_led.off();
            uart.write('.');  // 心跳：非阻塞入队，立即返回
        }
    }
}
