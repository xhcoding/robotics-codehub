#pragma once

#include <chrono>
#include <cstdint>

namespace stm32
{
class System
{
public:
    static bool init();

    static void delay(std::chrono::milliseconds ms);

    static std::chrono::milliseconds now();

    // 致命配置错误（无异常环境）。默认死锁；Debug 可覆写为断言/打印。
    [[noreturn]] static void panic(uint32_t code);


    // 每微秒多少 tick
    static inline constexpr uint32_t ticksPerUs()
    {
        return 72;
    }
};

}  // namespace stm32
