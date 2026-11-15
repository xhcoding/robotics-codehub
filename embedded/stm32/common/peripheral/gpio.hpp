#pragma once

#include <stdint.h>

namespace stm32
{

class Gpio
{
public:
    enum class Group { A, B, C };
    enum class Mode {
        INPUT,
        OUTPUT_PP,
        OUTPUT_OD,
        OUTPUT_AF_PP,
        OUTPUT_AF_OD,
    };
    enum class Speed { LOW, MEDIUM, HIGH };

    enum class Pull {
        NO,
        UP,
        DOWN
    };

public:
    Gpio(Group group, uint8_t pin);

    void configure(Mode mode, Speed speed, Pull pull);

    void set();

    void reset();

    bool test();

    void toggle();

private:
    void enableClock();

private:
    void* port_;
    uint16_t pin_;
};

}  // namespace stm32
