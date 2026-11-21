#pragma once
// 主机端 mock HAL：只为编译/仿真 stm32::Uart、Gpio 的逻辑（不参与交叉编译）
#include <cstdint>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

// ── 寄存器 ──
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_TypeDef;
typedef struct {
    volatile uint32_t CCR, CNDTR, CPAR, CMAR, rsv;
} DMA_Channel_TypeDef;
typedef struct {
    volatile uint32_t ISR, IFCR;
} DMA_TypeDef;
typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_TypeDef;

extern USART_TypeDef g_usart[3];
extern DMA_Channel_TypeDef g_dmach[8];
extern DMA_TypeDef g_dma1;
extern GPIO_TypeDef g_gpioa, g_gpiob, g_gpioc;

#define USART1 (&g_usart[0])
#define USART2 (&g_usart[1])
#define USART3 (&g_usart[2])
#define DMA1_Channel1 (&g_dmach[1])
#define DMA1_Channel2 (&g_dmach[2])
#define DMA1_Channel3 (&g_dmach[3])
#define DMA1_Channel4 (&g_dmach[4])
#define DMA1_Channel5 (&g_dmach[5])
#define DMA1_Channel6 (&g_dmach[6])
#define DMA1_Channel7 (&g_dmach[7])
#define DMA1 (&g_dma1)
#define GPIOA (&g_gpioa)
#define GPIOB (&g_gpiob)
#define GPIOC (&g_gpioc)

// ── 中断号 ──
typedef enum {
    DMA1_Channel1_IRQn = 11,
    DMA1_Channel2_IRQn = 12,
    DMA1_Channel3_IRQn = 13,
    DMA1_Channel4_IRQn = 14,
    DMA1_Channel5_IRQn = 15,
    DMA1_Channel6_IRQn = 16,
    DMA1_Channel7_IRQn = 17,
    USART1_IRQn = 37,
    USART2_IRQn = 38,
    USART3_IRQn = 39,
} IRQn_Type;

// ── USART 位定义（只需互不冲突，不追求与硅片一致）──
#define USART_SR_PE (1u << 8)
#define USART_SR_NE (1u << 2)
#define USART_SR_FE (1u << 1)
#define USART_SR_ORE (1u << 3)
#define USART_SR_IDLE (1u << 4)
#define USART_SR_RXNE (1u << 5)
#define USART_SR_TC (1u << 6)
#define USART_SR_TXE (1u << 7)
#define USART_CR1_UE (1u << 13)
#define USART_CR1_RE (1u << 2)
#define USART_CR1_TE (1u << 3)
#define USART_CR1_IDLEIE (1u << 4)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TCIE (1u << 6)
#define USART_CR1_TXEIE (1u << 7)
#define USART_CR1_PEIE (1u << 8)
#define USART_CR3_EIE (1u << 0)
#define USART_CR3_DMAR (1u << 6)
#define USART_CR3_DMAT (1u << 7)

// ── UART 配置常量 ──
#define UART_WORDLENGTH_8B 0u
#define UART_STOPBITS_1 0u
#define UART_STOPBITS_2 2u
#define UART_PARITY_NONE 0u
#define UART_PARITY_EVEN 1u
#define UART_PARITY_ODD 3u
#define UART_MODE_TX_RX (USART_CR1_TE | USART_CR1_RE)
#define UART_HWCONTROL_NONE 0u
#define UART_OVERSAMPLING_16 0u

typedef enum {
    HAL_UART_STATE_RESET = 0,
    HAL_UART_STATE_READY = 0x20,
    HAL_UART_STATE_BUSY = 0x24,
    HAL_UART_STATE_BUSY_TX = 0x21,
    HAL_UART_STATE_BUSY_RX = 0x22,
} HAL_UART_StateTypeDef;

#define HAL_UART_ERROR_NONE 0x00u
#define HAL_UART_ERROR_PE 0x01u
#define HAL_UART_ERROR_FE 0x02u
#define HAL_UART_ERROR_NE 0x04u
#define HAL_UART_ERROR_ORE 0x08u
#define HAL_UART_ERROR_DMA 0x10u

typedef struct {
    uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
} UART_InitTypeDef;

typedef struct __UART_HandleTypeDef {
    USART_TypeDef* Instance;
    UART_InitTypeDef Init;
    const uint8_t* pTxBuffPtr;
    uint16_t TxXferSize;
    volatile uint16_t TxXferCount;
    uint8_t* pRxBuffPtr;
    uint16_t RxXferSize;
    volatile uint16_t RxXferCount;
    void* hdmatx;
    void* hdmarx;
    volatile HAL_UART_StateTypeDef gState;
    volatile HAL_UART_StateTypeDef RxState;
    volatile uint32_t ErrorCode;
} UART_HandleTypeDef;

// ── DMA ──
#define DMA_PERIPH_TO_MEMORY 0u
#define DMA_MEMORY_TO_PERIPH (1u << 4)
#define DMA_PINC_DISABLE 0u
#define DMA_MINC_ENABLE (1u << 7)
#define DMA_PDATAALIGN_BYTE 0u
#define DMA_MDATAALIGN_BYTE 0u
#define DMA_NORMAL 0u
#define DMA_CIRCULAR (1u << 5)
#define DMA_PRIORITY_LOW 0u
#define DMA_PRIORITY_MEDIUM (1u << 8)
#define DMA_PRIORITY_HIGH (1u << 9)
#define DMA_IFCR_CGIF1 1u

typedef struct {
    uint32_t Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority;
} DMA_InitTypeDef;

typedef struct __DMA_HandleTypeDef {
    DMA_Channel_TypeDef* Instance;
    DMA_InitTypeDef Init;
    void* Parent;
    volatile int State;
} DMA_HandleTypeDef;

// ── GPIO ──
typedef enum {
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT_PP,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_AF_PP,
    GPIO_MODE_AF_OD
} GPIO_Mode;
typedef enum { GPIO_SPEED_LOW, GPIO_SPEED_MEDIUM, GPIO_SPEED_HIGH } GPIO_Speed;
typedef enum { GPIO_NOPULL, GPIO_PULLUP, GPIO_PULLDOWN } GPIO_Pull;
typedef struct {
    uint16_t Pin;
    GPIO_Mode Mode;
    GPIO_Pull Pull;
    GPIO_Speed Speed;
} GPIO_InitTypeDef;

#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0

// ── 时钟 / 重映射（记录用）──
extern int g_clk_en[8];  // 0=AFIO 1=GPIOA 2=GPIOB 3=GPIOC 4=USART1 5=USART2 6=USART3 7=DMA1
extern int g_clk_dis[8];
extern uint32_t g_remap;
#define __HAL_RCC_AFIO_CLK_ENABLE() (++g_clk_en[0])
#define __HAL_RCC_GPIOA_CLK_ENABLE() (++g_clk_en[1])
#define __HAL_RCC_GPIOB_CLK_ENABLE() (++g_clk_en[2])
#define __HAL_RCC_GPIOC_CLK_ENABLE() (++g_clk_en[3])
#define __HAL_RCC_USART1_CLK_ENABLE() (++g_clk_en[4])
#define __HAL_RCC_USART2_CLK_ENABLE() (++g_clk_en[5])
#define __HAL_RCC_USART3_CLK_ENABLE() (++g_clk_en[6])
#define __HAL_RCC_DMA1_CLK_ENABLE() (++g_clk_en[7])
#define __HAL_RCC_USART1_CLK_DISABLE() (++g_clk_dis[4])
#define __HAL_RCC_USART2_CLK_DISABLE() (++g_clk_dis[5])
#define __HAL_RCC_USART3_CLK_DISABLE() (++g_clk_dis[6])
#define __HAL_AFIO_REMAP_USART1_ENABLE() (g_remap |= 1u << 0)
#define __HAL_AFIO_REMAP_USART1_DISABLE() (g_remap &= ~(1u << 0))
#define __HAL_AFIO_REMAP_USART2_DISABLE() (g_remap &= ~(1u << 1))
#define __HAL_AFIO_REMAP_USART3_ENABLE() (g_remap |= 3u << 2)
#define __HAL_AFIO_REMAP_USART3_PARTIAL() (g_remap = (g_remap & ~(3u << 2)) | (1u << 2))
#define __HAL_AFIO_REMAP_USART3_DISABLE() (g_remap &= ~(3u << 2))

// ── CMSIS 原语 ──
extern int g_irq_enabled[64];
extern int g_irq_disabled[64];
extern uint32_t g_irq_prio[64];
inline void NVIC_DisableIRQ(IRQn_Type n)
{
    ++g_irq_disabled[static_cast<int>(n)];
}
inline void NVIC_EnableIRQ(IRQn_Type n)
{
    ++g_irq_enabled[static_cast<int>(n)];
}
inline void __DMB() {}
extern int g_irq_masked;
inline uint32_t __get_PRIMASK()
{
    return g_irq_masked;
}
inline void __disable_irq()
{
    g_irq_masked = 1;
}
inline void __set_PRIMASK(uint32_t v)
{
    g_irq_masked = static_cast<int>(v);
}

// ── HAL 函数（实现见 mock_hal.cpp）──
void HAL_NVIC_SetPriority(IRQn_Type n, uint32_t pre, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type n);
HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, int state);
int HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin);

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* h);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* h);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef* h, const uint8_t* p, uint16_t n);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef* h, const uint8_t* p, uint16_t n);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef* h, uint8_t* p, uint16_t n);
void HAL_UART_IRQHandler(UART_HandleTypeDef* h);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef* h);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef* h);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef* h);

// HAL 回调（由 uart.cpp 提供强定义）
void HAL_UART_MspInit(UART_HandleTypeDef* h);
void HAL_UART_MspDeInit(UART_HandleTypeDef* h);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* h, uint16_t size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef* h);
void HAL_UART_ErrorCallback(UART_HandleTypeDef* h);

// ── mock 记录 ──
extern int g_dma_inits;
extern uint8_t const* g_tx_last_ptr;  // 最近一次 HAL 发送的缓冲
extern uint16_t g_tx_last_len;
extern int g_tx_it_calls, g_tx_dma_calls;
extern uint8_t const* g_rx_last_buf;  // 最近一次装载的接收缓冲
extern uint16_t g_rx_last_size;
extern int g_rx_arm_calls;
extern int g_gpio_init[3][4];  // [port][0=input,1=af_pp,2=其他]
extern uint16_t g_gpio_last_pin[3];
extern UART_HandleTypeDef* g_uart_held[3];
extern int g_dma_aborts;
extern int g_uart_irq_handler_calls;
