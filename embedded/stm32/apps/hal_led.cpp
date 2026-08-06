#include <array>

#include "component/led.hpp"
#include "system/system.hpp"

using namespace stm32;
using namespace std::chrono_literals;

int main()
{
    System::init();

    // 流水 LED 灯
    std::array<Led, 3> leds = {
            Led(Gpio::Group::C, 13, Led::ActiveMode::LOW),
            Led(Gpio::Group::A, 0, Led::ActiveMode::LOW),
            Led(Gpio::Group::A, 2, Led::ActiveMode::LOW),
    };

    while (true) {
        for (auto& led : leds) {
            led.on();
            System::delay(1s);
            led.off();
            System::delay(1s);
        }
    }

    return 0;
}
