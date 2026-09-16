#include "system/system.hpp"

#include "stm32f1xx_hal.h"

namespace stm32
{

bool System::init()
{

    HAL_Init();

    RCC_OscInitTypeDef osc_cfg = {0};
    RCC_ClkInitTypeDef clk_cfg = {0};

    // 将 HSE 作为 PLL 时钟源，且 9 倍频，使得 STM32 达到 72Mhz
    osc_cfg.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc_cfg.HSEState = RCC_HSE_ON;
    osc_cfg.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc_cfg.PLL.PLLState = RCC_PLL_ON;
    osc_cfg.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc_cfg.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc_cfg) != HAL_OK) {
        return false;
    }

    // 设置 PLL 作为时钟源，配置 SysClk, HCLK, PCLK1, PCLK2

    clk_cfg.ClockType =
            (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    clk_cfg.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk_cfg.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_cfg.APB2CLKDivider = RCC_SYSCLK_DIV1;
    clk_cfg.APB1CLKDivider = RCC_SYSCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk_cfg, FLASH_LATENCY_2) != HAL_OK) {
        return false;
    }

    return true;
}

void System::delay(std::chrono::milliseconds ms)
{
    HAL_Delay(ms.count());
}

std::chrono::milliseconds System::now()
{
    return std::chrono::milliseconds(HAL_GetTick());
}

}  // namespace stm32

// 系统中断函数实现
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}
