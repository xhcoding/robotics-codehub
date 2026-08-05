#include "system/system.hpp"
#include "peripheral/gpio.hpp"

using namespace stm32;
using namespace std::chrono_literals;

int main()
{
    System::init();

    auto gpioc_13 = Gpio(Gpio::Group::C, 13);
    gpioc_13.configure(Gpio::Mode::OUTPUT_PP, Gpio::Speed::LOW, Gpio::Pull::NO);

    while (true) {
        gpioc_13.toggle();
        System::delay(5s);
    }

    return 0;
}
