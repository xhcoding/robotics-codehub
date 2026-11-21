#pragma once

#include <cstdarg>
#include <cstddef>
#include <span>

#include "peripheral/uart.hpp"

#ifndef DBG_LEVEL
#define DBG_LEVEL 4  ///< 0=完全关闭 1=只留 E 2=W 3=I 4=V（编译期生效）
#endif

#ifndef DBG_LINE_SIZE
#define DBG_LINE_SIZE 128  ///< 单条日志的格式化缓冲（在调用栈上，见下方说明）
#endif

namespace stm32::dbg
{

/**
 * 极简调试打印：自实现格式化（debug/fmt.hpp）+ Uart 非阻塞发送，不经 newlib stdio。
 *
 * 用法：
 * @code
 *   dbg::bind(&uart);                       // 绑定控制台串口（通常 USART1）
 *   LOG_E("boot %s %p\n", "f103", &uart);   // 等级：E > W > I > V
 *   LOG_I("tx=%lu rx=%lu lost=%u\n", s.tx_bytes, s.rx_bytes, s.rx_lost);
 *   dbg::dump(data);                        // 十六进制转储
 * @endcode
 *
 * 特性与约束：
 * - 等级在编译期生效：LOG_* 与 dump 按等级取舍，DBG_LEVEL=0 时连格式化器本身都不进镜像；
 *   直接调用 dbg::print 不受等级限制（显式调用即显式意图）。
 * - 发送走 `Uart::write()`（非阻塞入队）：因此可在 ISR 里调用。两种“截断”要分清：
 *   行太长（> DBG_LINE_SIZE-1）时返回值 = 应写长度，拿它可看出来；
 *   而发送环满时尾部字节会被静默丢弃，返回值反映不出来（调试日志宁可丢也不要堵主循环）。
 * - 未绑定串口时是空操作（仍会格式化到栈缓冲后丢弃——想要零开销请用 DBG_LEVEL=0）。
 * - 每条日志用 DBG_LINE_SIZE 字节的栈缓冲（默认 128B，含格式化器自身约 200B 栈峰值）；
 *   本工程链接脚本只预留 _Min_Stack_Size=1KB，别在很深的调用栈里打印长行。
 * - 名字刻意叫 print 而非 printf：避免与 ::printf 在 using-directive 下产生歧义，
 *   也和 libm 的 logf 不冲突。
 */

/// 绑定控制台串口；传 nullptr 解除绑定
void bind(Uart* u) noexcept;

/// 已绑定的控制台（未绑定为 nullptr）
Uart* console() noexcept;

/**
 * 格式化并发送一行；返回按格式“应写出”的长度（>= DBG_LINE_SIZE 即被截断）。
 * 带 printf 属性：格式串与实参不匹配时在编译期由 -Wformat 抓到。
 */
[[gnu::format(printf, 1, 2)]]
int print(const char* f, ...) noexcept;

/// 同样的实现，供已持有 va_list 的调用方使用
[[gnu::format(printf, 1, 0)]]
int vprint(const char* f, std::va_list ap) noexcept;

/**
 * 十六进制转储：每行 16 字节，空格分隔；用于调试二进制协议。
 * @param max 最多转储的字节数（超出部分忽略）
 */
void dump(std::span<const uint8_t> data, size_t max = 32) noexcept;

/// 编译期调试等级（DBG_LEVEL）
inline constexpr int kLevel = DBG_LEVEL;

}  // namespace stm32::dbg

/// 按等级编译期开关的日志宏：等级高于 DBG_LEVEL 的调用整体消失（零代码）
#define LOG_E(...)                            \
    do {                                      \
        if (::stm32::dbg::kLevel >= 1) {      \
            ::stm32::dbg::print(__VA_ARGS__); \
        }                                     \
    } while (0)
#define LOG_W(...)                            \
    do {                                      \
        if (::stm32::dbg::kLevel >= 2) {      \
            ::stm32::dbg::print(__VA_ARGS__); \
        }                                     \
    } while (0)
#define LOG_I(...)                            \
    do {                                      \
        if (::stm32::dbg::kLevel >= 3) {      \
            ::stm32::dbg::print(__VA_ARGS__); \
        }                                     \
    } while (0)
#define LOG_V(...)                            \
    do {                                      \
        if (::stm32::dbg::kLevel >= 4) {      \
            ::stm32::dbg::print(__VA_ARGS__); \
        }                                     \
    } while (0)
