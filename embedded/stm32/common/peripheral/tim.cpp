/*!
 * \file tim.cpp
 * \brief per-instance TIM 中断 trampoline。
 *
 * 注意：F1 的 TIM1 有 4 条独立中断线（BRK/UP/TRG_COM/CC），
 * 启动文件的向量槽引用 4 个独立符号名，都要提供强定义。
 * 当前 API 只订阅 Update(UP 线) 与 Ch1~4(CC 线)，BRK/TRG_COM 留给高级特性扩展。
 */
#include "peripheral/tim.hpp"

extern "C" void TIM1_BRK_IRQHandler(void)
{
    stm32::detail::irqDispatch(0);
}

extern "C" void TIM1_UP_IRQHandler(void)
{
    stm32::detail::irqDispatch(0);
}

extern "C" void TIM1_TRG_COM_IRQHandler(void)
{
    stm32::detail::irqDispatch(0);
}

extern "C" void TIM1_CC_IRQHandler(void)
{
    stm32::detail::irqDispatch(0);
}

extern "C" void TIM2_IRQHandler(void)
{
    stm32::detail::irqDispatch(1);
}

extern "C" void TIM3_IRQHandler(void)
{
    stm32::detail::irqDispatch(2);
}

extern "C" void TIM4_IRQHandler(void)
{
    stm32::detail::irqDispatch(3);
}
