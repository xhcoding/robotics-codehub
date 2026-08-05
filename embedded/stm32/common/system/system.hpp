#pragma once

#include <chrono>

namespace stm32
{
class System
{
public:
    static bool init();

    static void delay(std::chrono::milliseconds ms);
};

}  // namespace stm32_common
