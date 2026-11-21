// mock HAL 实现：忠实模仿 HAL 的状态机契约，便于在主机上驱动 stm32::Uart 逻辑
#include <cstring>

#include "stm32f1xx_hal.h"

USART_TypeDef g_usart[3]{};
DMA_Channel_TypeDef g_dmach[8]{};
DMA_TypeDef g_dma1{};
GPIO_TypeDef g_gpioa{}, g_gpiob{}, g_gpioc{};

int g_clk_en[8]{};
int g_clk_dis[8]{};
uint32_t g_remap = 0;

int g_irq_enabled[64]{};
int g_irq_disabled[64]{};
uint32_t g_irq_prio[64]{};
int g_irq_masked = 0;

int g_dma_inits = 0;
uint8_t const* g_tx_last_ptr = nullptr;
uint16_t g_tx_last_len = 0;
int g_tx_it_calls = 0, g_tx_dma_calls = 0;
uint8_t const* g_rx_last_buf = nullptr;
uint16_t g_rx_last_size = 0;
int g_rx_arm_calls = 0;
int g_gpio_init[3][4]{};
uint16_t g_gpio_last_pin[3]{};
UART_HandleTypeDef* g_uart_held[3]{};
int g_dma_aborts = 0;
int g_uart_irq_handler_calls = 0;

void HAL_NVIC_SetPriority(IRQn_Type n, uint32_t pre, uint32_t)
{
    g_irq_prio[static_cast<int>(n)] = pre;
}

void HAL_NVIC_EnableIRQ(IRQn_Type n)
{
    ++g_irq_enabled[static_cast<int>(n)];
}

static int portIdx(GPIO_TypeDef* p)
{
    if (p == GPIOA) {
        return 0;
    }
    if (p == GPIOB) {
        return 1;
    }
    return 2;
}

HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init)
{
    int const p = portIdx(port);
    ++g_gpio_init[p][init->Mode == GPIO_MODE_INPUT ? 0 : (init->Mode == GPIO_MODE_AF_PP ? 1 : 2)];
    g_gpio_last_pin[p] = init->Pin;
    return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef*, uint16_t, int) {}
int HAL_GPIO_ReadPin(GPIO_TypeDef*, uint16_t)
{
    return 0;
}
void HAL_GPIO_TogglePin(GPIO_TypeDef*, uint16_t) {}

// ── UART：模仿 HAL 的 gState/RxState 契约 ──

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* h)
{
    if (h->gState != HAL_UART_STATE_RESET) {
        return HAL_BUSY;
    }
    if (h->Instance == USART1) {
        g_uart_held[0] = h;
    } else if (h->Instance == USART2) {
        g_uart_held[1] = h;
    } else if (h->Instance == USART3) {
        g_uart_held[2] = h;
    }
    HAL_UART_MspInit(h);
    h->gState = HAL_UART_STATE_READY;
    h->RxState = HAL_UART_STATE_READY;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* h)
{
    HAL_UART_MspDeInit(h);
    h->gState = HAL_UART_STATE_RESET;
    h->RxState = HAL_UART_STATE_RESET;
    if (h->Instance == USART1) {
        g_uart_held[0] = nullptr;
    } else if (h->Instance == USART2) {
        g_uart_held[1] = nullptr;
    } else if (h->Instance == USART3) {
        g_uart_held[2] = nullptr;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef startTx(UART_HandleTypeDef* h, const uint8_t* p, uint16_t n, bool dma)
{
    if (h->gState != HAL_UART_STATE_READY) {
        return HAL_BUSY;  // HAL 的真实行为
    }
    if (p == nullptr || n == 0u) {
        return HAL_ERROR;
    }
    h->gState = HAL_UART_STATE_BUSY_TX;
    h->pTxBuffPtr = p;
    h->TxXferSize = n;
    h->TxXferCount = n;
    g_tx_last_ptr = p;
    g_tx_last_len = n;
    if (dma) {
        ++g_tx_dma_calls;
        h->Instance->CR3 |= USART_CR3_DMAT;
    } else {
        ++g_tx_it_calls;
        h->Instance->CR1 |= USART_CR1_TXEIE;
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef* h, const uint8_t* p, uint16_t n)
{
    return startTx(h, p, n, false);
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef* h, const uint8_t* p, uint16_t n)
{
    return startTx(h, p, n, true);
}

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef* h, uint8_t* p, uint16_t n)
{
    if (h->RxState != HAL_UART_STATE_READY) {
        return HAL_BUSY;  // HAL 的真实行为
    }
    if (p == nullptr || n == 0u) {
        return HAL_ERROR;
    }
    h->RxState = HAL_UART_STATE_BUSY_RX;
    h->pRxBuffPtr = p;
    h->RxXferSize = n;
    h->RxXferCount = n;
    g_rx_last_buf = p;
    g_rx_last_size = n;
    ++g_rx_arm_calls;
    return HAL_OK;
}

void HAL_UART_IRQHandler(UART_HandleTypeDef*)
{
    ++g_uart_irq_handler_calls;  // 测试直接调用 HAL 回调来模拟事件，这里只计数
}

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef* h)
{
    if (h->Instance == nullptr) {
        return HAL_ERROR;
    }
    ++g_dma_inits;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef* h)
{
    ++g_dma_aborts;
    if (h->Parent != nullptr) {
        auto* hu = static_cast<UART_HandleTypeDef*>(h->Parent);
        if (hu->hdmarx == h) {
            hu->RxState = HAL_UART_STATE_READY;
        } else {
            hu->gState = HAL_UART_STATE_READY;
        }
    }
    return HAL_OK;
}

void HAL_DMA_IRQHandler(DMA_HandleTypeDef*) {}
