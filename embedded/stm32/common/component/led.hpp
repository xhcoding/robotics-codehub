#pragma once

#include "peripheral/gpio.hpp"

namespace stm32
{

class Led final
{

public:
    enum class ActiveMode {
        HIGH,  // 高电平点亮
        LOW    // 低电平点亮
    };

public:
    Led(Gpio::Group group, uint8_t pin, ActiveMode mode);
    ~Led() = default;

    Led(const Led&) = delete;
    Led& operator=(const Led&) = delete;

public:
    void on();

    void off();

    void toggle();

private:
    Gpio gpio_;
    ActiveMode mode_;
};

}  // namespace stm32
