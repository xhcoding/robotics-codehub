/*!
 * \file dbg_demo.cpp
 * \brief stm32::dbg 调试打印示例：USART1 / PA9-TX PA10-RX / 115200-8N1。
 *
 * 控制台串口绑定后，用 LOG_E/W/I/V 打日志（等级在编译期生效），
 * 收到的每一帧按十六进制转储一次，同时原样回显。
 */
#include <chrono>
#include <cstddef>
#include <span>

#include "component/led.hpp"
#include "debug/dbg.hpp"
#include "peripheral/uart.hpp"
#include "system/system.hpp"

using namespace std::chrono_literals;  // 字面量命名空间：Google 允许文件级 using-directive

using stm32::Gpio;
using stm32::Led;
using stm32::System;
using stm32::Uart;
using stm32::dbg::bind;
using stm32::dbg::dump;

static Led g_led(Gpio::Group::C, 13, Led::ActiveMode::LOW);
static Uart* g_uart = nullptr;
static constexpr size_t kDumpMaxBytes = 32;  // 每帧最多转储的字节数

/// 帧到达（ISR 上下文）：转储 + 回显
static void onFrame(std::span<const uint8_t> data, void*)
{
    LOG_I("rx %u byte(s)\n", static_cast<unsigned>(data.size()));
    dump(data, kDumpMaxBytes);
    if (g_uart != nullptr) {
        g_uart->write(data);  // 原样回显，非阻塞
    }
}

int main()
{
    if (!System::init()) {
        System::panic(0x5000);
    }

    static Uart uart(Uart::Inst::Usart1);
    g_uart = &uart;
    bind(&uart);  // 之后 LOG_* 都走这个串口
    uart.onRx(onFrame, &uart);

    LOG_E("boot: %s sysclk=%luHz\n", "STM32F103xB", 72000000UL);
    LOG_W("提示：输入任意字符即可看到十六进制转储与回显\n");
    LOG_I("uart: USART1 PA9/PA10 默认配置（115200-8N1, DMA+空闲线收, IT 发）\n");

    auto last = System::now();
    while (true) {
        if (System::now() - last >= 1000ms) {
            last = System::now();
            g_led.toggle();
            auto const s = uart.getStats();
            LOG_V("stats: tx=%lu rx=%lu lost=%lu fe=%lu ore=%lu\n",
                  static_cast<unsigned long>(s.tx_bytes), static_cast<unsigned long>(s.rx_bytes),
                  static_cast<unsigned long>(s.rx_lost), static_cast<unsigned long>(s.err_fe),
                  static_cast<unsigned long>(s.err_ore));
        }
    }
}
