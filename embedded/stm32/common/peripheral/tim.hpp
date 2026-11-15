#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>

#include "stm32f103xb.h"  // TIM_TypeDef / RCC_TypeDef / 位定义
#include "system/system.hpp"
#include "util.hpp"

namespace stm32
{

namespace detail
    {

    ///< TIM1~TIM4 外设寄存器基址（宏在此展开为外设指针）
    static TIM_TypeDef* kTimAddr[4] = {TIM1, TIM2, TIM3, TIM4};

/**
 * per-instance IRQ trampoline（tim.cpp）入口。
 *
 * @param idx 实例索引（0..3，对应 TIM1~TIM4）
 */
void irqDispatch(uint8_t idx) noexcept;
}  // namespace detail

/**
 * 通用 16 位定时器（TIM1~TIM4）。
 *
 * 周期与计数模式在构造时确定；运行中仅可 start/stop 与事件订阅/退订。
 */
class Tim final
{
public:
    ///< 事件回调类型（使用处为 Cb*）：(事件位, 用户上下文)
    using Cb = void(uint16_t, void*);
    ///< TIM 实例（0..3，对应 kTimAddr 顺序）
    enum class Inst : uint8_t { Tim1, Tim2, Tim3, Tim4 };
    ///< 计数模式（CR1.CMS）
    enum class Mode : uint8_t { Up, Down, Center1, Center2 };
    /// 事件位掩码（与 TIMx_SR / TIMx_DIER 位一一对应）
    enum class Evt : uint16_t {
        Update = 1u << 0,
        Ch1 = 1u << 1,
        Ch2 = 1u << 2,
        Ch3 = 1u << 3,
        Ch4 = 1u << 4,
    };
    /// 事件掩码组合
    friend constexpr Evt operator|(Evt a, Evt b) noexcept
    {
        return static_cast<Evt>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }
    /// 事件掩码求交
    friend constexpr Evt operator&(Evt a, Evt b) noexcept
    {
        return static_cast<Evt>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }

    /**
     * 构造定时器实例。
     *
     * @param inst 定时器实例
     * @param period 周期（可转换为微秒且必须为正）
     * @param mode 计数模式
     * @note 同一实例重复构造（0x5002）或周期越界（0x5001）时 panic
     */
    template <class Duration>
        requires std::convertible_to<Duration, std::chrono::microseconds>
    explicit Tim(Inst inst, Duration period, Mode mode = Mode::Up) noexcept
            : idx_(static_cast<uint8_t>(inst))
    {
        if (g_active[idx_] != nullptr) {
            System::panic(0x5002);  // 同一实例重复构造
        }

        calcPscArr(toMicroseconds(period));
        configure(inst, mode);
        g_active[idx_] = this;
    }

    /**
     * 析构：停止计数、清事件状态、禁用中断线、摘除实例注册。
     */
    ~Tim()
    {
        stop();
        TIM_TypeDef* r = base();
        r->DIER = 0;
        r->SR = 0x1Fu;
        NVIC_DisableIRQ(static_cast<IRQn_Type>(kIrqn[idx_]));
        g_active[idx_] = nullptr;
    }

    Tim(Tim const&) = delete;
    Tim& operator=(Tim const&) = delete;

    // ── 生命周期 ──
    /// 启动计数（置 CR1.CEN）
    void start() noexcept
    {
        base()->CR1 |= TIM_CR1_CEN;
    }

    /// 停止计数（清 CR1.CEN）
    void stop() noexcept
    {
        base()->CR1 &= ~TIM_CR1_CEN;
    }

    // ── 事件 ──
    /**
     * 订阅事件（运行中可动态订阅/退订；每个事件位一个回调槽）。
     *
     * F1 中断线：Tim1 Update→TIM1_UP、Ch1~4→TIM1_CC；Tim2~4 共用一线。
     * 同一 Tim 上可共存多组回调（如 Update 心跳 + 通道捕获）。
     *
     * @param evts 要订阅的事件掩码
     * @param cb 事件回调
     * @param ctx 用户上下文，原样传给 cb
     * @param irqPri 中断优先级（0 最高）
     */
    void subscribe(Evt evts, Cb* cb, void* ctx = nullptr, uint8_t irqPri = 2) noexcept
    {
        const uint16_t m = static_cast<uint16_t>(evts);
        evts_ |= m;
        for (uint8_t b = 0; b < 5u; ++b) {
            if (m & (1u << b)) {
                slots_[b] = {cb, ctx};
                base()->DIER |= static_cast<uint16_t>(1u << b);
            }
        }
        NVIC_SetPriority(static_cast<IRQn_Type>(kIrqn[idx_]), irqPri);
        NVIC_EnableIRQ(static_cast<IRQn_Type>(kIrqn[idx_]));
    }

    /**
     * 退订事件：清对应事件位的回调槽并禁用中断线。
     *
     * @param evts 要退订的事件掩码
     */
    void unsubscribe(Evt evts) noexcept
    {
        const uint16_t m = static_cast<uint16_t>(evts);
        evts_ &= ~m;
        for (uint8_t b = 0; b < 5u; ++b) {
            if (m & (1u << b)) {
                slots_[b] = {};
                base()->DIER &= ~static_cast<uint16_t>(1u << b);
            }
        }

        NVIC_DisableIRQ(static_cast<IRQn_Type>(kIrqn[idx_]));
    }

private:
    /// 唯一外部入口：per-instance IRQ trampoline（tim.cpp）
    friend void detail::irqDispatch(uint8_t idx) noexcept;

    /// 清已触发事件的状态位并按已订阅事件位逐个回调（仅由 detail::irqDispatch 调用）
    void handleIrq() noexcept
    {
        TIM_TypeDef* r = base();
        const uint16_t sr = static_cast<uint16_t>(r->SR);
        uint16_t ev = 0;
        if (sr & TIM_SR_UIF) {
            r->SR = ~TIM_SR_UIF;
            ev |= 1u << 0;
        }
        if (sr & TIM_SR_CC1IF) {
            r->SR = ~TIM_SR_CC1IF;
            ev |= 1u << 1;
        }
        if (sr & TIM_SR_CC2IF) {
            r->SR = ~TIM_SR_CC2IF;
            ev |= 1u << 2;
        }
        if (sr & TIM_SR_CC3IF) {
            r->SR = ~TIM_SR_CC3IF;
            ev |= 1u << 3;
        }
        if (sr & TIM_SR_CC4IF) {
            r->SR = ~TIM_SR_CC4IF;
            ev |= 1u << 4;
        }
        ev &= evts_;
        for (uint8_t b = 0; b < 5u; ++b) {  // 逐事件回调
            if (ev & (1u << b)) {
                const Slot& s = slots_[b];
                if (s.cb != nullptr) {
                    s.cb(static_cast<uint16_t>(1u << b), s.ctx);
                }
            }
        }
    }

    ///< 每实例的主中断线：Tim2~4 全部事件共用；Tim1 仅 Update 用它
    static constexpr uint8_t kIrqn[4] = {TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn};
    ///< 每实例至多一个 Tim（构造注册/析构摘除）
    inline static Tim* g_active[4] = {};

    /// 本实例的寄存器基址
    [[nodiscard]] TIM_TypeDef* base() const noexcept
    {
        return detail::kTimAddr[idx_];
    }

    /// 计数模式 → CR1.CMS 位值
    static constexpr uint8_t cmsBits(Mode m) noexcept
    {
        switch (m) {
        case Mode::Up:
            return 0;
        case Mode::Down:
            return 1;
        case Mode::Center1:
            return 2;
        case Mode::Center2:
            return 3;
        }
        return 0;
    }

    /**
     * 周期(μs) → 预分频/重载值（均存 +1 形式）。
     *
     * @param us 周期（微秒）
     * @note 越界时 panic（0x5001）
     */
    void calcPscArr(uint32_t us) noexcept
    {
        if (us == 0u || us > static_cast<uint32_t>(UINT32_MAX / System::ticksPerUs())) {
            System::panic(0x5001);
        }
        const uint32_t ticks = System::ticksPerUs() * us;

        // 计算预分频值，向上取整
        const uint32_t d = (ticks - 1u) / UINT16_MAX + 1u;

        if (d > std::numeric_limits<uint16_t>::max() || ticks / d < 2u) {
            System::panic(0x5001);  // 周期越界
        }
        psc1_ = static_cast<uint16_t>(d);          // PSC+1
        arr1_ = static_cast<uint16_t>(ticks / d);  // ARR+1
    }

    /**
     * 配置基础寄存器（仅构造函数调用）。
     *
     * 清通道/事件配置，写入 PSC/ARR，按模式设置计数方向并使寄存器生效。
     *
     * @param inst 定时器实例
     * @param mode 计数模式
     */
    void configure(Inst inst, Mode mode) noexcept
    {
        enableClock(inst);
        TIM_TypeDef* r = base();
        r->SMCR = 0;
        r->CCMR1 = 0;
        r->CCMR2 = 0;
        r->CCER = 0;
        r->DIER = 0;
        r->PSC = static_cast<uint16_t>(psc1_ - 1u);
        r->ARR = static_cast<uint16_t>(arr1_ - 1u);
        r->CR1 = (static_cast<uint32_t>(cmsBits(mode)) << TIM_CR1_CMS_Pos) | TIM_CR1_ARPE;
        r->SR = 0x00;
        r->EGR = TIM_EGR_UG;  // 产生更新：PSC/ARR 生效
        r->SR = ~TIM_SR_UIF; //  W0C
    }

    /// 使能对应定时器的外设时钟（TIM1→APB2，Tim2~4→APB1）
    static void enableClock(Inst inst) noexcept
    {
        if (inst == Inst::Tim1) {
            RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
        } else {
            // Tim2→APB1ENR bit0, Tim3→bit1, Tim4→bit2
            RCC->APB1ENR |= 1u << (static_cast<unsigned>(inst) - 1u);
        }
    }

    uint8_t idx_ = 0;    ///< 实例索引 0..3
    uint16_t psc1_ = 0;  ///< PSC+1
    uint16_t arr1_ = 0;  ///< ARR+1
    uint16_t evts_ = 0;  ///< 已订阅事件掩码

    struct Slot {
        Cb* cb = nullptr;
        void* ctx = nullptr;
    };
    Slot slots_[5] = {};  ///< 每事件位一个回调槽（Update/Ch1~4）
};

namespace detail
{
inline void irqDispatch(uint8_t idx) noexcept
{
    if (Tim* p = Tim::g_active[idx]) {
        p->handleIrq();
    }
}
}  // namespace detail

}  // namespace stm32
