#pragma once

#include "peripheral/gpio.hpp"

namespace stm32
{

class Key final
{
public:
    enum Event : uint8_t { EV_NONE = 0, EV_PRESS, EV_RELEASE, EV_LONG };

public:
    Key(Gpio::Group group, uint8_t pin);
    ~Key();

    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;

public:
    void begin(uint32_t now_ms);

    Event update(uint32_t now_ms);

private:
    bool readRaw()
    {
        return !gpio_.test();
    }

private:
    Gpio gpio_;

    bool raw_;
    bool stable_;
    uint32_t changed_time_;
    uint32_t pressed_time_;
    bool long_fired_;

    inline constexpr static uint32_t debounce_ms = 10;
    inline constexpr static uint32_t long_press_ms = 1000;
};

}  // namespace stm32
