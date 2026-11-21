/*!
 * \file fmt.cpp
 * \brief 极简 printf 子集格式化：无堆、无 stdio、无 locale。
 *
 * 语义与 libc snprintf 按差分测试逐字节对齐（tests/host_uart/fmt_test.cpp，6.1 万组组合）。
 * 支持的子集与有意偏离见 debug/fmt.hpp 的说明。
 */
#include "dbg/fmt.hpp"

#include <cmath>
#include <cstdarg>
#include <cstring>

namespace stm32::fmt
{
namespace
{
constexpr int kMaxPrec = 32;          // 数字精度上限（防止 %.100d 撑爆局部缓冲）
constexpr int kNumBuf = 24;           // 数字缓冲：64 位十进制最多 20 位，留余量
constexpr int kBodyBuf = 40;          // 正文缓冲：kMaxPrec 位数字 + 0x 前缀 + 移位余量
constexpr int kFloatIntBuf = 12;      // 小数部分缓冲：10^9 共 9 位，留余量
constexpr double kMaxFloat = 1e15;    // %f 可覆盖的定点范围（超出输出 ovf）
constexpr int kMaxFloatPrec = 9;      // %f 小数位上限（受 kPow10 表长度限制）
constexpr int kDefaultFloatPrec = 6;  // %f 缺省小数位（同 C 标准）

struct Buf {
    char* p;
    char* const end;  // 最后一个可写字节的后一位置（已为 NUL 预留）
    size_t n;

    void put(char c) noexcept
    {
        if (p != end) {
            *p++ = c;
        }
        ++n;
    }
    void write(const char* s, size_t len) noexcept
    {
        for (size_t i = 0; i < len; ++i) {
            put(s[i]);
        }
    }
};

// 把已经渲染好的正文（不含符号/空白）按宽度与对齐落到缓冲区
void place(Buf& b, const char* pfx, int plen, const char* body, int bl, bool neg, char sign,
           int width, char pad, bool left) noexcept
{
    // 没有正文时（如 %.0d 的 0）不补 0；'0' 标志本身对 %c/%s 无意义
    if (bl == 0) {
        pad = ' ';
    }
    const int signw = (neg || sign != '\0') ? 1 : 0;
    const int fill = (width > bl + plen + signw) ? (width - bl - plen - signw) : 0;
    auto put_sign = [&](void) noexcept {
        if (neg) {
            b.put('-');
        } else if (sign != '\0') {
            b.put(sign);
        }
    };
    if (!left && pad == '0') {  // 0 填充：符号、0x 前缀在前，其后补 0
        put_sign();
        b.write(pfx, static_cast<size_t>(plen));
        for (int i = 0; i < fill; ++i) {
            b.put('0');
        }
        b.write(body, static_cast<size_t>(bl));
    } else {  // 空格填充：右对齐空白在符号前；左对齐空白在正文后
        if (!left) {
            for (int i = 0; i < fill; ++i) {
                b.put(' ');
            }
        }
        put_sign();
        b.write(pfx, static_cast<size_t>(plen));
        b.write(body, static_cast<size_t>(bl));
        if (left) {
            for (int i = 0; i < fill; ++i) {
                b.put(' ');
            }
        }
    }
}

// rev：低位在前的数字序列
void emitNum(Buf& b, const char* rev, int rev_len, int base, bool upper, bool neg, char sign,
             int width, int prec, char pad, bool left, bool alt) noexcept
{
    if (prec > kMaxPrec) {
        prec = kMaxPrec;
    }
    // C 规定：整数转换给了显式精度时忽略 '0' 标志
    if (prec >= 0) {
        pad = ' ';
    }
    char pfx[2];
    int plen = 0;
    char body[kBodyBuf];
    int bl = 0;
    const bool zero = (rev_len == 1 && rev[0] == '0');
    int digits = (prec > rev_len) ? prec : rev_len;
    if (prec == 0 && zero) {
        digits = 0;  // C 标准：精度 0 且值为 0 时不输出数字（%.0d -> ""）
    }
    if (alt && base == 16 && !zero && digits != 0) {  // 0x / 0X（值为 0 时不加前缀）
        pfx[plen++] = '0';
        pfx[plen++] = upper ? 'X' : 'x';
    }
    for (int i = digits - rev_len; i > 0; --i) {
        body[bl++] = '0';
    }
    if (digits != 0) {
        for (int i = rev_len - 1; i >= 0; --i) {
            body[bl++] = rev[i];
        }
    }
    // 八进制 #：强制首位为 0（首位已是 0 则不重复补；数字被精度抹掉时补回一个 0）
    if (alt && base == 8 && (bl == 0 || body[0] != '0')) {
        for (int i = bl; i > 0; --i) {
            body[i] = body[i - 1];
        }
        body[0] = '0';
        ++bl;
    }
    place(b, pfx, plen, body, bl, neg, sign, width, pad, left);
}

char toDigits(uint64_t v, int base, bool upper, char* out) noexcept
{
    const char* map = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;
    if (v == 0u) {
        out[len++] = '0';
    }
    while (v != 0u) {
        out[len++] = map[static_cast<size_t>(v % static_cast<uint64_t>(base))];
        v = v / static_cast<uint64_t>(base);
    }
    return static_cast<char>(len);
}

#ifndef STM32_DBG_NO_FLOAT
constexpr uint32_t kPow10[] = {1u,      10u,      100u,      1000u,      10000u,
                               100000u, 1000000u, 10000000u, 100000000u, 1000000000u};

void emitFloat(Buf& b, double v, int width, int prec, char pad, bool left, bool plus,
               bool space) noexcept
{
    if (v != v) {  // NaN
        place(b, nullptr, 0, "nan", 3, false, '\0', width, pad, left);
        return;
    }
    const bool neg = std::signbit(v);  // 也覆盖 -0.0
    if (v < 0.0) {
        v = -v;
    }
    if (v >= kMaxFloat) {  // 超出定点范围：宁可标记溢出，也不输出错数字
        place(b, nullptr, 0, "ovf", 3, neg, '\0', width, pad, left);
        return;
    }
    if (prec < 0) {
        prec = kDefaultFloatPrec;
    }
    if (prec > kMaxFloatPrec) {
        prec = kMaxFloatPrec;
    }
    const uint32_t scale = kPow10[prec];
    uint64_t ip = static_cast<uint64_t>(v);
    uint32_t fp = static_cast<uint32_t>((v - static_cast<double>(ip)) * static_cast<double>(scale) +
                                        0.5);  // 四舍五入(half-up)
    if (fp == scale) {                         // 进位：0.999... -> 1.000...
        fp = 0;
        ip = ip + 1u;
    }
    char body[kBodyBuf];
    int bl = 0;
    char dig[kNumBuf];
    const char dl = toDigits(ip, 10, false, dig);
    for (int i = dl - 1; i >= 0; --i) {
        body[bl++] = dig[i];
    }
    if (prec != 0) {
        body[bl++] = '.';
        char fd[kFloatIntBuf];
        char fl = toDigits(fp, 10, false, fd);
        for (int i = prec - fl; i > 0; --i) {
            body[bl++] = '0';
        }
        for (int i = fl - 1; i >= 0; --i) {
            body[bl++] = fd[i];
        }
    }
    place(b, nullptr, 0, body, bl, neg, plus ? '+' : (space && !neg ? ' ' : '\0'), width, pad,
          left);
}
#endif
}  // namespace

size_t vformat(char* dst, size_t cap, const char* f, std::va_list ap) noexcept
{
    // cap==0 或 dst==nullptr：与 snprintf 一样只数长度（p==end 时 put 只计数不落盘）
    const bool writable = (dst != nullptr) && (cap != 0u);
    Buf b{writable ? dst : nullptr, writable ? (dst + cap - 1u) : nullptr, 0};

    for (; *f != '\0'; ++f) {
        if (*f != '%') {
            b.put(*f);
            continue;
        }
        ++f;
        if (*f == '%') {
            b.put('%');
            continue;
        }

        bool left = false;
        bool plus = false;
        bool space = false;
        bool alt = false;
        char pad = ' ';
        for (;; ++f) {
            const char c = *f;
            if (c == '-') {
                left = true;
            } else if (c == '0') {
                if (!left) {
                    pad = '0';
                }
            } else if (c == '+') {
                plus = true;
            } else if (c == ' ') {
                space = true;
            } else if (c == '#') {
                alt = true;
            } else {
                break;
            }
        }

        int width = 0;
        if (*f == '*') {
            width = va_arg(ap, int);
            if (width < 0) {  // 负宽度 = 左对齐
                left = true;
                width = -width;
            }
            ++f;
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f++ - '0');
        }

        int prec = -1;
        if (*f == '.') {
            ++f;
            prec = 0;
            if (*f == '*') {
                prec = va_arg(ap, int);
                ++f;
            }
            while (*f >= '0' && *f <= '9') {
                prec = prec * 10 + (*f++ - '0');
            }
        }

        int len = 0;  // 0=int/h/z/j/t, 1=long, 2=long long
        for (;; ++f) {
            if (*f == 'l') {
                len = (len < 1) ? 1 : 2;
            } else if (*f != 'h' && *f != 'z' && *f != 'j' && *f != 't') {
                break;
            }
        }

        const char spec = *f;
        switch (spec) {
        case 'd':
        case 'i': {
            const long long v = (len == 2) ? va_arg(ap, long long)
                                           : (len == 1 ? static_cast<long long>(va_arg(ap, long))
                                                       : static_cast<long long>(va_arg(ap, int)));
            char dig[kNumBuf];
            const bool neg = (v < 0);
            const char dl = toDigits(neg ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v), 10,
                                     false, dig);
            emitNum(b, dig, dl, 10, false, neg, plus ? '+' : (space && !neg ? ' ' : '\0'), width,
                    prec, pad, left, false);
            break;
        }
        case 'u': {
            const uint64_t v =
                    (len == 2) ? va_arg(ap, unsigned long long)
                               : (len == 1 ? static_cast<uint64_t>(va_arg(ap, unsigned long))
                                           : static_cast<uint64_t>(va_arg(ap, unsigned)));
            char dig[kNumBuf];
            const char dl = toDigits(v, 10, false, dig);
            // C 规定：无符号转换忽略 + / 空格 标志
            emitNum(b, dig, dl, 10, false, false, '\0', width, prec, pad, left, false);
            break;
        }
        case 'x':
        case 'X': {
            const bool upper = (spec == 'X');
            const uint64_t v =
                    (len == 2) ? va_arg(ap, unsigned long long)
                               : (len == 1 ? static_cast<uint64_t>(va_arg(ap, unsigned long))
                                           : static_cast<uint64_t>(va_arg(ap, unsigned)));
            char dig[kNumBuf];
            const char dl = toDigits(v, 16, upper, dig);
            emitNum(b, dig, dl, 16, upper, false, '\0', width, prec, pad, left, alt);
            break;
        }
        case 'o': {
            const uint64_t v = (len == 2) ? va_arg(ap, unsigned long long)
                                          : static_cast<uint64_t>(va_arg(ap, unsigned));
            char dig[kNumBuf];
            const char dl = toDigits(v, 8, false, dig);
            emitNum(b, dig, dl, 8, false, false, '\0', width, prec, pad, left, alt);
            break;
        }
        case 'p': {
            const void* v = va_arg(ap, const void*);
            char dig[kNumBuf];
            const char dl =
                    toDigits(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(v)), 16, false, dig);
            emitNum(b, dig, dl, 16, false, false, '\0', (width != 0) ? width : 8, 8, pad, left,
                    false);
            break;
        }
        case 'c': {
            const char c = static_cast<char>(va_arg(ap, int));
            place(b, nullptr, 0, &c, 1, false, '\0', width, ' ', left);  // '0' 标志对 %c 无效
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (s == nullptr) {
                s = "(null)";
            }
            size_t l = std::strlen(s);
            if (prec >= 0 && static_cast<size_t>(prec) < l) {
                l = static_cast<size_t>(prec);
            }
            place(b, nullptr, 0, s, static_cast<int>(l), false, '\0', width, ' ',
                  left);  // '0' 标志对 %s 无效
            break;
        }
#ifndef STM32_DBG_NO_FLOAT
        case 'f':
        case 'F':
            emitFloat(b, va_arg(ap, double), width, prec, pad, left, plus, space);
            break;
#else
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            (void) va_arg(ap, double);  // 吃掉实参，后面的转换才不会错位
            b.write("(nof)", 5);        // 显式标记：本次编译没开浮点支持
            break;
        }
#endif
        default: {
            // 需要 double 的浮点转换（%e %g %a）：即便没编浮点支持也要吃掉实参，
            // 否则后面的转换会取错参数；输出用 (nof) 显式标记，比原样吐回更难被看漏
            if (spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G' || spec == 'a' ||
                spec == 'A') {
                (void) va_arg(ap, double);
                b.write("(nof)", 5);
            } else {  // %n 之类不认识的东西：原样吐回，便于发现写错的格式串
                b.put('%');
                b.put(spec);
            }
            break;
        }
        }
    }

    if (writable) {
        *b.p = '\0';
    }
    return b.n;
}
}  // namespace stm32::fmt
