/*!
 * \file dbg.cpp
 * \brief 调试打印层：绑定控制台串口 + 自实现格式化发送（不经 newlib stdio）。
 */
#include "dbg/dbg.hpp"

#include <cstdarg>
#include <cstddef>
#include <span>

#include "dbg/fmt.hpp"

namespace stm32::dbg
{
namespace
{
Uart* g_console = nullptr;
constexpr size_t kLineSize = DBG_LINE_SIZE;
constexpr size_t kDumpBytesPerRow = 16;  // dump 每行字节数：16 * "xx " + 前缀 10 + NUL = 59B
}  // namespace

void bind(Uart* u) noexcept
{
    g_console = u;
}

Uart* console() noexcept
{
    return g_console;
}

int vprint(const char* f, std::va_list ap) noexcept
{
    char buf[kLineSize];
    const size_t n = fmt::vformat(buf, sizeof buf, f, ap);
    if (g_console == nullptr) {
        return static_cast<int>(n);
    }
    const size_t len = (n >= sizeof buf) ? (sizeof buf - 1u) : n;
    g_console->write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf), len));
    return static_cast<int>(n);
}

int print(const char* f, ...) noexcept
{
    std::va_list ap;
    va_start(ap, f);
    const int n = vprint(f, ap);
    va_end(ap);
    return n;
}

void dump(std::span<const uint8_t> data, size_t max) noexcept
{
    if constexpr (kLevel < 1) {  // 关掉等级时整条路径编译掉
        (void) data;
        (void) max;
        return;
    }
    if (g_console == nullptr) {
        return;
    }
    const size_t n = (data.size() < max) ? data.size() : max;
    const uint8_t* bytes =
            data.data();  // 不用 span::operator[]：它的越界检查会引入 libstdc++ 断言桩
    char buf[kLineSize];
    size_t i = 0;
    while (i < n) {
        const size_t row_end = (n < i + kDumpBytesPerRow) ? n : i + kDumpBytesPerRow;
        size_t used = fmt::format(buf, sizeof buf, "%08x: ", static_cast<unsigned>(i));
        for (size_t k = i; k < row_end; ++k) {
            // 写满后仍继续计数（用于判断截断），但指针/容量始终留在缓冲内
            const size_t cap = (used < sizeof buf) ? (sizeof buf - used) : 1u;
            char* at = (used < sizeof buf) ? (buf + used) : (buf + sizeof buf - 1u);
            used += fmt::format(at, cap, "%02x ", bytes[k]);
        }
        if (used < sizeof buf) {
            buf[used] = '\n';
            ++used;
        }
        g_console->write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf), used));
        i = row_end;
    }
}

}  // namespace stm32::dbg
