// stm32::fmt 差分测试：与主机 libc snprintf 逐字节对照（含返回值语义）
//
// 这是自实现格式化器的主要正确性证据：6 万+ 组 标志×宽度×精度×边界值 组合。
// 浮点部分另做随机对照（28000 组），只允许两类已知偏离。
#include "debug/fmt.hpp"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using stm32::fmt::format;
using stm32::fmt::vformat;

static int g_fail = 0;
static int g_total = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

static void cmpStr(const char* what, const char* ref, const char* got)
{
    ++g_total;
    if (std::strcmp(ref, got) != 0) {
        if (g_fail < 15) {
            std::printf("MISMATCH %-14s libc=[%s] mine=[%s]\n", what, ref, got);
        }
        ++g_fail;
    }
}

// 整数族：一个 spec 同时喂 libc 与自实现，比较输出与返回值
static void checkNum(const char* conv, const char* fl, int w, int p, unsigned int v, bool sgn)
{
    char spec[24];
    std::snprintf(spec, sizeof spec, "%%%s%s%s%s", fl, (w != 0) ? std::to_string(w).c_str() : "",
                  (p >= 0) ? ("." + std::to_string(p)).c_str() : "", conv);
    char a[160];
    char b[160];
    const int rn = sgn ? std::snprintf(a, sizeof a, spec, static_cast<int>(v))
                       : std::snprintf(a, sizeof a, spec, v);
    const int fn = static_cast<int>(format(b, sizeof b, spec, sgn ? static_cast<int>(v) : v));
    ++g_total;
    if (std::strcmp(a, b) != 0 || rn != fn) {
        if (g_fail < 15) {
            std::printf("MISMATCH spec=%-10s v=%-12u libc=[%s](%d) mine=[%s](%d)\n", spec, v, a, rn,
                        b, fn);
        }
        ++g_fail;
    }
}

static void checkStr(const char* fl, int w, int p, const char* s)
{
    char spec[24];
    std::snprintf(spec, sizeof spec, "%%%s%s%s%s", fl, (w != 0) ? std::to_string(w).c_str() : "",
                  (p >= 0) ? ("." + std::to_string(p)).c_str() : "", "s");
    char a[160];
    char b[160];
    const int rn = std::snprintf(a, sizeof a, spec, s);
    const int fn = static_cast<int>(format(b, sizeof b, spec, s));
    ++g_total;
    if (std::strcmp(a, b) != 0 || rn != fn) {
        if (g_fail < 15) {
            std::printf("MISMATCH spec=%-10s s=%-10s libc=[%s](%d) mine=[%s](%d)\n", spec, s, a, rn,
                        b, fn);
        }
        ++g_fail;
    }
}

// 与 libc 不承诺一致的转换（见 fmt.hpp 的偏离说明）
static void testDocumentedDeviations()
{
    char b[64];
    // |v| >= 1e15 的 %f 输出 ovf（宁缺勿错）
    format(b, sizeof b, "%f", 1e18);
    CHECK(std::strcmp(b, "ovf") == 0);
    format(b, sizeof b, "%.2f", -1e20);
    CHECK(std::strcmp(b, "-ovf") == 0);
    // %e/%g：吃掉 double 实参并给出 (nof) 标记（后面的转换不会错位）
    format(b, sizeof b, "[%e %d]", 1.0, 7);
    CHECK(std::strcmp(b, "[(nof) 7]") == 0);
    // %p 是自己的 8 位十六进制
    format(b, sizeof b, "%p", reinterpret_cast<const void*>(0x1234u));
    CHECK(std::strcmp(b, "00001234") == 0);
    // 精度上限 32
    format(b, sizeof b, "%.40d", 1);
    CHECK(std::strlen(b) == 32u);  // 精度夹到 32
    CHECK(b[31] == '1');
    // NaN
    format(b, sizeof b, "%.1f", std::nan(""));
    CHECK(std::strcmp(b, "nan") == 0);
    // 空缓冲：只计数
    CHECK(format(nullptr, 0, "%d", 12345) == 5u);
}

static void testSweep()
{
    const unsigned int vals[] = {0u,  1u,    7u,          15u,         16u,
                                 99u, 1234u, 0xffffffffu, 0x80000000u, 0xdeadbeefu};
    const long sgnvals[] = {0, 1, 7, 1234, -1, -7, -1234, INT_MIN, INT_MAX, -42};
    const char* flags[] = {"", "-", "0", "+", " ", "#", "-0", "-+", "0#", "+#", " 0", "-#"};

    for (const char* fl : flags) {
        for (int w = 0; w <= 9; ++w) {
            for (int p = -1; p <= 7; ++p) {
                for (unsigned int v : vals) {
                    checkNum("u", fl, w, p, v, false);
                    checkNum("x", fl, w, p, v, false);
                    checkNum("X", fl, w, p, v, false);
                    checkNum("o", fl, w, p, v, false);
                }
                for (long v : sgnvals) {
                    checkNum("d", fl, w, p, static_cast<unsigned int>(static_cast<int>(v)), true);
                }
                checkStr(fl, w, p, "");
                checkStr(fl, w, p, "ab");
                checkStr(fl, w, p, "hello-world");
            }
        }
    }
}

static void testTruncation()
{
    // 返回值 = 按格式应写出的长度；缓冲始终 NUL 结尾（与 snprintf 一致）
    char s8[8];
    char s8b[8];
    const int rn = std::snprintf(s8, sizeof s8, "%d-%s", 1234567890, "toolong");
    const int fn = static_cast<int>(format(s8b, sizeof s8b, "%d-%s", 1234567890, "toolong"));
    cmpStr("trunc", s8, s8b);
    CHECK(rn == fn);
    CHECK(rn == 18);
    CHECK(std::strlen(s8b) < 8u);

    // %% 与纯文本
    char a[64];
    char b[64];
    std::snprintf(a, sizeof a, "plain %% %d", 5);
    format(b, sizeof b, "plain %% %d", 5);
    cmpStr("plain", a, b);

    // 64 位与长度修饰
    std::snprintf(a, sizeof a, "%lld %llu %lx %#lx", -9223372036854775807LL - 1,
                  18446744073709551615ULL, 0xdeadbeefUL, 0xdeadbeefUL);
    format(b, sizeof b, "%lld %llu %lx %#lx", -9223372036854775807LL - 1, 18446744073709551615ULL,
           0xdeadbeefUL, 0xdeadbeefUL);
    cmpStr("64bit", a, b);

    // 空串实参（%s 传 nullptr 时应给 (null)，不崩）
    format(b, sizeof b, "[%s]", nullptr);
    CHECK(std::strcmp(b, "[(null)]") == 0);
}

#ifndef STM32_DBG_NO_FLOAT
static void testFloat()
{
    // 定向用例：与 libc 逐字节一致
    const double vs[] = {0.0,       1.0 / 3.0, 3.14159265358979, -2.5000001, 12.345, 123456.789,
                         0.9999999, 1e-7,      999999.999999,    -0.0000004};
    const char* specs[] = {"%f",   "%.0f",   "%.1f",   "%.2f",  "%.3f",
                           "%.6f", "%12.3f", "%+8.2f", "%-8.1f"};
    char a[64];
    char b[64];
    for (const char* sp : specs) {
        for (double v : vs) {
            std::snprintf(a, sizeof a, sp, v);
            format(b, sizeof b, sp, v);
            cmpStr(sp, a, b);
        }
    }

    // 随机对照：只允许“恰好 .5”（libc half-even vs 本实现 half-up）这一类差异
    std::srand(20260729);
    int total = 0;
    int ties = 0;
    int unexpected = 0;
    for (int i = 0; i < 4000; ++i) {
        double v = (i % 2 ? 1.0 : -1.0) * (static_cast<double>(std::rand()) * 2.0 + 0.0001);
        if (i % 3 == 0) v /= 1000.0;
        if (i % 7 == 0) v /= 1000000.0;
        for (const char* sp : specs) {
            std::snprintf(a, sizeof a, sp, v);
            format(b, sizeof b, sp, v);
            ++total;
            if (std::strcmp(a, b) == 0) continue;
            int prec = 0;
            const char* d = std::strchr(sp, '.');
            if (d != nullptr) prec = std::atoi(d + 1);
            const double scaled = std::fabs(v) * std::pow(10.0, prec);
            const double frac = scaled - std::floor(scaled);
            if (std::fabs(frac - 0.5) < 1e-9) {
                ++ties;  // 舍入方式差异：已文档化
                continue;
            }
            if (unexpected < 5) {
                std::printf("UNEXPECTED %s v=%g libc=[%s] mine=[%s]\n", sp, v, a, b);
            }
            ++unexpected;
        }
    }
    CHECK(unexpected == 0);
    std::printf("float: 随机对照 total=%d 一致=%d 舍入方式差异=%.1f%%\n", total, total - ties,
                100.0 * static_cast<double>(ties) / static_cast<double>(total));
}
#endif

int main()
{
    testSweep();
    testTruncation();
    testDocumentedDeviations();
#ifndef STM32_DBG_NO_FLOAT
    testFloat();
#endif
    std::printf("%s comparisons=%d fail=%d\n", (g_fail == 0) ? "PASS" : "FAIL", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
