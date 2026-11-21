/*!
 * \file uart.cpp
 * \brief stm32::Uart：HAL UART/DMA 初始化 + per-instance 中断 trampoline。
 *
 * 中断归属（构造时确定，见 hal_irq_）：
 * - 纯 IT 路径（cfg.rx == It）：USART trampoline 直接进 Uart::fastIsr()，逐字节处理
 *   TXE/RXNE/IDLE 与错误标志（含 F1 ORE 勘误的“读 SR → 读 DR”清除序列），不穿
 *   HAL_UART_IRQHandler，热路径最短。
 * - 任一 DMA 路径：trampoline 转 HAL_UART_IRQHandler，HAL 再回调本文件实现的 HAL
 *   弱符号（mspInit/mspDeInit/RxEvent/TxCplt/ErrorCallback），按 huart->Instance
 *   查表分发到实例；事件按块发生，HAL 开销可忽略。
 *
 * F1 的 DMA 通道与外设是硬连线映射，本文件为 USART 所用通道（Ch2~Ch7）提供强
 * DMA1_Channelx_IRQHandler；Ch1 留给 ADC/TIM。若其他外设要用同一通道，需自行
 * 合并中断入口（本文件的分发只认本类登记的 DMA 句柄）。
 */
#include "peripheral/uart.hpp"

#include "peripheral/gpio.hpp"

namespace stm32
{

namespace
{

///< 实例索引 → USART 外设
// System::panic 的错误码：0x60xx 段留给 Uart（与 uart.hpp 的说明一致）
constexpr uint32_t kPanicBadConfig = 0x6001u;  ///< 实例/配置非法（含 USART2 不支持的重映射）
constexpr uint32_t kPanicDuplicate = 0x6002u;  ///< 同一 USART 实例重复构造
constexpr uint32_t kPanicHalInit = 0x6003u;    ///< HAL_UART_Init 失败
constexpr uint32_t kPanicHalDeInit = 0x6004u;  ///< HAL_UART_DeInit 失败

USART_TypeDef* const kUsart[3] = {USART1, USART2, USART3};
///< 实例索引 → USART 中断线
constexpr IRQn_Type kUsartIrqn[3] = {USART1_IRQn, USART2_IRQn, USART3_IRQn};
///< 实例索引 → 硬连线 DMA 通道（通道号 1..7）
constexpr uint8_t kTxChan[3] = {4, 7, 2};  // USART1/2/3 TX
constexpr uint8_t kRxChan[3] = {5, 6, 3};  // USART1/2/3 RX
///< 通道号(1..7) → 通道外设 / 通道中断线
DMA_Channel_TypeDef* const kDmaChan[8] = {nullptr,       DMA1_Channel1, DMA1_Channel2,
                                              DMA1_Channel3, DMA1_Channel4, DMA1_Channel5,
                                              DMA1_Channel6, DMA1_Channel7};
constexpr IRQn_Type kDmaIrqn[7] = {DMA1_Channel1_IRQn, DMA1_Channel2_IRQn, DMA1_Channel3_IRQn,
                                   DMA1_Channel4_IRQn, DMA1_Channel5_IRQn, DMA1_Channel6_IRQn,
                                   DMA1_Channel7_IRQn};

struct PinMap {
    Gpio::Group tx_port;
    uint8_t tx_pin;
    Gpio::Group rx_port;
    uint8_t rx_pin;
};

///< [实例][Pins] → TX/RX 引脚（USART2：Partial 为保留编码，Full 在 GPIOD）
constexpr PinMap kPinMap[3][3] = {
        /* USART1 */ {{Gpio::Group::A, 9, Gpio::Group::A, 10},
                      {Gpio::Group::B, 6, Gpio::Group::B, 7},
                      {Gpio::Group::A, 9, Gpio::Group::A, 10}},
        /* USART2 */
        {{Gpio::Group::A, 2, Gpio::Group::A, 3},
         {Gpio::Group::A, 2, Gpio::Group::A, 3},
         {Gpio::Group::A, 2, Gpio::Group::A, 3}},
        /* USART3 */
        {{Gpio::Group::B, 10, Gpio::Group::B, 11},
         {Gpio::Group::C, 4, Gpio::Group::C, 5},
         {Gpio::Group::C, 10, Gpio::Group::C, 11}},
};

///< 每实例至多一个 Uart（构造注册 / 析构摘除）；mspInit 也依赖它
Uart* g_active[3] = {};
///< 通道号(1..7) → 本类登记的 DMA 句柄（供通道中断分发）
DMA_HandleTypeDef* g_dma_held[8] = {};

///< USART 外设指针 → 实例索引；未知返回 0xFF
uint8_t idxOf(USART_TypeDef* r) noexcept
{
    if (r == USART1) {
        return 0;
    }
    if (r == USART2) {
        return 1;
    }
    if (r == USART3) {
        return 2;
    }
    return 0xFFu;
}

///< 通道中断分发：本类登记的句柄交给 HAL，否则仅清标志避免中断风暴
void dmaIrqDispatch(uint8_t ch) noexcept
{
    if (ch < 1u || ch > 7u) {
        return;
    }
    if (DMA_HandleTypeDef* h = g_dma_held[ch]) {
        HAL_DMA_IRQHandler(h);
        return;
    }
    DMA1->IFCR = DMA_IFCR_CGIF1 << ((ch - 1u) * 4u);
}

void enableClock(uint8_t idx) noexcept
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    if (idx == 0u) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if (idx == 1u) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}

void disableClock(uint8_t idx) noexcept
{
    if (idx == 0u) {
        __HAL_RCC_USART1_CLK_DISABLE();
    } else if (idx == 1u) {
        __HAL_RCC_USART2_CLK_DISABLE();
    } else {
        __HAL_RCC_USART3_CLK_DISABLE();
    }
}

///< 应用 AFIO 引脚重映射（调用前 AFIO 时钟已开）
void applyRemap(uint8_t idx, Uart::Pins pins) noexcept
{
    if (idx == 0u) {
        // USART1 只有部分重映射（TX=PB6/RX=PB7）；Full 在 F103 上等价 Default
        if (pins == Uart::Pins::Partial) {
            __HAL_AFIO_REMAP_USART1_ENABLE();
        } else {
            __HAL_AFIO_REMAP_USART1_DISABLE();
        }
    } else if (idx == 1u) {
        __HAL_AFIO_REMAP_USART2_DISABLE();  // Full 在 GPIOD，本类不支持
    } else if (pins == Uart::Pins::Full) {
        __HAL_AFIO_REMAP_USART3_ENABLE();  // PC10/PC11
    } else if (pins == Uart::Pins::Partial) {
        __HAL_AFIO_REMAP_USART3_PARTIAL();  // PC4/PC5
    } else {
        __HAL_AFIO_REMAP_USART3_DISABLE();
    }
}

///< 配置引脚：TX=复用推挽，RX=上拉输入
void initPins(uint8_t idx, Uart::Pins pins)
{
    PinMap const& m = kPinMap[idx][static_cast<uint8_t>(pins)];

    Gpio tx(m.tx_port, m.tx_pin);
    tx.configure(Gpio::Mode::OUTPUT_AF_PP, Gpio::Speed::HIGH, Gpio::Pull::NO);

    Gpio rx(m.rx_port, m.rx_pin);
    rx.configure(Gpio::Mode::INPUT, Gpio::Speed::HIGH, Gpio::Pull::UP);
}

///< 释放引脚：退回普通输入，避免残留复用功能
void releasePins(uint8_t idx, Uart::Pins pins)
{
    PinMap const& m = kPinMap[idx][static_cast<uint8_t>(pins)];

    Gpio tx(m.tx_port, m.tx_pin);
    tx.configure(Gpio::Mode::INPUT, Gpio::Speed::LOW, Gpio::Pull::NO);

    Gpio rx(m.rx_port, m.rx_pin);
    rx.configure(Gpio::Mode::INPUT, Gpio::Speed::LOW, Gpio::Pull::NO);
}

}  // namespace

// ── 构造 / 析构 ──

Uart::Uart(Inst inst, Config const& cfg) : cfg_(cfg), idx_(static_cast<uint8_t>(inst))
{
    static_assert((kTxSize & (kTxSize - 1u)) == 0u, "kTxSize 必须是 2 的幂");
    static_assert((kRxSize & (kRxSize - 1u)) == 0u, "kRxSize 必须是 2 的幂");
    static_assert((kFrameSize & (kFrameSize - 1u)) == 0u, "kFrameSize 必须是 2 的幂");
    static_assert(kFrameSize <= kRxSize, "kFrameSize 不能大于 kRxSize");

    if (idx_ > 2u) {
        System::panic(kPanicBadConfig);  // 实例越界
    }
    if (g_active[idx_] != nullptr) {
        System::panic(kPanicDuplicate);  // 同一实例重复构造
    }
    validate();

    hal_irq_ = (cfg_.rx != RxPath::It);

    g_active[idx_] = this;  // 先注册：HAL_UART_Init 会在 mspInit 里查回本对象

    configureHal();
    configureDma();
    if (HAL_UART_Init(&hal_) != HAL_OK) {
        System::panic(kPanicHalInit);  // HAL 初始化失败
    }
    startRx();
}

Uart::~Uart()
{
    // 先屏蔽中断线，避免拆除过程中再进入本对象的 ISR
    NVIC_DisableIRQ(kUsartIrqn[idx_]);
    if (cfg_.rx != RxPath::It) {
        NVIC_DisableIRQ(kDmaIrqn[kRxChan[idx_] - 1u]);
        g_dma_held[kRxChan[idx_]] = nullptr;
    }
    if (cfg_.tx == TxPath::Dma) {
        NVIC_DisableIRQ(kDmaIrqn[kTxChan[idx_] - 1u]);
        g_dma_held[kTxChan[idx_]] = nullptr;
    }

    stopAll();
    (void) HAL_UART_DeInit(&hal_);

    if (g_active[idx_] == this) {
        g_active[idx_] = nullptr;
    }
}

void Uart::validate() const
{
    if (cfg_.baud == 0u) {
        System::panic(kPanicBadConfig);
    }
    // 纯 IT 接收由自管快速 ISR 处理，发送也必须走 IT
    if (cfg_.rx == RxPath::It && cfg_.tx != TxPath::It) {
        System::panic(kPanicBadConfig);
    }
    // USART2：Partial 是保留编码；Full 在 GPIOD（F103xB 无该引脚组）
    if (idx_ == 1u && cfg_.pins != Pins::Default) {
        System::panic(kPanicBadConfig);
    }
}

void Uart::configureHal()
{
    hal_.Instance = kUsart[idx_];
    hal_.Init.BaudRate = cfg_.baud;
    hal_.Init.WordLength = UART_WORDLENGTH_8B;
    hal_.Init.StopBits = (cfg_.stop == Stop::Bits2) ? UART_STOPBITS_2 : UART_STOPBITS_1;
    hal_.Init.Parity = (cfg_.parity == Parity::Even)  ? UART_PARITY_EVEN
                       : (cfg_.parity == Parity::Odd) ? UART_PARITY_ODD
                                                      : UART_PARITY_NONE;
    hal_.Init.Mode = UART_MODE_TX_RX;
    hal_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hal_.Init.OverSampling = UART_OVERSAMPLING_16;  // F1（非 F100）只有 16 倍过采样

    hal_.hdmatx = (cfg_.tx == TxPath::Dma) ? &dma_tx_ : nullptr;
    hal_.hdmarx = (cfg_.rx != RxPath::It) ? &dma_rx_ : nullptr;
}

void Uart::configureDma()
{
    if (cfg_.rx == RxPath::It && cfg_.tx == TxPath::It) {
        return;
    }
    __HAL_RCC_DMA1_CLK_ENABLE();

    if (cfg_.rx != RxPath::It) {
        uint8_t const ch = kRxChan[idx_];
        dma_rx_.Instance = kDmaChan[ch];
        dma_rx_.Init.Direction = DMA_PERIPH_TO_MEMORY;
        dma_rx_.Init.PeriphInc = DMA_PINC_DISABLE;  // DR 是固定地址
        dma_rx_.Init.MemInc = DMA_MINC_ENABLE;
        dma_rx_.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        dma_rx_.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        dma_rx_.Init.Mode = (cfg_.rx == RxPath::DmaCircular) ? DMA_CIRCULAR : DMA_NORMAL;
        dma_rx_.Init.Priority = DMA_PRIORITY_HIGH;
        dma_rx_.Parent = &hal_;
        if (HAL_DMA_Init(&dma_rx_) != HAL_OK) {
            System::panic(kPanicHalInit);
        }
        g_dma_held[ch] = &dma_rx_;
        HAL_NVIC_SetPriority(kDmaIrqn[ch - 1u], cfg_.irq_pri, 0);
        HAL_NVIC_EnableIRQ(kDmaIrqn[ch - 1u]);
    }

    if (cfg_.tx == TxPath::Dma) {
        uint8_t const ch = kTxChan[idx_];
        dma_tx_.Instance = kDmaChan[ch];
        dma_tx_.Init.Direction = DMA_MEMORY_TO_PERIPH;
        dma_tx_.Init.PeriphInc = DMA_PINC_DISABLE;
        dma_tx_.Init.MemInc = DMA_MINC_ENABLE;
        dma_tx_.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        dma_tx_.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        dma_tx_.Init.Mode = DMA_NORMAL;  // 环回绕拆块后逐块重装
        dma_tx_.Init.Priority = DMA_PRIORITY_MEDIUM;
        dma_tx_.Parent = &hal_;
        if (HAL_DMA_Init(&dma_tx_) != HAL_OK) {
            System::panic(kPanicHalInit);
        }
        g_dma_held[ch] = &dma_tx_;
        HAL_NVIC_SetPriority(kDmaIrqn[ch - 1u], cfg_.irq_pri, 0);
        HAL_NVIC_EnableIRQ(kDmaIrqn[ch - 1u]);
    }
}

void Uart::mspInit()
{
    enableClock(idx_);
    applyRemap(idx_, cfg_.pins);
    initPins(idx_, cfg_.pins);

    HAL_NVIC_SetPriority(kUsartIrqn[idx_], cfg_.irq_pri, 0);
    HAL_NVIC_EnableIRQ(kUsartIrqn[idx_]);
}

void Uart::mspDeInit()
{
    releasePins(idx_, cfg_.pins);
    disableClock(idx_);
}

void Uart::startRx()
{
    if (cfg_.rx == RxPath::DmaIdle) {
        armRx();
    } else if (cfg_.rx == RxPath::DmaCircular) {
        armRx();
    } else {
        // 纯 IT：RXNE 收字节，IDLE 作帧界，EIE 通知 FE/NE/ORE
        hal_.Instance->CR1 |= (USART_CR1_RXNEIE | USART_CR1_IDLEIE);
        hal_.Instance->CR3 |= USART_CR3_EIE;
    }
}

void Uart::armRx() noexcept
{
    uint8_t* buf = (cfg_.rx == RxPath::DmaIdle) ? frame_buf_ : rx_buf_;
    uint16_t const n = (cfg_.rx == RxPath::DmaIdle) ? kFrameSize : kRxSize;

    if (HAL_UARTEx_ReceiveToIdle_DMA(&hal_, buf, n) == HAL_OK) {
        return;
    }
    // 状态机未回到 READY（异常中止时可能出现）：强制复位后重试一次
    hal_.RxState = HAL_UART_STATE_READY;
    (void) HAL_UARTEx_ReceiveToIdle_DMA(&hal_, buf, n);
}

void Uart::stopAll()
{
    hal_.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE | USART_CR1_RXNEIE | USART_CR1_IDLEIE |
                            USART_CR1_PEIE);
    hal_.Instance->CR3 &= ~(USART_CR3_EIE | USART_CR3_DMAT | USART_CR3_DMAR);

    if (cfg_.rx != RxPath::It) {
        (void) HAL_DMA_Abort(&dma_rx_);
        hal_.RxState = HAL_UART_STATE_READY;
    }
    if (cfg_.tx == TxPath::Dma) {
        (void) HAL_DMA_Abort(&dma_tx_);
        hal_.gState = HAL_UART_STATE_READY;
    }
    tx_pumping_ = false;
}

// ── 接收：环写入 + 帧通知 ──

void Uart::pushRx(uint8_t b) noexcept
{
    uint16_t const head = rx_head_;
    if (static_cast<uint16_t>(head - rx_tail_) < kRxSize) {
        rx_buf_[head & (kRxSize - 1u)] = b;
        __DMB();
        rx_head_ = static_cast<uint16_t>(head + 1u);
        rx_bytes_ = rx_bytes_ + 1u;
    } else {
        rx_lost_ = rx_lost_ + 1u;  // 消费方跟不上：丢弃最新字节
    }
}

void Uart::notifyFrame(uint16_t abs_end) noexcept
{
    if (rx_cb_ == nullptr) {
        rx_fr_ = abs_end;
        return;
    }
    uint16_t const n = static_cast<uint16_t>(abs_end - rx_fr_);
    uint16_t const i = static_cast<uint16_t>(rx_fr_ & (kRxSize - 1u));
    uint16_t first = static_cast<uint16_t>(kRxSize - i);
    if (first > n) {
        first = n;
    }
    if (first != 0u) {
        rx_cb_(std::span<const uint8_t>(&rx_buf_[i], first), rx_ctx_);
    }
    uint16_t const rest = static_cast<uint16_t>(n - first);
    if (rest != 0u) {  // 帧跨缓冲末尾：拆两段交付
        rx_cb_(std::span<const uint8_t>(&rx_buf_[0], rest), rx_ctx_);
    }
    rx_fr_ = abs_end;
}

void Uart::onRxEvent(uint16_t pos) noexcept
{
    if (cfg_.rx == RxPath::DmaIdle) {
        uint16_t n = pos;
        if (n > kFrameSize) {
            n = kFrameSize;
        }
        if (n != 0u) {
            for (uint16_t i = 0; i < n; ++i) {
                pushRx(frame_buf_[i]);
            }
            if (rx_cb_ != nullptr) {
                rx_cb_(std::span<const uint8_t>(frame_buf_, n), rx_ctx_);
            }
        }
        // HAL 在 IDLE/满 事件后已停止接收：重新装载下一帧
        armRx();
        return;
    }

    // DmaCircular：rx_buf_ 就是 DMA 缓冲。HAL 给的 pos 是当前圈内已收字节数
    // （HT=kRxSize/2、TC=kRxSize、IDLE=kRxSize-CNDTR）。
    // 圈数不靠 TC 事件计数（DMA 线与 USART 线优先级无关），而是从 pos 相对上次
    // 圈内位置的回落本身推出——跨圈时补一整圈，因此与中断先后无关。
    uint16_t const prev = rx_head_;
    uint16_t const prev_pos = static_cast<uint16_t>(prev & (kRxSize - 1u));
    uint16_t const p = (pos > kRxSize) ? kRxSize : pos;
    uint16_t delta;
    if (p >= prev_pos) {
        delta = static_cast<uint16_t>(p - prev_pos);
    } else {
        delta = static_cast<uint16_t>(static_cast<uint16_t>(kRxSize - prev_pos) + p);
    }
    if (delta == 0u) {
        return;
    }
    uint16_t const abs = static_cast<uint16_t>(prev + delta);

    uint16_t const used = static_cast<uint16_t>(abs - rx_tail_);
    if (used > kRxSize) {  // DMA 已覆盖未读数据：丢最旧，保住连续性
        rx_lost_ = rx_lost_ + static_cast<uint32_t>(used - kRxSize);
        rx_tail_ = static_cast<uint16_t>(abs - kRxSize);
    }
    __DMB();
    rx_head_ = abs;
    rx_bytes_ = rx_bytes_ + static_cast<uint32_t>(static_cast<uint16_t>(abs - rx_fr_));
    notifyFrame(abs);
}

void Uart::onErr(uint32_t hal_err) noexcept
{
    if ((hal_err & HAL_UART_ERROR_PE) != 0u) {
        err_pe_ = err_pe_ + 1u;
    }
    if ((hal_err & HAL_UART_ERROR_FE) != 0u) {
        err_fe_ = err_fe_ + 1u;
    }
    if ((hal_err & HAL_UART_ERROR_NE) != 0u) {
        err_ne_ = err_ne_ + 1u;
    }
    if ((hal_err & HAL_UART_ERROR_ORE) != 0u) {
        err_ore_ = err_ore_ + 1u;
    }

    // ORE / DMA 错误会中止接收：HAL 已把 RxState 复位并清掉 DMA 位置，
    // 重新从头装载（位置基准随之重置，未读数据丢弃）
    if (cfg_.rx != RxPath::It) {
        if (cfg_.rx == RxPath::DmaCircular) {
            rx_fr_ = 0;
            rx_head_ = 0;
            rx_tail_ = 0;
        }
        armRx();
    }

    // 发送侧不在此动状态：HAL 已空闲但没回调时，由 kickTx()/flush() 里的 txRescue() 接手
}

// ── 发送泵 ──

void Uart::kickTx() noexcept
{
    uint32_t const prim = __get_PRIMASK();
    __disable_irq();
    if (!tx_pumping_ && tx_take_ != tx_head_) {
        tx_pumping_ = true;
        if (!hal_irq_) {
            hal_.Instance->CR1 |= USART_CR1_TXEIE;  // 快速 ISR 逐字节搬走
        } else {
            txChunkHal();
        }
    } else if (tx_pumping_) {
        txRescue();
    }
    __set_PRIMASK(prim);
}

void Uart::txRescue() noexcept
{
    // HAL 路径上若发送被错误中止，HAL 不会给出 TxCplt 回调：此时 HAL 已空闲而
    // 队列仍有待发送数据，认为上一块已结束（送达或被丢弃），接着泵。
    if (hal_irq_ && tx_pumping_ && hal_.gState == HAL_UART_STATE_READY && tx_take_ != tx_head_) {
        tx_sent_ = tx_take_;
        txChunkHal();
    }
}

void Uart::txChunkHal() noexcept
{
    uint16_t const take = tx_take_;
    uint16_t const all = static_cast<uint16_t>(tx_head_ - take);
    if (all == 0u) {
        tx_pumping_ = false;
        return;
    }
    uint16_t const i = static_cast<uint16_t>(take & (kTxSize - 1u));
    uint16_t n = static_cast<uint16_t>(kTxSize - i);  // 不跨环末尾，保证块内连续
    if (n > all) {
        n = all;
    }

    HAL_StatusTypeDef st = (cfg_.tx == TxPath::Dma) ? HAL_UART_Transmit_DMA(&hal_, &tx_buf_[i], n)
                                                    : HAL_UART_Transmit_IT(&hal_, &tx_buf_[i], n);
    if (st != HAL_OK) {
        return;  // 状态未就绪：保持 pumping，等下一次事件继续
    }
    tx_take_ = static_cast<uint16_t>(take + n);
}

void Uart::onTxDone() noexcept
{
    tx_sent_ = tx_take_;
    if (tx_take_ != tx_head_) {
        txChunkHal();
        return;
    }
    tx_pumping_ = false;
    if (tx_cb_ != nullptr) {
        tx_cb_(tx_ctx_);
    }
}

// ── 中断 ──

void Uart::isr() noexcept
{
    if (hal_irq_) {
        HAL_UART_IRQHandler(&hal_);
        return;
    }
    fastIsr();
}

void Uart::fastIsr() noexcept
{
    USART_TypeDef* const r = hal_.Instance;
    uint32_t const sr = r->SR;  // 清标志序列第 1 步：读 SR
    uint32_t const cr1 = r->CR1;
    uint32_t const errs = sr & (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE);

    if ((errs != 0u) && ((cr1 & (USART_CR1_RXNEIE | USART_CR1_PEIE)) != 0u)) {
        if ((errs & USART_SR_PE) != 0u) {
            err_pe_ = err_pe_ + 1u;
        }
        if ((errs & USART_SR_FE) != 0u) {
            err_fe_ = err_fe_ + 1u;
        }
        if ((errs & USART_SR_NE) != 0u) {
            err_ne_ = err_ne_ + 1u;
        }
        if ((errs & USART_SR_ORE) != 0u) {
            err_ore_ = err_ore_ + 1u;
        }
    }

    if ((sr & USART_SR_RXNE) != 0u && (cr1 & USART_CR1_RXNEIE) != 0u) {
        // 读 DR 既取走字节，也完成 FE/NE/ORE 的标志清除序列
        pushRx(static_cast<uint8_t>(r->DR & 0xFFu));
    } else if (((sr | errs) & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_IDLE)) != 0u) {
        (void) r->DR;  // 无字节可取时也要走完 读 SR → 读 DR 清除序列（F1 ORE 勘误）
    }

    if ((sr & USART_SR_IDLE) != 0u && (cr1 & USART_CR1_IDLEIE) != 0u) {
        notifyFrame(rx_head_);  // 空闲线 = 一帧结束
    }

    if ((sr & USART_SR_TXE) != 0u && (cr1 & USART_CR1_TXEIE) != 0u) {
        uint16_t const take = tx_take_;
        if (take != tx_head_) {
            r->DR = tx_buf_[take & (kTxSize - 1u)];
            tx_take_ = static_cast<uint16_t>(take + 1u);
            tx_sent_ = tx_take_;
        } else {
            r->CR1 &= ~USART_CR1_TXEIE;
            tx_pumping_ = false;
            if (tx_cb_ != nullptr) {
                tx_cb_(tx_ctx_);
            }
        }
    }
}

// ── detail：per-instance trampoline 入口 ──

namespace detail
{

void uartIrqDispatch(uint8_t idx) noexcept
{
    if (idx <= 2u) {
        if (Uart* p = g_active[idx]) {
            p->isr();
        }
    }
}

void uartMspDispatch(uint8_t idx, bool init) noexcept
{
    if (idx <= 2u) {
        if (Uart* p = g_active[idx]) {
            if (init) {
                p->mspInit();
            } else {
                p->mspDeInit();
            }
        }
    }
}

void uartRxEventDispatch(uint8_t idx, uint16_t pos) noexcept
{
    if (idx <= 2u) {
        if (Uart* p = g_active[idx]) {
            p->onRxEvent(pos);
        }
    }
}

void uartTxDoneDispatch(uint8_t idx) noexcept
{
    if (idx <= 2u) {
        if (Uart* p = g_active[idx]) {
            p->onTxDone();
        }
    }
}

void uartErrDispatch(uint8_t idx, uint32_t hal_err) noexcept
{
    if (idx <= 2u) {
        if (Uart* p = g_active[idx]) {
            p->onErr(hal_err);
        }
    }
}

}  // namespace detail

}  // namespace stm32

// ── HAL 弱符号：全局唯一实现，按 Instance 查表分发到实例 ──

void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
    stm32::detail::uartMspDispatch(stm32::idxOf(huart->Instance), true);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
    stm32::detail::uartMspDispatch(stm32::idxOf(huart->Instance), false);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size)
{
    uint8_t const idx = stm32::idxOf(huart->Instance);
    if (idx <= 2u) {
        stm32::detail::uartRxEventDispatch(idx, size);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    uint8_t const idx = stm32::idxOf(huart->Instance);
    if (idx <= 2u) {
        stm32::detail::uartTxDoneDispatch(idx);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    uint8_t const idx = stm32::idxOf(huart->Instance);
    if (idx <= 2u) {
        stm32::detail::uartErrDispatch(idx, huart->ErrorCode);
    }
}

// ── 向量表入口：startup 文件里的弱符号在此被强定义取代 ──

extern "C" void USART1_IRQHandler(void)
{
    stm32::detail::uartIrqDispatch(0);
}

extern "C" void USART2_IRQHandler(void)
{
    stm32::detail::uartIrqDispatch(1);
}

extern "C" void USART3_IRQHandler(void)
{
    stm32::detail::uartIrqDispatch(2);
}

extern "C" void DMA1_Channel2_IRQHandler(void)
{
    stm32::dmaIrqDispatch(2);
}

extern "C" void DMA1_Channel3_IRQHandler(void)
{
    stm32::dmaIrqDispatch(3);
}

extern "C" void DMA1_Channel4_IRQHandler(void)
{
    stm32::dmaIrqDispatch(4);
}

extern "C" void DMA1_Channel5_IRQHandler(void)
{
    stm32::dmaIrqDispatch(5);
}

extern "C" void DMA1_Channel6_IRQHandler(void)
{
    stm32::dmaIrqDispatch(6);
}

extern "C" void DMA1_Channel7_IRQHandler(void)
{
    stm32::dmaIrqDispatch(7);
}
