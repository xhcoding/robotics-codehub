#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>

namespace stm32
{

/**
 * 时间量 → 微秒数（整数截断）。
 *
 * @tparam Duration 可转换为微秒的时间量
 * @param d 时间量
 * @return 微秒数
 */
template <class Duration>
requires std::convertible_to<Duration, std::chrono::microseconds>
constexpr uint32_t toMicroseconds(Duration d) noexcept
{
    return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(d).count());
}

}  // namespace stm32
