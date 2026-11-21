#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace stm32::fmt
{

/**
 * 极简 printf 子集格式化：无堆、无 locale、无 stdio、无递归。
 *
 * 与 libc `snprintf` 的语义按差分测试逐字节对齐（见 tests/host_uart 里的
 * fmt_test：6.1 万组组合全部一致），支持的子集为：
 *
 * - 转换：`%d %i %u %x %X %o %c %s %%`，以及 `%f`（定点，可用 STM32_DBG_NO_FLOAT 关掉）
 * - 标志：`-` `0` `+` 空格 `#`
 * - 宽度/精度：数字或 `*`（`*` 取 int 实参；负宽度=左对齐）
 * - 长度修饰：`h hh l ll z j t`（按 AAPCS 提升规则取参；h/hh 按 int 处理）
 *
 * 有意的偏离（都不影响调试用途，但别指望和 libc 一致）：
 * - `%f` 采用四舍五入(half-up)，libc 是 half-even：恰好 `.5` 时个位不同
 * - `|%v| >= 1e15` 时 `%f` 输出 `ovf`（定点范围外，宁缺勿错）
 * - `%e %E %g %G %a %A` 不支持：会消费掉 double 实参（避免后面的转换取错参数），
 *   并输出 `(nof)` 标记；编译时定义 `STM32_DBG_NO_FLOAT` 时连 `%f` 也走这条路
 * - 数字精度上限 32（`%.40d` 被夹到 32 位数字）
 * - `%p` 输出 8 位十六进制，不承诺与 libc 的 `%p` 表示一致
 * - 其余不认识的转换（如 `%n`）原样吐回 `%x`，便于发现写错的格式串
 *
 * @param dst 输出缓冲（可为 nullptr，此时只计数）
 * @param cap dst 容量（含 NUL）；写满 cap-1 字节后丢弃其余，但始终补 NUL
 * @param f   格式串
 * @param ap  实参列表
 * @return 按格式“应写出”的长度；>= cap 即表示被截断（和 snprintf 的返回值语义一致）
 */
size_t vformat(char* dst, size_t cap, const char* f, std::va_list ap) noexcept;

/// vformat 的可变参包装
inline size_t format(char* dst, size_t cap, const char* f, ...) noexcept
{
    std::va_list ap;
    va_start(ap, f);
    const size_t n = vformat(dst, cap, f, ap);
    va_end(ap);
    return n;
}

}  // namespace stm32::fmt
