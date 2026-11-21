// 主机端仿真测试：验证 stm32::Uart 的环/帧/游标/分块/错误恢复逻辑
//
// 约定：测试自己跟踪 rx_pushed / rx_consumed，因此可精确推算环游标位置。
#include <cstdio>
#include <cstring>
#include <span>

#include "peripheral/gpio.hpp"
#include "peripheral/uart.hpp"
#include "system/system.hpp"

using stm32::Uart;
using stm32::detail::uartIrqDispatch;

// ── System 实现（主机）──
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

// ── 断言 ──
static int g_fail = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

// ── 回调记录 ──
static int g_frames = 0;
static int g_seen_len = 0;
static uint8_t g_seen[8192]{};
static int g_tx_idle_calls = 0;

static void onFrame(std::span<const uint8_t> s, void*)
{
    ++g_frames;
    if (g_seen_len + static_cast<int>(s.size()) <= static_cast<int>(sizeof(g_seen))) {
        std::memcpy(&g_seen[g_seen_len], s.data(), s.size());
        g_seen_len += static_cast<int>(s.size());
    }
}

static void onTxIdle(void*)
{
    ++g_tx_idle_calls;
}

static void reset()
{
    std::memset(g_usart, 0, sizeof(g_usart));
    std::memset(g_dmach, 0, sizeof(g_dmach));
    std::memset(g_clk_en, 0, sizeof(g_clk_en));
    std::memset(g_clk_dis, 0, sizeof(g_clk_dis));
    std::memset(g_irq_enabled, 0, sizeof(g_irq_enabled));
    std::memset(g_irq_disabled, 0, sizeof(g_irq_disabled));
    std::memset(g_gpio_init, 0, sizeof(g_gpio_init));
    g_remap = 0;
    g_uart_held[0] = g_uart_held[1] = g_uart_held[2] = nullptr;
    g_frames = 0;
    g_seen_len = 0;
    g_tx_idle_calls = 0;
    g_rx_arm_calls = 0;
    g_rx_last_buf = nullptr;
    g_rx_last_size = 0;
    g_tx_it_calls = 0;
    g_tx_dma_calls = 0;
    g_tx_last_ptr = nullptr;
    g_tx_last_len = 0;
    g_dma_inits = 0;
    g_dma_aborts = 0;
}

static Uart::Config makeCfg(Uart::RxPath rx, Uart::TxPath tx, uint32_t baud = 115200)
{
    Uart::Config c{};
    c.rx = rx;
    c.tx = tx;
    c.baud = baud;
    return c;
}

// ── IT 路径的字节级仿真辅助 ──
struct IrqSim {
    USART_TypeDef* r;
    uint8_t idx;

    void rx(const uint8_t* p, int n)
    {
        for (int i = 0; i < n; ++i) {
            r->SR = USART_SR_RXNE;
            r->DR = p[i];
            uartIrqDispatch(idx);
        }
    }
    void idle()
    {
        r->SR = USART_SR_IDLE;
        uartIrqDispatch(idx);
    }
    // 一帧：若干字节 + 空闲线
    void frame(const uint8_t* p, int n)
    {
        rx(p, n);
        idle();
    }
};

// 模拟 DMA 路径的一帧到达：填 DMA 缓冲 + HAL 状态 + RxEvent 回调
static void fireRxEvent(UART_HandleTypeDef* h, const uint8_t* src, uint16_t n, uint16_t pos)
{
    std::memcpy(h->pRxBuffPtr, src, n);
    h->RxState = HAL_UART_STATE_READY;  // HAL 在 IDLE/满 事件里已把状态回到 READY
    HAL_UARTEx_RxEventCallback(h, pos);
}

// ── 测试 1：纯 IT 路径（自管快速 ISR）──
static void testItMode()
{
    reset();
    Uart u(Uart::Inst::Usart2, makeCfg(Uart::RxPath::It, Uart::TxPath::It));
    USART_TypeDef* r = &g_usart[1];
    IrqSim irq{r, 1};

    // 中断使能位 + 引脚（USART2 默认 TX=PA2 复用推挽，RX=PA3 上拉输入）
    CHECK((r->CR1 & (USART_CR1_RXNEIE | USART_CR1_IDLEIE)) ==
          (USART_CR1_RXNEIE | USART_CR1_IDLEIE));
    CHECK((r->CR3 & USART_CR3_EIE) != 0u);
    CHECK(g_gpio_init[0][0] == 1 && g_gpio_init[0][1] == 1);
    CHECK(g_gpio_last_pin[0] == (1u << 3));
    CHECK(g_clk_en[1] >= 1 && g_clk_en[5] == 1);  // GPIOA + USART2 时钟

    u.onRx(onFrame);
    u.onTx(onTxIdle);

    // 一帧 5 字节
    irq.frame(reinterpret_cast<const uint8_t*>("ABCDE"), 5);
    CHECK(g_frames == 1 && g_seen_len == 5);
    CHECK(u.rxPending() == 5);
    uint8_t out[8]{};
    CHECK(u.read(std::span<uint8_t>(out, 8)) == 5);
    CHECK(std::memcmp(out, "ABCDE", 5) == 0);
    CHECK(u.rxPending() == 0);

    // 单字节帧推进游标，直到写游标停在环末尾前一格
    int pushed = 5;
    const uint8_t zero = 0x00;
    while (pushed % Uart::kRxSize != Uart::kRxSize - 1) {
        irq.frame(&zero, 1);
        u.discardRx();
        ++pushed;
    }
    CHECK(pushed % Uart::kRxSize == Uart::kRxSize - 1);
    CHECK(u.rxPending() == 0);
    g_frames = 0;
    g_seen_len = 0;

    // 3 字节帧跨环回绕 -> 拆两段交付，内容不变
    irq.frame(reinterpret_cast<const uint8_t*>("\xF0\xF1\xF2"), 3);
    CHECK(g_frames == 2);
    CHECK(g_seen_len == 3);
    CHECK(g_seen[0] == 0xF0 && g_seen[1] == 0xF1 && g_seen[2] == 0xF2);
    CHECK(u.rxPending() == 3);
    uint8_t w3[4]{};
    CHECK(u.read(std::span<uint8_t>(w3, 4)) == 3);

    // readByte：空环返回 false 且不写 out；有数据时写入 *out
    uint8_t probe = 0xA5;
    CHECK(!u.readByte(&probe));
    CHECK(probe == 0xA5);  // 空环时不得碰输出参数
    const uint8_t one = 0x77;
    irq.frame(&one, 1);
    CHECK(u.readByte(&probe));
    CHECK(probe == 0x77);
    CHECK(!u.readByte(&probe));  // 取完又变空

    // 环满：最新字节被丢弃并计数
    auto lost0 = u.getStats().rx_lost;
    uint8_t filler[Uart::kRxSize + 4];
    std::memset(filler, 0x5A, sizeof(filler));
    irq.rx(filler, static_cast<int>(sizeof(filler)));
    CHECK(u.rxPending() == Uart::kRxSize);
    CHECK(u.getStats().rx_lost == lost0 + 4);
    u.discardRx();

    // 错误计数（ORE：RXNE 未置位，只走清标志序列）
    r->SR = USART_SR_ORE;
    uartIrqDispatch(1);
    CHECK(u.getStats().err_ore == 1);
    r->SR = USART_SR_FE | USART_SR_RXNE;
    r->DR = 0x11;
    uartIrqDispatch(1);
    CHECK(u.getStats().err_fe == 1);
    CHECK(u.getStats().rx_bytes > 0);

    // ── 发送：IT 逐字节 ──
    CHECK((r->CR1 & USART_CR1_TXEIE) == 0u);
    CHECK(u.write("hello") == 5);
    CHECK((r->CR1 & USART_CR1_TXEIE) != 0u);
    uint8_t got[16]{};
    int n = 0;
    while ((r->CR1 & USART_CR1_TXEIE) != 0u && n < 16) {
        r->SR = USART_SR_TXE;
        uartIrqDispatch(1);
        got[n++] = static_cast<uint8_t>(r->DR & 0xFFu);
    }
    CHECK(std::memcmp(got, "hello", 5) == 0);
    // 环空后还会多进一次 TXE 中断（用来关 TXEIE），不写新字节 -> DR 重复上一次值
    CHECK(n == 6);
    CHECK(got[5] == got[4]);
    CHECK((r->CR1 & USART_CR1_TXEIE) == 0u);
    CHECK(g_tx_idle_calls == 1);
    CHECK(u.txPending() == 0);
    CHECK(u.busy() == false);
    CHECK(g_tx_it_calls == 0);  // 纯 IT 不使用 HAL_UART_Transmit_IT

    // 跨越发送环末尾
    const int len = Uart::kTxSize - 6;
    uint8_t big[Uart::kTxSize];
    for (int i = 0; i < len; ++i) {
        big[i] = static_cast<uint8_t>(0x30 + (i % 100));
    }
    CHECK(u.write(std::span<const uint8_t>(big, len)) == len);
    uint8_t back[Uart::kTxSize + 4]{};
    n = 0;
    while ((r->CR1 & USART_CR1_TXEIE) != 0u && n < Uart::kTxSize + 4) {
        r->SR = USART_SR_TXE;
        uartIrqDispatch(1);
        back[n++] = static_cast<uint8_t>(r->DR & 0xFFu);
    }
    CHECK(n == len + 1);
    CHECK(std::memcmp(back, big, len) == 0);

    // writeAll 与超时
    CHECK(u.writeAll(std::span<const uint8_t>(big, len)) == true);
    // 环满时带超时的 writeAll 必须退出（不无限阻塞）
    CHECK(u.writeAll(std::span<const uint8_t>(big, len), std::chrono::milliseconds(3)) == false);
    n = 0;
    while ((r->CR1 & USART_CR1_TXEIE) != 0u && n < 4 * Uart::kTxSize) {
        r->SR = USART_SR_TXE;
        uartIrqDispatch(1);
        ++n;
    }
    CHECK(u.txPending() == 0);

    // flush：TC 未置位时不算线上空闲
    r->SR &= ~USART_SR_TC;
    CHECK(u.flush(std::chrono::milliseconds(2)) == false);
    r->SR |= USART_SR_TC;
    CHECK(u.flush(std::chrono::milliseconds(2)) == true);
    u.flush();
}

// ── 测试 2：DMA + 空闲线（默认配置）──
static void testDmaIdle()
{
    reset();
    Uart u(Uart::Inst::Usart1);
    UART_HandleTypeDef* h = g_uart_held[0];
    CHECK(h != nullptr);
    CHECK(g_rx_arm_calls == 1 && g_rx_last_size == Uart::kFrameSize);
    CHECK(g_tx_it_calls == 0 && g_tx_dma_calls == 0);
    CHECK(g_dma_inits == 1);  // 只有接收 DMA
    u.onRx(onFrame);
    u.onTx(onTxIdle);

    // 一帧 4 字节
    fireRxEvent(h, reinterpret_cast<const uint8_t*>("ping"), 4, 4);
    CHECK(g_frames == 1 && g_seen_len == 4);
    CHECK(g_rx_arm_calls == 2);  // 已自动重装载
    CHECK(g_rx_last_size == Uart::kFrameSize);
    uint8_t out[8]{};
    CHECK(u.read(std::span<uint8_t>(out, 8)) == 4 && std::memcmp(out, "ping", 4) == 0);

    // 整缓冲满（pos == kFrameSize）
    uint8_t full[Uart::kFrameSize];
    for (int i = 0; i < Uart::kFrameSize; ++i) {
        full[i] = static_cast<uint8_t>(i);
    }
    fireRxEvent(h, full, Uart::kFrameSize, Uart::kFrameSize);
    CHECK(g_frames == 2 && g_seen_len == 4 + Uart::kFrameSize);
    CHECK(g_rx_arm_calls == 3);
    CHECK(u.rxPending() == Uart::kFrameSize);
    uint8_t back[Uart::kFrameSize];
    CHECK(u.read(std::span<uint8_t>(back, Uart::kFrameSize)) == Uart::kFrameSize);
    CHECK(std::memcmp(back, full, Uart::kFrameSize) == 0);

    // 空事件不产生回调，但仍重装载
    h->RxState = HAL_UART_STATE_READY;
    HAL_UARTEx_RxEventCallback(h, 0);
    CHECK(g_frames == 2);
    CHECK(g_rx_arm_calls == 4);

    // 连续多帧（模拟命令流）：每帧都重装载，内容顺序正确
    for (int f = 0; f < 5; ++f) {
        uint8_t fr[3] = {static_cast<uint8_t>(f), static_cast<uint8_t>(f + 1), 0xEE};
        fireRxEvent(h, fr, 3, 3);
    }
    CHECK(g_frames == 7 && g_seen_len == 4 + Uart::kFrameSize + 15);
    uint8_t seq[15]{};
    CHECK(u.read(std::span<uint8_t>(seq, 15)) == 15);
    CHECK(std::memcmp(seq, g_seen + 4 + Uart::kFrameSize, 15) == 0);
}

// ── 测试 3：DMA 发送的分块与续泵（含环回绕）──
static int g_tx_pushed = 0;  // 测试侧跟踪已入队总字节，用于推算环游标

// 把所有已入队数据"发完"：模拟 HAL 的 TC 中断 -> TxCplt 回调链
static void drainTx(Uart& u, UART_HandleTypeDef* h)
{
    int guard = 0;
    while (u.busy() && ++guard < 128) {
        h->gState = HAL_UART_STATE_READY;  // HAL 在 TC 中断里已把 gState 复位
        h->Instance->SR |= USART_SR_TC;
        HAL_UART_TxCpltCallback(h);
    }
    CHECK(u.txPending() == 0);
    CHECK(u.busy() == false);
}

static void testTxChunking()
{
    reset();
    g_tx_pushed = 0;
    Uart u(Uart::Inst::Usart3, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::Dma));
    UART_HandleTypeDef* h = g_uart_held[2];
    u.onTx(onTxIdle);
    CHECK(g_dma_inits == 2);  // rx + tx 两个 DMA
    CHECK(g_irq_enabled[DMA1_Channel3_IRQn] == 1 && g_irq_enabled[DMA1_Channel2_IRQn] == 1);
    CHECK(g_rx_arm_calls == 1);

    uint8_t data[300];
    for (int i = 0; i < 300; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }

    // 第一笔 200 字节：起点 0，单块
    CHECK(u.write(std::span<const uint8_t>(data, 200)) == 200);
    g_tx_pushed += 200;
    CHECK(g_tx_dma_calls == 1);
    CHECK(g_tx_last_len == 200);
    CHECK(std::memcmp(g_tx_last_ptr, data, 200) == 0);
    CHECK(g_tx_it_calls == 0);  // DMA 发送不使用 HAL_UART_Transmit_IT

    // 泵运行中追加：不得重复启动；环剩余 56 字节
    CHECK(u.write(std::span<const uint8_t>(data + 200, 100)) == Uart::kTxSize - 200);
    g_tx_pushed += Uart::kTxSize - 200;
    CHECK(g_tx_dma_calls == 1);
    CHECK(u.txPending() == Uart::kTxSize);

    // 第一块完成 -> 续泵到环末尾的 56 字节
    h->gState = HAL_UART_STATE_READY;
    HAL_UART_TxCpltCallback(h);
    CHECK(g_tx_dma_calls == 2);
    CHECK(g_tx_last_len == Uart::kTxSize - 200);
    CHECK(std::memcmp(g_tx_last_ptr, data + 200, Uart::kTxSize - 200) == 0);
    drainTx(u, h);
    CHECK(g_tx_idle_calls == 1);
    CHECK(g_tx_dma_calls == 2);

    // 游标此时 mod 256 == 0：再写 100 字节应是一整块
    CHECK(u.write(std::span<const uint8_t>(data + 200, 100)) == 100);
    g_tx_pushed += 100;
    CHECK(g_tx_dma_calls == 3);
    CHECK(g_tx_last_len == 100);
    CHECK(std::memcmp(g_tx_last_ptr, data + 200, 100) == 0);
    drainTx(u, h);
    CHECK(g_tx_idle_calls == 2);

    // 把游标推到环末尾前 6 字节（mod 256 == 250）
    const int pad = 250 - (g_tx_pushed % Uart::kTxSize);
    CHECK(pad > 0);
    CHECK(u.write(std::span<const uint8_t>(data, pad)) == static_cast<size_t>(pad));
    g_tx_pushed += pad;
    CHECK(g_tx_pushed % Uart::kTxSize == 250);
    drainTx(u, h);

    // 20 字节必须拆成 6 + 14 两块（环回绕）
    CHECK(u.write(std::span<const uint8_t>(data, 20)) == 20);
    g_tx_pushed += 20;
    CHECK(g_tx_dma_calls == 5);
    CHECK(g_tx_last_len == 6);
    CHECK(std::memcmp(g_tx_last_ptr, data, 6) == 0);
    h->gState = HAL_UART_STATE_READY;
    h->Instance->SR |= USART_SR_TC;
    HAL_UART_TxCpltCallback(h);
    CHECK(g_tx_dma_calls == 6);
    CHECK(g_tx_last_len == 14);
    CHECK(std::memcmp(g_tx_last_ptr, data + 6, 14) == 0);
    drainTx(u, h);

    // 超过环容量的写入会被截断（不阻塞、不越界）
    uint8_t huge[Uart::kTxSize * 2];
    std::memset(huge, 0x77, sizeof(huge));
    CHECK(u.write(std::span<const uint8_t>(huge, sizeof(huge))) == Uart::kTxSize);
    drainTx(u, h);

    // 错误中止自救：HAL 已空闲但没给 TxCplt 回调
    CHECK(u.write(static_cast<uint8_t>('!')) == 1);
    const int calls = g_tx_dma_calls;
    h->gState = HAL_UART_STATE_READY;  // 模拟错误路径复位状态而无回调
    CHECK(u.write(static_cast<uint8_t>('?')) == 1);
    CHECK(g_tx_dma_calls == calls + 1);  // kickTx -> txRescue 接手
    CHECK(g_tx_last_len == 1);
    CHECK(g_tx_last_ptr[0] == '?');
    h->gState = HAL_UART_STATE_READY;
    h->Instance->SR |= USART_SR_TC;
    HAL_UART_TxCpltCallback(h);
    CHECK(u.busy() == false);
    CHECK(u.flush(std::chrono::milliseconds(1)) == true);
}

// ── 测试 3b：接收用 DMA、发送用 IT 时，发送按块交给 HAL_UART_Transmit_IT ──
static void testTxHalIt()
{
    reset();
    Uart it(Uart::Inst::Usart1, makeCfg(Uart::RxPath::DmaIdle, Uart::TxPath::It));
    UART_HandleTypeDef* hi = g_uart_held[0];
    uint8_t data[30];
    for (int i = 0; i < 30; ++i) {
        data[i] = static_cast<uint8_t>(0x90 + i);
    }
    CHECK(it.write(std::span<const uint8_t>(data, 30)) == 30);
    CHECK(g_tx_it_calls == 1);
    CHECK(g_tx_dma_calls == 0);
    CHECK(g_tx_last_len == 30);
    CHECK(std::memcmp(g_tx_last_ptr, data, 30) == 0);
    drainTx(it, hi);
    CHECK(it.txPending() == 0);
}

// ── 测试 4a：DMA 循环接收的圈数账目（IDLE/HT/TC 混合事件）──
static void testCircularLap()
{
    reset();
    Uart u(Uart::Inst::Usart3, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::It));
    UART_HandleTypeDef* h = g_uart_held[2];
    u.onRx(onFrame);
    CHECK(g_rx_arm_calls == 1 && g_rx_last_size == Uart::kRxSize);
    uint8_t* rb = h->pRxBuffPtr;

    for (int i = 0; i < Uart::kRxSize; ++i) {
        rb[i] = static_cast<uint8_t>(i);
    }
    h->RxState = HAL_UART_STATE_BUSY_RX;  // 循环模式全程 busy
    HAL_UARTEx_RxEventCallback(h, 3);
    CHECK(g_frames == 1 && g_seen_len == 3);
    HAL_UARTEx_RxEventCallback(h, Uart::kRxSize / 2);
    CHECK(g_frames == 2 && g_seen_len == Uart::kRxSize / 2);
    HAL_UARTEx_RxEventCallback(h, 300);
    CHECK(g_frames == 3 && g_seen_len == 300);
    HAL_UARTEx_RxEventCallback(h, Uart::kRxSize);  // TC = 满一圈
    CHECK(g_frames == 4 && g_seen_len == Uart::kRxSize);
    CHECK(g_rx_arm_calls == 1);  // 循环模式不重装载
    CHECK(u.getStats().rx_lost == 0 && u.getStats().rx_bytes == Uart::kRxSize);
    CHECK(u.rxPending() == Uart::kRxSize);
    bool pat = true;
    for (int i = 0; i < Uart::kRxSize; ++i) {
        if (g_seen[i] != static_cast<uint8_t>(i)) {
            pat = false;
            break;
        }
    }
    CHECK(pat);

    // 第二圈：pos 回到圈内小值，绝对游标必须继续累加（不靠 TC 计数）
    for (int i = 0; i < 8; ++i) {
        rb[i] = static_cast<uint8_t>(0xA0 + i);
    }
    HAL_UARTEx_RxEventCallback(h, 8);
    CHECK(g_frames == 5 && g_seen_len == Uart::kRxSize + 8);
    CHECK(u.getStats().rx_bytes == Uart::kRxSize + 8);
    CHECK(u.rxPending() == Uart::kRxSize);  // 消费方没跟上
    CHECK(u.getStats().rx_lost == 8);

    // 读出：内容应是仍在环内的最新 512 字节
    uint8_t out[Uart::kRxSize];
    CHECK(u.read(std::span<uint8_t>(out, Uart::kRxSize)) == Uart::kRxSize);
    bool lap = true;
    for (int i = 0; i < Uart::kRxSize - 8; ++i) {
        if (out[i] != static_cast<uint8_t>(8 + i)) {
            lap = false;
            break;
        }
    }
    CHECK(lap);
    CHECK(out[Uart::kRxSize - 8] == 0xA0 && out[Uart::kRxSize - 1] == 0xA7);
    CHECK(u.rxPending() == 0);
}

// ── 测试 4b：IDLE 先于 DMA TC 被处理（跨圈位置回落）+ 帧拆两段 ──
static void testCircularFrameSplit()
{
    reset();
    Uart u(Uart::Inst::Usart1, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::It));
    UART_HandleTypeDef* h = g_uart_held[0];
    u.onRx(onFrame);
    uint8_t* rb = h->pRxBuffPtr;
    for (int i = 0; i < Uart::kRxSize; ++i) {
        rb[i] = static_cast<uint8_t>(0x40 + (i % 64));
    }
    h->RxState = HAL_UART_STATE_BUSY_RX;

    // 第一帧结束于圈内 511
    HAL_UARTEx_RxEventCallback(h, Uart::kRxSize - 1);
    CHECK(g_frames == 1 && g_seen_len == Uart::kRxSize - 1);

    // 下一帧 pos 回落到 3：说明已经跨过界，必须补上一整圈
    HAL_UARTEx_RxEventCallback(h, 3);
    CHECK(g_frames == 3);                          // 跨回绕拆两段
    CHECK(g_seen_len == (Uart::kRxSize - 1) + 4);  // 1 + 3 字节
    for (int i = 0; i < 4; ++i) {
        const uint8_t want = rb[(Uart::kRxSize - 1 + i) % Uart::kRxSize];
        CHECK(g_seen[g_seen_len - 4 + i] == want);
    }
    CHECK(u.getStats().rx_bytes == Uart::kRxSize + 3);
    CHECK(u.getStats().rx_lost == 3);  // 515 > 512：最旧 3 字节已被覆盖
    CHECK(u.rxPending() == Uart::kRxSize);

    // 读出的第一个字节应是 index 3 的内容（最旧 3 字节已丢）
    uint8_t out[Uart::kRxSize];
    CHECK(u.read(std::span<uint8_t>(out, Uart::kRxSize)) == Uart::kRxSize);
    CHECK(out[0] == rb[3] && out[1] == rb[4]);
}

// ── 测试 4c：消费方完全跟不上时保留最新数据 ──
static void testCircularStale()
{
    reset();
    Uart u(Uart::Inst::Usart2, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::It));
    UART_HandleTypeDef* h = g_uart_held[1];
    u.onRx(onFrame);
    uint8_t* rb = h->pRxBuffPtr;
    h->RxState = HAL_UART_STATE_BUSY_RX;

    for (int lap = 0; lap < 2; ++lap) {
        for (int i = 0; i < Uart::kRxSize; ++i) {
            rb[i] = static_cast<uint8_t>(0xC0 + lap * 8 + (i % 8));
        }
        HAL_UARTEx_RxEventCallback(h, Uart::kRxSize);  // TC
    }
    CHECK(u.rxPending() == Uart::kRxSize);
    CHECK(u.getStats().rx_lost == Uart::kRxSize);
    uint8_t out[Uart::kRxSize];
    CHECK(u.read(std::span<uint8_t>(out, Uart::kRxSize)) == Uart::kRxSize);
    bool fresh = true;
    for (int i = 0; i < Uart::kRxSize; ++i) {
        if (out[i] != static_cast<uint8_t>(0xC8 + (i % 8))) {
            fresh = false;
            break;
        }
    }
    CHECK(fresh);
}

// ── 测试 5：ORE 错误恢复（重装载 + 位置基准重置，未读数据丢弃）──
static void testErrRecovery()
{
    reset();
    Uart u(Uart::Inst::Usart3, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::It));
    UART_HandleTypeDef* h = g_uart_held[2];
    u.onRx(onFrame);
    CHECK(g_rx_arm_calls == 1);

    fireRxEvent(h, reinterpret_cast<const uint8_t*>("xy"), 2, 2);
    CHECK(g_frames == 1 && g_seen_len == 2);
    CHECK(u.rxPending() == 2);

    h->ErrorCode = HAL_UART_ERROR_ORE;
    h->RxState = HAL_UART_STATE_READY;  // HAL 的错误路径会复位接收状态
    HAL_UART_ErrorCallback(h);
    CHECK(u.getStats().err_ore == 1);
    CHECK(g_rx_arm_calls == 2);  // 已重装载
    CHECK(u.rxPending() == 0);   // 未读数据随基准重置被丢弃

    // 基准已重置：新的 pos 从 0 起算
    fireRxEvent(h, reinterpret_cast<const uint8_t*>("zw"), 2, 2);
    CHECK(g_frames == 2 && g_seen_len == 4);
    uint8_t out[8]{};
    CHECK(u.read(std::span<uint8_t>(out, 8)) == 2);
    CHECK(std::memcmp(out, "zw", 2) == 0);

    // DmaIdle 的错误恢复同样要重装载
    reset();
    Uart d(Uart::Inst::Usart1);
    UART_HandleTypeDef* hd = g_uart_held[0];
    CHECK(g_rx_arm_calls == 1);
    hd->ErrorCode = HAL_UART_ERROR_NE;
    hd->RxState = HAL_UART_STATE_READY;
    HAL_UART_ErrorCallback(hd);
    CHECK(d.getStats().err_ne == 1);
    CHECK(g_rx_arm_calls == 2);
}

// ── 测试 6：非法配置 / 重复实例 / 析构清理 ──
static void testPanics()
{
    reset();
    {
        Uart a(Uart::Inst::Usart1);
        bool caught = false;
        uint32_t code = 0;
        try {
            Uart b(Uart::Inst::Usart1);
        } catch (uint32_t c) {
            caught = true;
            code = c;
        }
        CHECK(caught && code == 0x6002);
        CHECK(g_clk_en[4] == 1 && g_irq_enabled[USART1_IRQn] == 1);
    }
    CHECK(g_clk_dis[4] == 1);  // 时钟已关
    CHECK(g_irq_disabled[USART1_IRQn] == 1);
    CHECK(g_dma_aborts == 1);  // 接收 DMA 已中止

    reset();
    {
        bool caught = false;
        uint32_t code = 0;
        try {
            Uart a(Uart::Inst::Usart2, makeCfg(Uart::RxPath::It, Uart::TxPath::Dma));
        } catch (uint32_t c) {
            caught = true;
            code = c;
        }
        CHECK(caught && code == 0x6001);  // 纯 IT 接收 + DMA 发送 不允许
        CHECK(g_clk_en[5] == 0);          // 未注册实例：不碰硬件
    }
    reset();
    {
        Uart::Config c{};
        c.pins = Uart::Pins::Partial;
        bool caught = false;
        uint32_t code = 0;
        try {
            Uart a(Uart::Inst::Usart2, c);
        } catch (uint32_t c2) {
            caught = true;
            code = c2;
        }
        CHECK(caught && code == 0x6001);  // USART2 无部分重映射
    }
    reset();
    {
        Uart::Config c{};
        c.baud = 0;
        bool caught = false;
        uint32_t code = 0;
        try {
            Uart a(Uart::Inst::Usart1, c);
        } catch (uint32_t c2) {
            caught = true;
            code = c2;
        }
        CHECK(caught && code == 0x6001);
    }
    // 三个实例共存：USART1(DMA 收) + USART2(IT) + USART3(DMA 收发)
    reset();
    {
        Uart a(Uart::Inst::Usart1);
        Uart b(Uart::Inst::Usart2, makeCfg(Uart::RxPath::It, Uart::TxPath::It));
        Uart c(Uart::Inst::Usart3, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::Dma));
        CHECK(g_uart_held[0] && g_uart_held[1] && g_uart_held[2]);
        CHECK(g_rx_arm_calls == 2);  // USART1 + USART3
        CHECK(g_irq_enabled[USART2_IRQn] == 1);
    }
    CHECK(g_clk_dis[4] == 1 && g_clk_dis[5] == 1 && g_clk_dis[6] == 1);
    CHECK(g_uart_held[0] == nullptr && g_uart_held[1] == nullptr && g_uart_held[2] == nullptr);
}

// ── 测试 7~9：随机压力测试，与独立参考模型对照 ──
static uint32_t g_rng = 20260420u;
static uint32_t rnd()
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 8) & 0x7FFFFFFFu;
}

// IT 接收：随机帧长，逐轮全量消费；对照线性参考（帧段数由环跨界次数决定）
static void stressItRx()
{
    reset();
    Uart u(Uart::Inst::Usart1, makeCfg(Uart::RxPath::It, Uart::TxPath::It));
    IrqSim irq{&g_usart[0], 0};
    u.onRx(onFrame);

    uint32_t abs = 0;  // 真实绝对写位置
    for (int round = 0; round < 500; ++round) {
        const int n = 1 + static_cast<int>(rnd() % 400);
        uint8_t buf[400];
        for (int i = 0; i < n; ++i) {
            buf[i] = static_cast<uint8_t>((abs + i) * 31u + i);
        }
        const uint32_t before = abs;
        irq.rx(buf, n);
        abs += static_cast<uint32_t>(n);
        irq.idle();

        const int segs = 1 + static_cast<int>((abs - 1u) / Uart::kRxSize - before / Uart::kRxSize);
        if (g_frames != segs || g_seen_len != n) {
            std::printf("FAIL stressItRx round %d: frames=%d(期望%d) bytes=%d(期望%d)\n", round,
                        g_frames, segs, g_seen_len, n);
            ++g_fail;
            return;
        }
        for (int i = 0; i < n; ++i) {
            if (g_seen[i] != buf[i]) {
                std::printf("FAIL stressItRx round %d 内容不符 at %d\n", round, i);
                ++g_fail;
                return;
            }
        }
        uint8_t back[512];
        if (static_cast<int>(u.read(std::span<uint8_t>(back, 512))) != n) {
            std::printf("FAIL stressItRx round %d rxPending 不符\n", round);
            ++g_fail;
            return;
        }
        for (int i = 0; i < n; ++i) {
            if (back[i] != buf[i]) {
                std::printf("FAIL stressItRx round %d read 内容不符\n", round);
                ++g_fail;
                return;
            }
        }
        g_frames = 0;
        g_seen_len = 0;
    }
    CHECK(u.getStats().rx_lost == 0);
}

// IT 发送（逐字节严格对照）：每个 TXE 中断取出的字节必须等于 FIFO 头部
static void stressItTxOrder()
{
    reset();
    Uart u(Uart::Inst::Usart1, makeCfg(Uart::RxPath::It, Uart::TxPath::It));
    USART_TypeDef* r = &g_usart[0];

    static uint8_t ref[1 << 20];  // 足够容纳整场随机测试
    int ref_in = 0, ref_out = 0;
    int last_byte = -1;

    for (int step = 0; step < 1500; ++step) {
        const int n = 1 + static_cast<int>(rnd() % 200);
        uint8_t buf[200];
        for (int i = 0; i < n; ++i) {
            buf[i] = static_cast<uint8_t>(ref_in + i);
        }
        size_t off = 0;
        while (off < static_cast<size_t>(n)) {
            const size_t got = u.write(std::span<const uint8_t>(buf + off, n - off));
            for (size_t i = 0; i < got; ++i) {
                ref[ref_in++] = buf[off + i];
            }
            off += got;
            // 环满：排空到完全空出来，再补写剩余部分（校验入队不会永久失败）
            while (u.busy()) {
                r->SR = USART_SR_TXE;
                uartIrqDispatch(0);
                if (u.busy()) {
                    if (ref_out >= ref_in || static_cast<int>(r->DR & 0xFFu) != ref[ref_out]) {
                        std::printf("FAIL stressItTxOrder step %d 补写排空不符\n", step);
                        ++g_fail;
                        return;
                    }
                    ++ref_out;
                    last_byte = static_cast<int>(r->DR & 0xFFu);
                }
            }
        }
        const int drain_n = static_cast<int>(rnd() % 500);
        for (int i = 0; i < drain_n && u.busy(); ++i) {
            r->SR = USART_SR_TXE;
            uartIrqDispatch(0);
            const int out = static_cast<int>(r->DR & 0xFFu);
            if (u.busy()) {
                // 这一拍真的搬了一个字节
                if (ref_out >= ref_in || out != ref[ref_out]) {
                    std::printf("FAIL stressItTxOrder step %d out %d: got %02X want %02X\n", step,
                                ref_out, out, ref_out < ref_in ? ref[ref_out] : 0xFF);
                    ++g_fail;
                    return;
                }
                ++ref_out;
                last_byte = out;
            } else {
                // 环已空：这一拍只关中断，DR 应保持上一字节
                if (out != last_byte) {
                    std::printf("FAIL stressItTxOrder step %d 空环拍写了新字节\n", step);
                    ++g_fail;
                    return;
                }
                break;
            }
        }
    }
    while (u.busy()) {
        r->SR = USART_SR_TXE;
        uartIrqDispatch(0);
        if (ref_out < ref_in) {
            const int out = static_cast<int>(r->DR & 0xFFu);
            if (out != ref[ref_out]) {
                std::printf("FAIL stressItTxOrder 收尾不符\n");
                ++g_fail;
                return;
            }
            ++ref_out;
        }
    }
    CHECK(ref_in >= ref_out);
    CHECK(ref_out == ref_in);
    CHECK(static_cast<int>(u.getStats().tx_bytes) == ref_in);
}

// DMA 循环接收：测试知道真实字节流，可独立校验驱动的位置账目
static void stressCircular()
{
    reset();
    Uart u(Uart::Inst::Usart2, makeCfg(Uart::RxPath::DmaCircular, Uart::TxPath::It));
    UART_HandleTypeDef* h = g_uart_held[1];
    u.onRx(onFrame);
    uint8_t* rb = h->pRxBuffPtr;
    h->RxState = HAL_UART_STATE_BUSY_RX;

    uint32_t abs = 0;
    for (int seg = 0; seg < 400; ++seg) {
        const int n = 1 + static_cast<int>(rnd() % (Uart::kRxSize - 112));
        // 先消费，避免覆盖未读数据
        uint8_t back[Uart::kRxSize];
        while (u.rxPending() > 0) {
            u.read(std::span<uint8_t>(back, Uart::kRxSize));
        }
        for (int i = 0; i < n; ++i) {
            const uint32_t at = abs + static_cast<uint32_t>(i);
            rb[at % Uart::kRxSize] = static_cast<uint8_t>(at * 7u + i);
        }
        const uint32_t before = abs;
        abs += static_cast<uint32_t>(n);

        // 按 HAL 的行为发出事件：每半圈一个 HT、整圈一个 TC，段尾一个 IDLE
        for (uint32_t p = before; p < abs;) {
            const uint32_t half = (p / Uart::kRxSize) * Uart::kRxSize + Uart::kRxSize / 2;
            const uint32_t full = (p / Uart::kRxSize) * Uart::kRxSize + Uart::kRxSize;
            if (half > p && half <= abs) {
                p = half;
                HAL_UARTEx_RxEventCallback(h, Uart::kRxSize / 2);
            } else if (full > p && full <= abs) {
                p = full;
                HAL_UARTEx_RxEventCallback(h, Uart::kRxSize);
            } else {
                p = abs;
                HAL_UARTEx_RxEventCallback(h, static_cast<uint16_t>(abs % Uart::kRxSize));
            }
        }
        if (g_seen_len != n) {
            std::printf("FAIL stressCircular seg %d: 交付 %d 期望 %d\n", seg, g_seen_len, n);
            ++g_fail;
            return;
        }
        for (int i = 0; i < n; ++i) {
            const uint8_t want = static_cast<uint8_t>((before + static_cast<uint32_t>(i)) * 7u + i);
            if (g_seen[i] != want) {
                std::printf("FAIL stressCircular seg %d 内容不符 at %d\n", seg, i);
                ++g_fail;
                return;
            }
        }
        g_seen_len = 0;
        g_frames = 0;
    }
    CHECK(u.getStats().rx_lost == 0);
    CHECK(g_rx_arm_calls == 1);  // 循环模式从不重装载
}

int main()
{
    testItMode();
    testDmaIdle();
    testTxChunking();
    testTxHalIt();
    testCircularLap();
    testCircularFrameSplit();
    testCircularStale();
    testErrRecovery();
    testPanics();
    stressItRx();
    stressItTxOrder();
    stressCircular();
    std::printf("%s fail=%d\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
