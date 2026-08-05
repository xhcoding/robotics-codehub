#include "peripheral/gpio.hpp"

#include "stm32f1xx_hal.h"

namespace stm32
{

Gpio::Gpio(Group group, uint8_t pin) : pin_(1 << pin)
{
    if (group == Group::A) {
        port_ = GPIOA;
    } else if (group == Group::B) {
        port_ = GPIOB;
    } else if (group == Group::C) {
        port_ = GPIOC;
    }
}

void Gpio::configure(Mode mode, Speed speed, Pull pull)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    gpio_cfg.Pin = pin_;
    
    switch (mode) {
    case Mode::INPUT:
        gpio_cfg.Mode = GPIO_MODE_INPUT;
        break;
    case Mode::OUTPUT_PP:
        gpio_cfg.Mode = GPIO_MODE_OUTPUT_PP;
        break;
    case Mode::OUTPUT_OD:
        gpio_cfg.Mode = GPIO_MODE_OUTPUT_OD;
        break;
    }

    switch (speed) {
    case Speed::LOW:
        gpio_cfg.Speed = GPIO_SPEED_LOW;
        break;
    case Speed::MEDIUM:
        gpio_cfg.Speed = GPIO_SPEED_MEDIUM;
        break;
    case Speed::HIGH:
        gpio_cfg.Speed = GPIO_SPEED_HIGH;
        break;
    }

    switch (pull) {
    case Pull::NO:
        gpio_cfg.Pull = GPIO_NOPULL;
        break;
    case Pull::UP:
        gpio_cfg.Pull = GPIO_PULLUP;
        break;
    case Pull::DOWN:
        gpio_cfg.Pull = GPIO_PULLDOWN;
        break;
    }
    // 始能时钟
    if (port_ == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port_ == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port_ == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }

    HAL_GPIO_Init(static_cast<GPIO_TypeDef*>(port_), &gpio_cfg);
}

void Gpio::set()
{
    HAL_GPIO_WritePin(static_cast<GPIO_TypeDef*>(port_), pin_, GPIO_PIN_SET);
}

void Gpio::reset()
{
    HAL_GPIO_WritePin(static_cast<GPIO_TypeDef*>(port_), pin_, GPIO_PIN_RESET);
}

bool Gpio::test()
{
    return HAL_GPIO_ReadPin(static_cast<GPIO_TypeDef*>(port_), pin_) == GPIO_PIN_SET;
}

void Gpio::toggle()
{
    HAL_GPIO_TogglePin(static_cast<GPIO_TypeDef*>(port_), pin_);
}

}  // namespace stm32
