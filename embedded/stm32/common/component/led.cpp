#include "component/led.hpp"

namespace stm32
{

Led::Led(Gpio::Group group, uint8_t pin, ActiveMode mode) : gpio_(group, pin), mode_(mode)
{
    gpio_.configure(Gpio::Mode::OUTPUT_PP, Gpio::Speed::LOW, Gpio::Pull::NO);
}

void Led::on()
{
    if (mode_ == ActiveMode::HIGH) {
        gpio_.set();
    } else {
        gpio_.reset();
    }
}

void Led::off()
{
    if (mode_ == ActiveMode::HIGH) {
        gpio_.reset();
    } else {
        gpio_.set();
    }
}

void Led::toggle()
{
    gpio_.toggle();
}

}  // namespace stm32
