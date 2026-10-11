#pragma once
#include <cstdint>

enum HAL_StatusTypeDef { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
enum IRQn_Type { USART1_IRQn, USART2_IRQn, USART6_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, DMA2_Stream1_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn };
struct GPIO_TypeDef {};
struct USART_TypeDef {};
struct DMA_Stream_TypeDef {};
inline GPIO_TypeDef uart_gpio{};
inline USART_TypeDef uart_instances[3]{};
inline DMA_Stream_TypeDef uart_streams[16]{};
#define GPIOA (&uart_gpio)
#define USART1 (&uart_instances[0])
#define USART2 (&uart_instances[1])
#define USART6 (&uart_instances[2])
#define DMA1_Stream0 (&uart_streams[0])
#define DMA1_Stream1 (&uart_streams[1])
#define DMA1_Stream2 (&uart_streams[2])
#define DMA1_Stream3 (&uart_streams[3])
#define DMA1_Stream4 (&uart_streams[4])
#define DMA1_Stream5 (&uart_streams[5])
#define DMA1_Stream6 (&uart_streams[6])
#define DMA1_Stream7 (&uart_streams[7])
#define DMA2_Stream0 (&uart_streams[8])
#define DMA2_Stream1 (&uart_streams[9])
#define DMA2_Stream2 (&uart_streams[10])
#define DMA2_Stream3 (&uart_streams[11])
#define DMA2_Stream4 (&uart_streams[12])
#define DMA2_Stream5 (&uart_streams[13])
#define DMA2_Stream6 (&uart_streams[14])
#define DMA2_Stream7 (&uart_streams[15])
#define GPIO_NOPULL 0
#define GPIO_PULLUP 1
#define GPIO_PULLDOWN 2
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_AF_PP 2
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_SPEED_FREQ_HIGH 2
#define IS_GPIO_PULL(p) ((p) <= GPIO_PULLDOWN)
#define UART_MODE_RX 1
#define UART_MODE_TX 2
#define UART_MODE_TX_RX 3
#define IS_UART_MODE(m) ((m) >= 1 && (m) <= 3)
#define UART_WORDLENGTH_8B 0
#define UART_STOPBITS_1 0
#define UART_PARITY_NONE 0
#define UART_HWCONTROL_NONE 0
#define UART_OVERSAMPLING_16 0
#define DMA_CHANNEL_4 4
#define DMA_CHANNEL_5 5
#define DMA_PERIPH_TO_MEMORY 0
#define DMA_MEMORY_TO_PERIPH 1
#define DMA_PINC_DISABLE 0
#define DMA_MINC_ENABLE 1
#define DMA_PDATAALIGN_BYTE 0
#define DMA_MDATAALIGN_BYTE 0
#define DMA_CIRCULAR 1
#define DMA_NORMAL 0
#define DMA_PRIORITY_MEDIUM 1
#define DMA_FIFOMODE_DISABLE 0
#define HAL_UART_ERROR_NONE 0u
#define HAL_UART_ERROR_PE 1u
#define HAL_UART_ERROR_NE 2u
#define HAL_UART_ERROR_FE 4u
#define HAL_UART_ERROR_ORE 8u
#define HAL_UART_ERROR_DMA 16u
#define HAL_UART_STATE_READY 0
#define HAL_UART_STATE_BUSY_RX 1
#define HAL_UART_STATE_BUSY_TX 2
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART6_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)

struct DMA_InitTypeDef
{
    std::uint32_t Channel, Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority, FIFOMode;
};
struct DMA_HandleTypeDef
{
    DMA_Stream_TypeDef *Instance = nullptr;
    DMA_InitTypeDef Init{};
    void *Parent = nullptr;
};
struct UART_InitTypeDef
{
    std::uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
};
struct UART_HandleTypeDef
{
    USART_TypeDef *Instance = nullptr;
    UART_InitTypeDef Init{};
    DMA_HandleTypeDef *hdmarx = nullptr;
    DMA_HandleTypeDef *hdmatx = nullptr;
    std::uint32_t ErrorCode = 0;
    unsigned RxState = HAL_UART_STATE_READY;
};
inline unsigned fake_rx_starts = 0;
inline unsigned fake_tx_aborts = 0;
inline unsigned fake_flag_clears = 0;
inline HAL_StatusTypeDef fake_rx_start_result = HAL_OK;
inline HAL_StatusTypeDef fake_tx_start_result = HAL_OK;
#define __HAL_UART_CLEAR_PEFLAG(h) (++fake_flag_clears)
inline HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *) { return HAL_OK; }
inline HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *) { return HAL_OK; }
inline void HAL_NVIC_SetPriority(IRQn_Type, unsigned, unsigned) {}
inline void HAL_NVIC_EnableIRQ(IRQn_Type) {}
inline std::uint32_t HAL_UART_GetError(const UART_HandleTypeDef *h) { return h->ErrorCode; }
inline HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, std::uint8_t *, std::uint16_t)
{
    ++fake_rx_starts;
    h->ErrorCode = HAL_UART_ERROR_NONE;
    if (fake_rx_start_result == HAL_OK) h->RxState = HAL_UART_STATE_BUSY_RX;
    return fake_rx_start_result;
}
inline HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const std::uint8_t *, std::uint16_t) { return fake_tx_start_result; }
inline HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *) { ++fake_tx_aborts; return HAL_OK; }
inline void HAL_UART_IRQHandler(UART_HandleTypeDef *) {}
inline void HAL_DMA_IRQHandler(DMA_HandleTypeDef *) {}
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *, std::uint16_t);
