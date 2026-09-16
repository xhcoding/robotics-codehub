#include "component/key.hpp"

namespace stm32
{

Key::Key(Gpio::Group group, uint8_t pin) : gpio_(group, pin)
{
    gpio_.configure(Gpio::Mode::INPUT, Gpio::Speed::HIGH, Gpio::Pull::UP);
}

Key::~Key() {}

void Key::begin(uint32_t now_ms)
{
    raw_ = readRaw();
    stable_ = raw_;
    changed_time_ = now_ms;
    pressed_time_ = now_ms;
    long_fired_ = false;
}

Key::Event Key::update(uint32_t now_ms)
{
    bool raw = readRaw();
    if (raw != raw_) {
        raw_ = raw;
        changed_time_ = now_ms;
    }

    if (raw_ != stable_ && (now_ms - changed_time_) >= debounce_ms) {
        stable_ = raw_;
        if (stable_) {
            pressed_time_ = now_ms;
            long_fired_ = false;
            return EV_PRESS;
        } else {
            return EV_RELEASE;
        }
    }

    if (stable_ && !long_fired_ && long_press_ms > 0 && (now_ms - pressed_time_) >= long_press_ms) {
        long_fired_ = true;
        return EV_LONG;
    }
    return EV_NONE;
}

}  // namespace stm32
