#include <chrono>

#include "component/led.hpp"
#include "peripheral/gpio.hpp"
#include "peripheral/tim.hpp"
#include "system/system.hpp"

using namespace stm32;
using namespace std::chrono_literals;

// // 全局 Led：单条 16-bit 寄存器写，ISR 中调用安全
static Led g_led(Gpio::Group::C, 13, Led::ActiveMode::LOW);

// // 1 ms 心跳回调（ISR 上下文：非捕获、无阻塞）
static void onTick(uint16_t, void*)
{
    g_led.toggle();
}

int main()
{
    System::init();

    Tim tick(Tim::Inst::Tim2, 10000ms);
    tick.subscribe(Tim::Evt::Update, onTick);
    tick.start();

    while (true) {
        System::delay(1000ms);
    }

    return 0;
}
