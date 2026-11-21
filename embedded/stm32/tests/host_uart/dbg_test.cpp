// 主机端测试：stm32::dbg 调试打印层（真 dbg.cpp + fmt.cpp + uart.cpp + mock HAL）
//
// 校验 dbg 层自身的语义：绑定/未绑定、发出的字节序列、超长行的截断语义、
// dump 的排版、等级宏在编译期的取舍、以及从 ISR（帧回调）里打印的安全性。
//
// 注意：mock 的整块复位只在构造 Uart 之前做一次；之后只清 SR/DR，
// 因为 CR1 里存着驱动使能的 UE/TE/RE/RXNEIE/IDLEIE，抹掉它们会让驱动收不到东西。
#include "debug/dbg.hpp"

#include <cstdio>
#include <cstring>
#include <span>

#include "peripheral/gpio.hpp"
#include "peripheral/uart.hpp"
#include "system/system.hpp"

using stm32::Uart;
using stm32::dbg::bind;
using stm32::dbg::console;
using stm32::dbg::dump;
using stm32::dbg::kLevel;
using stm32::dbg::print;
using stm32::detail::uartIrqDispatch;

// ── System 主机实现（panic 用 throw，便于断言错误路径）──
static long long g_now_ms = 0;
namespace stm32
{
bool System::init()
{
    return true;
}
void System::delay(std::chrono::milliseconds ms)
{
    g_now_ms += ms.count();
}
std::chrono::milliseconds System::now()
{
    return std::chrono::milliseconds(g_now_ms++);
}
void System::panic(uint32_t code)
{
    throw code;
}
}  // namespace stm32

static int g_fail = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

static void resetMock()
{
    std::memset(g_usart, 0, sizeof(g_usart));
    std::memset(g_dmach, 0, sizeof(g_dmach));
    std::memset(g_clk_en, 0, sizeof(g_clk_en));
    std::memset(g_clk_dis, 0, sizeof(g_clk_dis));
    std::memset(g_irq_enabled, 0, sizeof(g_irq_enabled));
    std::memset(g_irq_disabled, 0, sizeof(g_irq_disabled));
    std::memset(g_gpio_init, 0, sizeof(g_gpio_init));
}

/// 清标志位（保留 CR1/CR3：那是驱动自己使能的中断源）
static void clearFlags()
{
    g_usart[0].SR = 0;
    g_usart[0].DR = 0;
}

/// 纯 IT 配置：发送由 fastIsr 逐字节搬走，测试可精确取出每个字节
static Uart::Config makeCfgIt()
{
    Uart::Config c;
    c.rx = Uart::RxPath::It;
    c.tx = Uart::TxPath::It;
    return c;
}

/// 把发送环里的字节全部取出（每个 TXE 中断搬走一字节）
static int drain(Uart& u, USART_TypeDef* r, char* out, int cap)
{
    int n = 0;
    int guard = 0;
    while (u.txPending() != 0u && ++guard < 4096) {
        r->SR = USART_SR_TXE;
        uartIrqDispatch(0);
        if (n < cap) {
            out[n++] = static_cast<char>(r->DR & 0xFFu);
        }
    }
    if (n < cap) {
        out[n] = '\0';
    }
    return n;
}

// 未绑定串口：仍返回“按格式应写出的长度”，且不崩
static void testUnbound()
{
    bind(nullptr);
    CHECK(console() == nullptr);
    CHECK(print("hello %d\n", 42) == 9);
}

// 绑定后：字节序列与格式串完全一致
static void testPrintExact(Uart& u)
{
    clearFlags();
    bind(&u);
    CHECK(console() == &u);

    char out[256];
    const int want = print("n=%d s=%s x=%#x c=%c\n", -42, "ab", 0xabcu, 'z');
    const int got = drain(u, &g_usart[0], out, static_cast<int>(sizeof out));
    CHECK(got == want);
    CHECK(std::strcmp(out, "n=-42 s=ab x=0xabc c=z\n") == 0);

    // 连续多条日志首尾相接（非阻塞入队的正常结果）
    print("a");
    print("b");
    CHECK(drain(u, &g_usart[0], out, static_cast<int>(sizeof out)) == 2);
    CHECK(std::strcmp(out, "ab") == 0);
}

// 超长行：返回“应写出的长度”，实际只写 DBG_LINE_SIZE-1 字节
static void testTruncation(Uart& u)
{
    clearFlags();
    bind(&u);

    char big[400];
    std::memset(big, 'u', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    const int want = print("%s", big);
    CHECK(want == static_cast<int>(sizeof big) - 1);  // 应写出的长度（同 snprintf 返回值）

    char out[DBG_LINE_SIZE + 8];
    const int got = drain(u, &g_usart[0], out, static_cast<int>(sizeof out));
    CHECK(got == DBG_LINE_SIZE - 1);  // 实际落盘的字节数
    CHECK(out[0] == 'u');
    CHECK(out[DBG_LINE_SIZE - 2] == 'u');
}

// dump：每行 16 字节，8 位十六进制偏移前缀
static void testDump(Uart& u)
{
    clearFlags();
    bind(&u);

    uint8_t data[20];
    for (int i = 0; i < 20; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }
    dump(std::span<const uint8_t>(data, sizeof data), 32);

    if (kLevel < 1) {  // 等级关闭时 dump 整条编译掉，不该有任何输出
        CHECK(u.txPending() == 0u);
        return;
    }
    char out[512];
    const int got = drain(u, &g_usart[0], out, static_cast<int>(sizeof out));
    const char* expect =
            "00000000: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f \n"
            "00000010: 10 11 12 13 \n";
    CHECK(got == static_cast<int>(std::strlen(expect)));
    CHECK(std::strcmp(out, expect) == 0);

    // max 限制：只转储前 2 字节
    clearFlags();
    dump(std::span<const uint8_t>(data, sizeof data), 2);
    const int got2 = drain(u, &g_usart[0], out, static_cast<int>(sizeof out));
    CHECK(std::strcmp(out, "00000000: 00 01 \n") == 0);
    CHECK(got2 == static_cast<int>(std::strlen("00000000: 00 01 \n")));
}

// 等级宏：kLevel 是编译期常量；这里按当前编译等级校验实际出现的输出
static int g_cb_calls = 0;
static void onFrame(std::span<const uint8_t> data, void*)
{
    ++g_cb_calls;
    print("rx %u\n", static_cast<unsigned>(data.size()));
}

static void testLevels(Uart& u)
{
    clearFlags();
    bind(&u);
    CHECK(kLevel == DBG_LEVEL);

    char out[256];
    LOG_E("E\n");
    LOG_W("W\n");
    LOG_I("I\n");
    LOG_V("V\n");
    const int got = drain(u, &g_usart[0], out, static_cast<int>(sizeof out));
    if (kLevel >= 4) {
        CHECK(std::strcmp(out, "E\nW\nI\nV\n") == 0);
        CHECK(got == 8);
    } else if (kLevel >= 3) {
        CHECK(std::strcmp(out, "E\nW\nI\n") == 0);
        CHECK(got == 6);
    } else if (kLevel >= 2) {
        CHECK(std::strcmp(out, "E\nW\n") == 0);
        CHECK(got == 4);
    } else if (kLevel >= 1) {
        CHECK(std::strcmp(out, "E\n") == 0);
        CHECK(got == 2);
    } else {
        CHECK(got == 0);
    }
}

// 帧回调里打印：ISR 上下文走非阻塞入队，不会死等
static void testFromIsr(Uart& u)
{
    clearFlags();
    bind(&u);
    u.onRx(onFrame, nullptr);
    g_cb_calls = 0;

    USART_TypeDef* r = &g_usart[0];
    CHECK((r->CR1 & USART_CR1_RXNEIE) != 0u);  // 纯 IT 接收应当已使能 RXNE+IDLE 中断
    for (int i = 0; i < 3; ++i) {
        r->SR = USART_SR_RXNE;
        r->DR = static_cast<uint16_t>(0x41 + i);
        uartIrqDispatch(0);
    }
    r->SR = USART_SR_IDLE;  // 空闲线 = 一帧结束
    uartIrqDispatch(0);

    CHECK(g_cb_calls == 1);
    char out[64];
    const int got = drain(u, r, out, static_cast<int>(sizeof out));
    CHECK(std::strcmp(out, "rx 3\n") == 0);
    CHECK(got == static_cast<int>(std::strlen("rx 3\n")));

    u.onRx(nullptr, nullptr);
}

int main()
{
    resetMock();
    testUnbound();  // 不碰寄存器，可以先跑

    Uart u(Uart::Inst::Usart1, makeCfgIt());  // 单一实例：同一 USART 只允许一个 Uart 对象
    testPrintExact(u);
    testTruncation(u);
    testDump(u);
    testLevels(u);
    testFromIsr(u);

    std::printf("%s fail=%d\n", (g_fail == 0) ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
