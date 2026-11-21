#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "stm32f1xx_hal.h"  // UART_HandleTypeDef / DMA_HandleTypeDef / USART_TypeDef
#include "system/system.hpp"

namespace stm32
{

namespace detail
{
/**
 * per-instance USART 中断 trampoline（uart.cpp）入口。
 *
 * @param idx 实例索引（0..2，对应 USART1~3）
 */
void uartIrqDispatch(uint8_t idx) noexcept;

/**
 * per-instance Msp 分发（由 uart.cpp 里的 HAL_UART_MspInit/DeInit 调用）。
 *
 * @param idx  实例索引
 * @param init true=mspInit，false=mspDeInit
 */
void uartMspDispatch(uint8_t idx, bool init) noexcept;

/**
 * per-instance Rx 事件分发（HAL RxEvent 回调入口）。
 *
 * @param idx 实例索引
 * @param pos HAL 给出的圈内已收字节数（DmaIdle: 帧长；DmaCircular: HT=size/2、
 *            TC=size、IDLE=size-CNDTR）
 */
void uartRxEventDispatch(uint8_t idx, uint16_t pos) noexcept;

/**
 * per-instance 发送完成分发（HAL TxCplt 入口）。
 */
void uartTxDoneDispatch(uint8_t idx) noexcept;

/**
 * per-instance 错误分发（HAL ErrorCallback 入口）。
 *
 * @param hal_err HAL UART_ERROR_* 位掩码
 */
void uartErrDispatch(uint8_t idx, uint32_t hal_err) noexcept;
}  // namespace detail

/**
 * 通用异步串口（USART1~3），基于 HAL。
 *
 * 设计要点：
 * - 收发各一条无锁 SPSC 环形缓冲（容量为 2 的幂，索引掩码取模，无除法）；
 *   发送非阻塞入队，由 ISR/DMA 泵出；接收由 ISR/DMA 填充，用户随时取。
 * - 接收路径三选一：
 *   - `It`         ：RXNE 逐字节中断（自管快速 ISR，不穿 HAL_UART_IRQHandler）
 *   - `DmaIdle`    ：DMA(正常模式) + 空闲线帧定界，每帧一次事件，帧内连续
 *   - `DmaCircular`：DMA(循环模式) 直写接收缓冲，永不丢字节，事件为 半满/满/空闲
 *     （接收缓冲就是环本身，零拷贝；消费跟不上时丢最旧并计入 Stats.rx_lost）
 * - 发送路径二选一：`It`（分块 HAL_UART_Transmit_IT 或快速 ISR 逐字节）、
 *   `Dma`（HAL_UART_Transmit_DMA，按环回绕拆块）。
 * - 纯 IT 模式（rx=It）由本类自管 ISR 处理 TXE/RXNE/IDLE/错误标志（含 F1 ORE
 *   勘误清除序列），热路径最短；一旦启用任一 DMA 路径，该实例的 USART 中断
 *   改由 HAL_UART_IRQHandler 处理（事件按块发生，开销可忽略）。
 *
 * F1 的 DMA 通道与外设是硬连线映射（本类固定使用，不可选）：
 *   USART1 TX=DMA1_Ch4 RX=DMA1_Ch5（与 I2C2 DMA 冲突）
 *   USART2 TX=DMA1_Ch7 RX=DMA1_Ch6（与 I2C1 DMA 冲突）
 *   USART3 TX=DMA1_Ch2 RX=DMA1_Ch3（与 SPI1 DMA 冲突）
 * uart.cpp 会为所用通道提供强 `DMA1_Channelx_IRQHandler`，若其他外设要用同一
 * 通道，需自行合并中断处理。
 *
 * 缓冲容量若需调整，改下面三个常量（必须是 2 的幂）。
 *
 * 对象体积：sizeof(Uart) ≈ 1.2 KB（环 + 句柄）。因为本工程链接脚本只预留
 * _Min_Stack_Size = 1 KB，建议用全局或 static 存储期对象，不要放在深调用栈里。
 * 另外：必须在 System::init() 之后构造（波特率按当时 PCLK 计算）。
 *
 */
class Uart final
{
public:
    ///< 发送环形缓冲容量（2 的幂）
    static constexpr uint16_t kTxSize = 256;
    ///< 接收环形缓冲容量（2 的幂；DmaCircular 模式下它同时就是 DMA 缓冲）
    static constexpr uint16_t kRxSize = 512;
    ///< DmaIdle 模式的单帧 DMA 缓冲容量（2 的幂，须 <= kRxSize）
    static constexpr uint16_t kFrameSize = 128;

    ///< USART 实例
    enum class Inst : uint8_t { Usart1 = 0, Usart2 = 1, Usart3 = 2 };
    ///< 校验（数据位固定 8 位：校验开时为 8 数据位 + 校验位，F1 HAL UART 仅提供 8B 字长）
    enum class Parity : uint8_t { None, Even, Odd };
    ///< 停止位（F1 HAL UART 仅支持 1 / 2）
    enum class Stop : uint8_t { Bits1, Bits2 };
    ///< 引脚组（F1 AFIO 重映射；Full 对 USART1 等价于 Default）
    enum class Pins : uint8_t { Default, Partial, Full };
    ///< 接收路径
    enum class RxPath : uint8_t { It, DmaIdle, DmaCircular };
    ///< 发送路径（rx=It 时只能取 It）
    enum class TxPath : uint8_t { It, Dma };

    /// 构造配置（全部字段有默认值：115200-8N1，默认引脚，DMA+空闲线收，IT 发）
    struct Config {
        uint32_t baud = 115200;
        Parity parity = Parity::None;
        Stop stop = Stop::Bits1;
        Pins pins = Pins::Default;
        RxPath rx = RxPath::DmaIdle;
        TxPath tx = TxPath::It;
        uint8_t irq_pri = 3;  ///< USART/DMA 中断优先级（0 最高）
    };

    /// 运行统计（ISR 内更新，读取非原子但单调）
    struct Stats {
        uint32_t tx_bytes;  ///< 已入队提交的字节数
        uint32_t rx_bytes;  ///< 已收到的字节数
        uint32_t rx_lost;   ///< 因缓冲溢出丢失的字节数
        uint32_t err_fe;    ///< 帧错误次数
        uint32_t err_ne;    ///< 噪声错误次数
        uint32_t err_ore;   ///< 溢出错误次数
        uint32_t err_pe;    ///< 校验错误次数
    };

    /**
     * 接收事件回调（ISR 上下文）：一段连续的新数据。
     *
     * - DmaIdle：每次 IDLE/满 事件一次调用，span 即整帧（仅回调期间有效）。
     * - DmaCircular / It：以 IDLE（空闲线）为分界；DMA 循环模式下 HT/TC 也会
     *   触发事件以避免数据被覆盖。若新数据跨越缓冲末尾，会按回绕拆成两段、
     *   分两次调用。
     * - 未订阅（cb=nullptr）时仅缓冲可用，靠 read() 取数。
     */
    using CbRx = void(std::span<const uint8_t>, void*);

    /// 发送队列排空回调（ISR 上下文，span 已真正上线）
    using CbTx = void(void*);

    /**
     * 构造并使能实例（时钟、GPIO、重映射、NVIC、DMA 全部自动完成）。
     *
     * @param inst USART 实例
     * @note 同一实例重复构造（0x6002）/ 配置非法（0x6001）/ HAL 初始化失败
     *       （0x6003）/ 析构时 HAL DeInit 失败（0x6004）时 panic
     */
    Uart(Inst inst) : Uart(inst, Config()) {}

    /**
     * 构造并使能实例（自定义配置）。
     *
     * @param inst USART 实例
     * @param cfg  配置
     */
    Uart(Inst inst, Config const& cfg);

    /**
     * 析构：停止收发、中止 DMA、屏蔽中断、摘除实例注册。
     */
    ~Uart();

    Uart(Uart const&) = delete;
    Uart& operator=(Uart const&) = delete;

    // ── 发送（非阻塞，可在 ISR 中调用；单一生产者上下文）──
    /**
     * 入队发送，返回实际入队字节数（< data.size() 表示环已满）。
     */
    size_t write(std::span<const uint8_t> data) noexcept
    {
        if (data.empty()) {
            return 0;
        }
        uint16_t const head = tx_head_;
        uint16_t const used = static_cast<uint16_t>(head - tx_sent_);
        uint16_t room = static_cast<uint16_t>(kTxSize - used);
        size_t n = data.size();
        if (n > room) {
            n = room;
        }

        uint16_t i = static_cast<uint16_t>(head & (kTxSize - 1));
        uint16_t first = static_cast<uint16_t>(kTxSize - i);
        if (first > n) {
            first = static_cast<uint16_t>(n);
        }
        std::memcpy(&tx_buf_[i], data.data(), first);
        if (n > first) {
            std::memcpy(&tx_buf_[0], data.data() + first, n - first);
        }
        __DMB();  // 数据先落环，再发布写游标
        tx_head_ = static_cast<uint16_t>(head + n);
        tx_bytes_ = tx_bytes_ + static_cast<uint32_t>(n);

        kickTx();
        return n;
    }

    /// 字符串字面量/string_view 便捷重载
    size_t write(std::string_view s) noexcept
    {
        return write(
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    }

    /// 单字节便捷重载
    size_t write(uint8_t b) noexcept
    {
        return write(std::span<const uint8_t>(&b, 1));
    }

    /**
     * 阻塞入队直到全部接受（不等待发完）。单一生产者下永不失败（无超时）。
     */
    bool writeAll(std::span<const uint8_t> data) noexcept
    {
        size_t off = 0;
        while (off < data.size()) {
            off += write(std::span<const uint8_t>(data.data() + off, data.size() - off));
        }
        return true;
    }

    bool writeAll(std::string_view s) noexcept
    {
        return writeAll(
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    }

    /// 字符串形式的带超时阻塞入队
    template <class Duration>
        requires std::convertible_to<Duration, std::chrono::milliseconds>
    bool writeAll(std::string_view s, Duration timeout) noexcept
    {
        return writeAll(
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()),
                timeout);
    }

    /**
     * 带超时的阻塞入队。
     *
     * @return 全部入队返回 true；超时返回 false（已入队部分照常发出）
     */
    template <class Duration>
        requires std::convertible_to<Duration, std::chrono::milliseconds>
    bool writeAll(std::span<const uint8_t> data, Duration timeout) noexcept
    {
        auto const start = System::now();
        size_t off = 0;
        while (off < data.size()) {
            off += write(std::span<const uint8_t>(data.data() + off, data.size() - off));
            if (System::now() - start > timeout) {
                return off == data.size();
            }
        }
        return true;
    }

    // ── 接收（非阻塞，单一消费者上下文）──
    /**
     * 从接收环取出最多 out.size() 字节，返回实际取出数。
     */
    size_t read(std::span<uint8_t> out) noexcept
    {
        if (out.empty()) {
            return 0;
        }
        uint16_t const tail = rx_tail_;
        uint16_t const avail = static_cast<uint16_t>(rx_head_ - tail);
        size_t n = out.size();
        if (n > avail) {
            n = avail;
        }

        uint16_t i = static_cast<uint16_t>(tail & (kRxSize - 1));
        uint16_t first = static_cast<uint16_t>(kRxSize - i);
        if (first > n) {
            first = static_cast<uint16_t>(n);
        }
        std::memcpy(out.data(), &rx_buf_[i], first);
        if (n > first) {
            std::memcpy(out.data() + first, &rx_buf_[0], n - first);
        }
        rx_tail_ = static_cast<uint16_t>(tail + n);  // 消费游标最后发布
        return n;
    }

    /**
     * 取一个字节：有数据时返回 true 并把字节写入 *out；无数据返回 false。
     * @param out 输出参数（Google 风格：输出参数用指针，调用处一眼看出会被写）
     */
    bool readByte(uint8_t* out) noexcept
    {
        uint16_t const tail = rx_tail_;
        if (tail == rx_head_) {
            return false;
        }
        *out = rx_buf_[tail & (kRxSize - 1)];
        rx_tail_ = static_cast<uint16_t>(tail + 1);
        return true;
    }

    /**
     * 丢弃接收环中所有已到达数据。
     *
     * @return 被丢弃的字节数
     */
    size_t discardRx() noexcept
    {
        uint16_t const head = rx_head_;
        size_t n = static_cast<uint16_t>(head - rx_tail_);
        rx_tail_ = head;
        return n;
    }

    // ── 事件订阅（与 Tim::subscribe 同风格；ISR 上下文回调）──
    /// 订阅接收事件（帧/数据到达）
    void onRx(CbRx* cb, void* ctx = nullptr) noexcept
    {
        rx_ctx_ = ctx;
        rx_cb_ = cb;
    }

    /// 订阅发送队列排空事件
    void onTx(CbTx* cb, void* ctx = nullptr) noexcept
    {
        tx_ctx_ = ctx;
        tx_cb_ = cb;
    }

    // ── 观测与控制 ──
    /// 尚未上线的字节数（含已交给移位寄存器的最后一字节）
    [[nodiscard]] size_t txPending() const noexcept
    {
        return static_cast<uint16_t>(tx_head_ - tx_sent_);
    }

    /// 接收环中可读字节数
    [[nodiscard]] size_t rxPending() const noexcept
    {
        return static_cast<uint16_t>(rx_head_ - rx_tail_);
    }

    /// 发送泵是否在运行
    [[nodiscard]] bool busy() const noexcept
    {
        return tx_pumping_;
    }

    [[nodiscard]] Stats getStats() const noexcept
    {
        return Stats{tx_bytes_, rx_bytes_, rx_lost_, err_fe_, err_ne_, err_ore_, err_pe_};
    }

    /**
     * 等待发送彻底结束（环空 + 移位寄存器空，即线上出现空闲）。
     *
     * @return 线上空闲返回 true；超时返回 false
     */
    template <class Duration>
        requires std::convertible_to<Duration, std::chrono::milliseconds>
    bool flush(Duration timeout) noexcept
    {
        auto const start = System::now();
        while (!lineIdle()) {
            txRescue();
            if (System::now() - start > timeout) {
                return false;
            }
        }
        return true;
    }

    /// 无超时等待发送彻底结束
    void flush() noexcept
    {
        while (!lineIdle()) {
            txRescue();
        }
    }

private:
    ///< 仅 uart.cpp 的 trampoline / HAL 回调可进入内部处理
    friend void detail::uartIrqDispatch(uint8_t) noexcept;
    friend void detail::uartMspDispatch(uint8_t, bool) noexcept;
    friend void detail::uartRxEventDispatch(uint8_t, uint16_t) noexcept;
    friend void detail::uartTxDoneDispatch(uint8_t) noexcept;
    friend void detail::uartErrDispatch(uint8_t, uint32_t) noexcept;

    ///< 队列已空且最后一字节已移入移位寄存器（线上空闲）
    [[nodiscard]] bool lineIdle() const noexcept
    {
        if (tx_take_ != tx_sent_ || tx_take_ != tx_head_ || tx_pumping_) {
            return false;
        }
        if (tx_bytes_ == 0u) {
            return true;  // 从未发送过，TC 恒为 0
        }
        return (hal_.Instance->SR & USART_SR_TC) != 0u;
    }

    // ── 构造/析构辅助（uart.cpp）──
    void validate() const;
    void configureHal();
    void configureDma();
    void mspInit();    ///< HAL_UART_MspInit 入口：时钟/重映射/引脚/NVIC
    void mspDeInit();  ///< HAL_UART_MspDeInit 入口：引脚退回输入并关时钟
    void startRx();
    void armRx() noexcept;  ///< 装载/重装载 DMA 接收（失败时强制复位接收状态机重试）
    void stopAll();

    // ── 中断/泵（uart.cpp）──
    void isr() noexcept;
    void fastIsr() noexcept;                ///< 纯 IT：自管 TXE/RXNE/IDLE/错误
    void kickTx() noexcept;                 ///< 发送泵启动（临界区保护）
    void txChunkHal() noexcept;             ///< HAL 分块发送（IT/DMA）
    void onTxDone() noexcept;               ///< HAL TxCplt：继续泵下一块
    void onRxEvent(uint16_t pos) noexcept;  ///< HAL RxEvent：搬运 + 帧回调
    void onErr(uint32_t hal_err) noexcept;  ///< HAL ErrorCallback
    void notifyFrame(uint16_t pos) noexcept;
    void pushRx(uint8_t b) noexcept;
    void txRescue() noexcept;  ///< HAL 路径下异常中止后的发送泵自救（不破坏正常流程）

    UART_HandleTypeDef hal_{};
    DMA_HandleTypeDef dma_tx_{};
    DMA_HandleTypeDef dma_rx_{};
    Config cfg_;
    uint8_t idx_ = 0;  ///< 实例索引 0..2

    ///< true：USART 中断交给 HAL_UART_IRQHandler；false：自管快速 ISR
    bool hal_irq_ = false;

    // ── 发送环（生产者=用户，消费者=ISR）──
    uint8_t tx_buf_[kTxSize];
    volatile uint16_t tx_head_ = 0;  ///< 生产游标
    volatile uint16_t tx_take_ = 0;  ///< 交付游标（已交给 ISR/DMA）
    volatile uint16_t tx_sent_ = 0;  ///< 完成游标（已真正上线）
    volatile bool tx_pumping_ = false;

    // ── 接收环（生产者=ISR/DMA，消费者=用户）──
    uint8_t rx_buf_[kRxSize];
    volatile uint16_t rx_head_ = 0;  ///< 写游标（绝对，累计回绕）
    volatile uint16_t rx_tail_ = 0;  ///< 消费游标
    volatile uint16_t rx_fr_ = 0;    ///< 帧通知游标
    uint8_t frame_buf_[kFrameSize];  ///< DmaIdle 的单帧 DMA 缓冲

    CbRx* rx_cb_ = nullptr;
    void* rx_ctx_ = nullptr;
    CbTx* tx_cb_ = nullptr;
    void* tx_ctx_ = nullptr;

    // ── 统计（ISR 更新）──
    volatile uint32_t tx_bytes_ = 0;
    volatile uint32_t rx_bytes_ = 0;
    volatile uint32_t rx_lost_ = 0;
    volatile uint32_t err_fe_ = 0;
    volatile uint32_t err_ne_ = 0;
    volatile uint32_t err_ore_ = 0;
    volatile uint32_t err_pe_ = 0;
};

}  // namespace stm32
